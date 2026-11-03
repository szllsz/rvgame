"""Shared native route/telemetry/audio contract, without hardware ownership."""
import json
import os


PLAIN_EXIT_PAGES={'coremark':0x362,'validation':0x362,'gfx':0x37f,'bitmap':0x38f,'alpha':0x39f}


def prepare_plain_native_retirement(control,application,timeout=35,clock=None):
    """Request exit through this owner, then prove a stable producer exit page.

    A sampled idle gap while a renderer is running never admits HOLD. These
    native binaries enter their exit loop only after finishing both bands;
    fatal paths issue the existing abort before publishing 0x3ff.
    """
    if clock is None:
        import time as clock
    assert application in PLAIN_EXIT_PAGES
    if control.read(0x28)==1:return dict(safe_to_hold=True,kind='already_held',final=None)
    control.write(0x40,0x10004)
    deadline=clock.monotonic()+timeout
    while True:
        page=control.read(0x48)>>16;status=control.read(0x74)
        if page in (PLAIN_EXIT_PAGES[application],0x3ff) and not status&2:
            return dict(safe_to_hold=True,kind='plain_exit' if page!=0x3ff else 'fatal_halt',
                        final=None,application=application,exit_page=page,frame_status=status)
        if clock.monotonic()>=deadline:raise TimeoutError('Plain native producer exit unknown; quarantine before HOLD')
        clock.sleep(.001)


def prepare_native_error_retirement(control,mailbox,refill=None,timeout=20,clock=None):
    """Prove a native producer stopped before a PS HOLD can block AXI.

    A run failure remains a failure. This never clears error counters, frees
    memory, reloads hardware, or writes HOLD; an unknown state raises so
    the caller retains the owner and its buffers with SSH still accessible.
    """
    if clock is None:
        import time as clock
    if control.read(0x28)==1:
        return dict(safe_to_hold=True,kind='already_held',final=None)
    deadline=clock.monotonic()+timeout;requested=False;sample=None
    verified=bool(mailbox is not None and mailbox.finished)
    while True:
        status=control.read(0x74);position=control.read(0x48)
        retirement=getattr(mailbox,'retirement',None)
        audio_pending=bool(verified and retirement and retirement['application']=='touhou' and not control.read(0xb4)&(1<<17))
        if not status&2 and (verified or position>>16==0x3ff) and not audio_pending:
            kind=('CRC_compact_final' if getattr(mailbox,'retirement_only',False) else 'CRC_final') if verified else 'fatal_halt'
            return dict(safe_to_hold=True,kind=kind,
                        final=sample,frame_status=status)
        if clock.monotonic()>=deadline:
            raise TimeoutError('Native error retirement unknown; quarantine before HOLD')
        if position>>16==0x3ff:
            if refill is not None:refill()
            clock.sleep(.001)
            continue
        if mailbox is None:
            raise RuntimeError('No current-run mailbox; quarantine before HOLD')
        if not requested and not verified:
            control.write(0x40,0)
            control.write(0x0c,mailbox.request_final(position));requested=True
        if refill is not None:refill()
        if not verified:
            value=mailbox.accept(position,dict(monotonic=clock.monotonic(),position=position,
                scan=control.read(0x10),copies=control.read(0x7c),underflows=control.read(0x78),
                ddr_status=control.read(0x60),display_status=status))
            control.write(0x0c,0x102|mailbox.phase|(4 if getattr(mailbox,'compact_final',False) else 0))
            if value is not None and value.get('mailbox_final'):
                sample=value;verified=mailbox.finished
        clock.sleep(.001)


def game_route(choice):
    if choice not in (0, 1):
        raise ValueError('Unknown game selection')
    return ('game' if choice == 0 else 'touhou', None)


def main_menu_shortcut(active, page, word, blocked=0):
    """SELECT is global; B leaves root pages without stealing nested cancel."""
    if active == 'menu' or not word & 0x10000:
        return False
    keys = word & ~blocked & 4095
    if keys & 4:
        return True
    return bool(keys & 1 and active in ('coremark', 'validation', 'gfx', 'bitmap', 'alpha'))


def valid_touhou_sample(sample):
    meta, data = sample['structure_telemetry'], sample['telemetry']
    return (len(meta) == 16 and len(data) == 64 and meta[0] == 0x54475031
            and meta[15] == (meta[1] ^ 0xffffffff) and meta[2] <= 8
            and meta[6] <= 128 and meta[7] <= 2048 and not meta[12]
            and not (meta[13] & 8) and not data[16] and data[22] == data[53]
            and data[52] in (2026, 2027, 2028)
            and data[31] & ~0x10f == 0x41550000 and (data[31] & 15) <= 8
            and data[54] <= meta[4] and data[55] == 0 and data[56] == 3
            and data[59] in (0, 1) and data[60] in (0, 1) and data[61] <= 3
            and 1 <= data[62] <= 8
            and data[63] <= 600)


class NativeAudio:
    def __init__(self, settings):
        self.settings = settings
        self.value = 5
        if settings.exists():
            value = json.loads(settings.read_text())
            self.update(value['volume'], value['muted'], save=False)

    def update(self, volume, muted, save=True):
        if type(volume) is not int or not 0 <= volume <= 8 or type(muted) is not int or muted not in (0, 1):
            raise ValueError('Invalid native audio preferences')
        self.value = volume | muted << 8
        if save:
            self.settings.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.settings.with_suffix('.next')
            temporary.write_text(json.dumps(dict(volume=volume, muted=muted))+'\n')
            os.replace(temporary, self.settings)

    def capture(self, control, active, final_sample):
        if active in ('game', 'menu', 'touhou'):
            # APU mute also contains transient pause/exit suppression. Capture
            # the sealed logical preference, never that physical gate.
            if final_sample is None:
                raise ValueError('Native settings require final telemetry')
            if final_sample.get('retirement_only'):
                retirement=final_sample['retirement']
                if retirement['application']!=active:
                    raise ValueError('Retirement application mismatch')
                value=retirement['audio']
                self.update(value&15,(value>>8)&1)
                return
            value = final_sample['telemetry'][{'game':61,'menu':4,'touhou':31}[active]]
            if value & ~0x10f != 0x41550000:
                raise ValueError('Missing or invalid logical native audio preference')
            self.update(value & 15, (value >> 8) & 1)

    def publish_boot(self, control, menu_selection=0):
        # Called only while the CPU is held, before DdrProgram.load releases it.
        if control.read(0x28) != 1:
            raise RuntimeError('Boot preferences require held CPU')
        if type(menu_selection) is not int or not 0 <= menu_selection < 6:
            raise ValueError('Invalid native menu selection')
        control.write(0x94, self.value | menu_selection << 9)
        control.write(0x98, 0x41554430)
        # 63 fits the accepted six-bit field, outside physical presets10/20/30/50.
        # Bits28..31 are rejected by the real PS staging port.
        control.write(0x9c, 63 << 22)
        control.write(0xa0, 0x10000)


class NativeTransition:
    """Status banner in the existing owned framebuffer, only after RV freeze."""
    def __init__(self, control, frame, banners):
        self.control,self.frame,self.banners=control,frame,banners
        self.last=None;self.updates=0

    def show(self, target, stage, completed=0, total=1):
        key='transition_%s_%s.rgb332'%(target,stage)
        if key not in self.banners:return True
        step=min(16,max(0,completed*16//max(1,total))) if stage==1 else 16
        signature=(key,step)
        if self.last==signature:return True
        # A final packet page freezes RV drawing. Observe HP1 idle before
        # touching our retained allocation; no second GPU or input writer.
        final_page=self.control.read(0x48)>>16==0x35f
        held=self.control.read(0x28)==1
        if (not final_page and not held) or self.control.read(0x74)&2:return False
        assert self.control.read(0x70)==1
        banner=bytearray(self.banners[key])
        if len(banner)==800*80:
            stride,height,top,left,bar_width,bar_top,bar_height=800,80,520,32,736,52,8
        else:
            assert len(banner)==640*64
            stride,height,top,left,bar_width,bar_top,bar_height=640,64,416,26,588,40,8
        width=bar_width*step//16
        for row in range(bar_top,bar_top+bar_height):
            banner[row*stride+left:row*stride+left+width]=bytes([0x3f])*width
        view=memoryview(self.frame).cast('B')
        for base in (0,0x80000):view[base+top*stride:base+(top+height)*stride]=banner
        self.frame.flush();self.last=signature;self.updates+=1
        return True

    def reset(self):self.last=None
