"""Receiver for the existing-register alternating-bit telemetry transport.

No acknowledgement until a symbol has been consumed. The producer holds a
snapshot until all payload symbols are acknowledged; repeated polls are
ignored. A missing/bad/final packet is an error, never an empty successful run.
"""
def decode_v2(symbols):
    packed=sum(value<<(10*i) for i,value in enumerate(symbols))
    bits=len(symbols)*10;cursor=0;words=[]
    def take(width):
        nonlocal cursor
        assert cursor+width<=bits,'Truncated mailbox code'
        value=(packed>>cursor)&((1<<width)-1);cursor+=width
        return value
    while len(words)<81:
        tag=take(2)
        if tag==0:
            run=take(6)+1
            assert len(words)+run<=81,'Mailbox zero run overflow'
            words.extend([0]*run)
        else:
            value=take((8,16,32)[tag-1])
            assert value>0 and (tag==1 or value>(255 if tag==2 else 65535)), 'Noncanonical mailbox integer'
            words.append(value)
    assert 0<=bits-cursor<10 and packed>>cursor==0,'Nonzero or excess mailbox padding'
    return words

def valid_menu_sample(sample):
    words=sample.get('structure_telemetry',[]);status=sample.get('telemetry',[])
    return (len(words)==16 and len(status)==64 and words[0]==0x55493630 and
            words[7]==(words[1]^0xffffffff) and words[8]==0x4d4e5531 and
            words[15]==(words[9]^0xffffffff) and words[4]==words[9] and
            not status[0]&8 and status[1] in (0,1))

def valid_voxel_sample(sample):
    meta=sample.get('structure_telemetry',[]);data=sample.get('telemetry',[])
    return (len(meta)==16 and len(data)==64 and meta[0]==0x56585031 and
            data[0]==0x56584631 and data[16]==0x56585431 and
            meta[1]==data[1] and meta[15]==data[15]==data[63]==(data[1]^0xffffffff) and
            0<meta[2]==data[37]<131072 and meta[3] in (80,160) and meta[4]==meta[3]*3//4 and
            meta[5]==meta[3]*meta[4] and meta[14]==data[17] and
            0<=data[4]<32*1024 and 0<=data[5]<16*1024 and 0<=data[6]<32*1024)

class Mailbox:
    def __init__(self,compact_final=False):
        self.phase=0
        self.symbols=None
        self.start=None
        self.final=False
        self.waiting_final=False
        self.version=1
        self.count=None
        self.finished=False
        self.compact_final=bool(compact_final)
        self.retirement_only=False
        self.retirement=None
        self.last_progress=None

    def check_liveness(self,now,started):
        # Slow software rendering still advances the alternating-bit stream.
        # Keep the20s no-progress gate, with a separate bounded whole-packet
        # deadline (120s includes at most278 symbols). CRC remains mandatory.
        if now-max(started,self.last_progress if self.last_progress is not None else started)>20:
            raise TimeoutError('Telemetry symbols stalled; run fails')
        if self.start is not None and now-self.start['monotonic']>120:
            raise TimeoutError('Telemetry packet deadline exceeded; run fails')

    def accept(self,position,stamp):
        assert not self.finished or ((position>>10)&1)==self.phase,'Data after final mailbox packet'
        if self.waiting_final and (position>>16)!=0x35f:return None
        phase=(position>>10)&1
        if phase==self.phase:return None
        self.last_progress=stamp['monotonic']
        value=position&1023
        self.phase=phase
        if (position>>16)==0x35f and not self.final:
            self.symbols=None;self.start=None
        if self.symbols is None:
            assert value in (0x3a5,0x3a6,0x3a7,0x3a8),'Missing mailbox packet marker'
            self.version=2 if value>=0x3a7 else 1
            self.count=None if self.version==2 else 260
            self.final=value in (0x3a6,0x3a8)
            assert self.final==((position>>16)==0x35f),'Mailbox final page mismatch'
            self.symbols=[];self.start=stamp
            return None
        if self.count is None:
            assert 1<=value<=276,'Invalid mailbox payload length'
            self.count=value
            return None
        self.symbols.append(value)
        if len(self.symbols)<self.count:return None
        if self.version==2:words=decode_v2(self.symbols)
        else:
            packed=sum(value<<(10*i) for i,value in enumerate(self.symbols))
            assert packed>>(81*32)==0,'Nonzero mailbox padding'
            words=[(packed>>(32*i))&0xffffffff for i in range(81)]
        checksum=2166136261
        for word in words[:80]:checksum=((checksum^word)*16777619)&0xffffffff
        assert checksum==words[80],'Mailbox checksum mismatch'
        retirement=None
        if words[0]==0x52545231:
            assert self.final and self.waiting_final and self.compact_final,'Unrequested retirement packet'
            flags=words[1];application=flags&7;audio=(flags>>3)&0x10f;route=(flags>>12)&3
            assert not flags&~0x387f and application in (1,2,3),'Invalid retirement flags'
            assert (audio&15)<=8 and route<=2 and (application==3 or route==0),'Invalid retirement settings'
            assert all(word==0 for word in words[3:80]),'Nonzero retirement reserved fields'
            retirement=dict(application={1:'menu',2:'game',3:'touhou'}[application],
                            audio=audio,route=route,fault=words[2])
        elif self.final and self.compact_final:
            raise AssertionError('Missing requested retirement packet')
        result=dict(self.start,telemetry=words[16:80],structure_telemetry=words[:16],
                    mailbox_checksum=checksum,mailbox_symbols=self.count+(2 if self.version==2 else 1),
                    mailbox_version=self.version,mailbox_final=self.final,
                    mailbox_received_monotonic=stamp['monotonic'])
        if retirement is not None:
            result.update(retirement_only=True,retirement=retirement)
        self.retirement_only=retirement is not None
        self.retirement=retirement
        self.symbols=None;self.start=None
        self.finished=self.final
        return result

    def request_final(self,position):
        self.phase=(position>>10)&1;self.symbols=None;self.start=None
        self.waiting_final=True
        return 0x102|self.phase|(4 if self.compact_final else 0)
