"""Original local UI recordings mixed by the existing single PCM producer.

No MMIO owner, input writer, thread or hardware reset is added. UI effects
are monophonic; a new effect replaces the old one at the next submitted sample.
"""
import hashlib
import zlib
import numpy as np
from scarlet_pcm import ScarletMusic
from pcm_fifo_write import write_pcm


class ScarletUiMusic(ScarletMusic):
    def __init__(self,data,loop_start,loop_end,expected_sha256,effects):
        self.effects={}
        for kind,spec in effects.items():
            raw,digest=spec
            if kind not in ('select','confirm','cancel') or len(raw)%2 or hashlib.sha256(raw).hexdigest()!=digest:
                raise ValueError('Invalid immutable UI effect')
            self.effects[kind]=memoryview(raw).cast('h')
        super().__init__(data,loop_start,loop_end,expected_sha256)

    def begin(self,segment):
        super().begin(segment)
        self.generated=bytearray();self.ui_events=[];self.ui_event_count=0;self.effect=None;self.effect_cursor=0

    def play_ui(self,kind):
        if kind not in self.effects:raise ValueError('Unknown UI effect')
        self.ui_events.append(dict(kind=kind,start_sample=self.sent_samples))
        self.ui_event_count+=1
        self.effect=self.effects[kind];self.effect_cursor=0

    def poll(self,control):
        if control.read(0x28)!=0:return 0
        if not self.checked_signature:
            if control.read(0xc0)!=0x50434d31 or control.read(0xcc)!=32000:
                raise RuntimeError('PCM hardware signature/rate mismatch')
            self.checked_signature=True
        status=control.read(0xb4)
        if not status&(1<<16) or status&(1<<17):return 0
        queued=status&4095
        if queued>self.FIFO_WORDS:raise RuntimeError('PCM FIFO count outside accepted hardware capacity')
        words=min(self.BURST_WORDS,max(0,self.TARGET_WORDS-queued))
        if not words:return 0
        remaining=words*2;parts=[]
        while remaining:
            take=min(remaining,self.loop_end-self.cursor)
            parts.append(self.data[self.cursor*2:(self.cursor+take)*2]);self.cursor+=take;remaining-=take
            if self.cursor==self.loop_end:self.cursor=self.loop_start;self.loops+=1
        raw=b''.join(parts)
        if self.effect is not None:
            take=min(words*2,len(self.effect)-self.effect_cursor)
            audio=np.frombuffer(raw,dtype='<i2').astype(np.int32)
            audio[:take]+=np.frombuffer(self.effect,dtype='<i2')[self.effect_cursor:self.effect_cursor+take]
            np.clip(audio,-32768,32767,out=audio);raw=audio.astype('<i2').tobytes()
            self.effect_cursor+=take
            if self.effect_cursor==len(self.effect):self.effect=None
        self.generated.extend(raw)
        write_pcm(control,raw)
        self.sent_samples+=words*2
        return words

    def _expected_to(self,count):
        if count<self.confirmed_samples or count>self.sent_samples:
            raise RuntimeError('Unexpected mixed PCM counter reset or over-consumption')
        size=(count-self.confirmed_samples)*2
        if size>len(self.generated):raise RuntimeError('Missing submitted mixed PCM witness')
        self.expected_crc=zlib.crc32(self.generated[:size],self.expected_crc)
        del self.generated[:size];self.confirmed_samples=count
        self.expected_cursor=count if count<self.loop_end else self.loop_start+(count-self.loop_end)%(self.loop_end-self.loop_start)
        return self.expected_crc

    def snapshot(self,control):
        result=super().snapshot(control)
        if result is not None:
            result.update(ui_mixed=True,ui_effects_base=self.ui_event_count-len(self.ui_events),
                          ui_effects_total=self.ui_event_count,ui_effects=[dict(event) for event in self.ui_events])
            self.ui_events=[]
        return result
