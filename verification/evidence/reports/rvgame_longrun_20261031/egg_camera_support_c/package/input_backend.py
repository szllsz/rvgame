"""Single-owner USB source selection for the existing run_input contract."""
import json
import os
import time
from pathlib import Path

from input_actions import Actions
from usb_xinput import UsbInput, devices

NEUTRAL = dict(buttons=0, lx=0, ly=0, rx=0, ry=0, lt=0, rt=0)


class Sources:
    def __init__(self, legacy, usb_factory=UsbInput, enumerate_usb=devices,
                 clock=time.monotonic, deadzone=6553):
        self.legacy = legacy
        self.usb_factory = usb_factory
        self.enumerate_usb = enumerate_usb
        self.clock = clock
        self.actions = Actions(deadzone)
        self.reader = None
        self.fd = None
        self.source = None
        self.retry = 0
        self.sample = None
        self.valid_at = 0
        self.frames = 0
        self.stale = False
        self.diagnostic = {}

    def disconnect(self):
        # Release in finally even if close itself fails; never keep held state.
        try:
            if self.fd is not None:
                os.close(self.fd)
            if self.reader is not None:
                self.reader.close()
        finally:
            self.fd = self.reader = self.source = self.sample = None
            self.actions.reset()
            self.stale = False

    def poll(self, menu=False):
        now = self.clock()
        try:
            if self.source is None and now >= self.retry:
                self.retry = now + .5
                node = self.legacy.find_pad()
                candidates = self.enumerate_usb()
                # Keep an acquired source until loss. At initial discovery an
                # old verified device has priority; never pick a random VADER4.
                if node:
                    self.fd = os.open(node, os.O_RDONLY | os.O_NONBLOCK)
                    self.source = 'legacy_0810'
                elif len(candidates) == 1:
                    self.reader = self.usb_factory()
                    self.reader.open(candidates[0])
                    self.source = 'flydigi_usb'
                elif len(candidates) > 1:
                    self.diagnostic = dict(error='Ambiguous VADER4 sources; no arbitrary claim')
            if self.source == 'legacy_0810':
                mask = self.legacy.snapshot(self.fd)
                self.diagnostic = dict(source=self.source, connected=True, mask=mask)
                return mask | 0x10000
            if self.source == 'flydigi_usb':
                # Drain a bounded queue. A1ms synchronous request was cancelled
                # before real VADER4 reports arrived; actual same-device A/B
                # captured0/298 at1ms and595/595 at10ms. Keep the existing20ms
                # owner cadence and permit one bounded10ms transfer wait.
                begin = self.clock()
                for _ in range(32):
                    received = self.reader.read(timeout_ms=10)
                    if received is None:
                        break
                    self.sample, self.valid_at = received, self.clock()
                    self.frames += 1
                    if self.clock()-begin >= .004:
                        break
                if self.sample is None:
                    return 0  # Claimed interface is not yet a valid controller.
                # Inactivity is not USB removal. Preserve connection heartbeats
                # and emit no connect/disconnect chime when a report goes stale.
                # Clear stale held actions defensively, including both axes.
                self.stale = self.clock()-self.valid_at > .25
                action = self.actions.decode(NEUTRAL if self.stale else self.sample, menu)
                self.diagnostic = dict(action, source=self.source, connected=True,
                                       stale=self.stale, packets=self.frames)
                return action['mask'] | 0x10000
        except (OSError, ValueError) as error:
            self.diagnostic = dict(error=str(error), connected=False)
            self.disconnect()
            self.retry = self.clock() + .5
        return 0


def run(legacy, control=None, seconds=0, on_poll=None, sources=None,
        clock=time.monotonic, sleep=time.sleep, settings=None):
    """All source words pass through the owner's on_poll, then one MMIO write.

    Callback return values are intentionally not masked: callers retain their
    established routing/stop semantics. The finally write always clears input.
    """
    source = sources or Sources(legacy, clock=clock)
    deadline = clock()+seconds if seconds else float('inf')
    previous = None
    extended = control is not None and control.read(0x04) == 0x10001
    if extended:
        if control.read(0x90) != 0x49500100:
            raise RuntimeError('Input snapshot version mismatch')
    percent = 20
    if settings is not None and Path(settings).exists():
        config = json.loads(Path(settings).read_text())
        percent = config['movement_deadzone_percent']
        if type(percent) is not int or percent not in (10, 20, 30, 50):
            raise ValueError('Unknown input deadzone preset')
    if hasattr(source, 'actions'):
        source.actions.deadzone = percent*32767//100
    last_physical = 0
    try:
        while clock() < deadline:
            begin = clock()
            # Taisei retains analog magnitude after its radial deadzone.
            # Brotato keeps its existing constant-speed direction contract.
            page = control.read(0x48) >> 16 if control is not None else 0
            touhou = 0x3d0 <= page <= 0x3d8 or page in (0x3e0, 0x3e1)
            word = source.poll(menu=page != 0x351 and not touhou)
            physical_word = word
            pressed = word & ~last_physical
            last_physical = word
            if page == 0x352 and bool(pressed & 1024) != bool(pressed & 2048):
                presets = (10, 20, 30, 50)
                index = presets.index(percent)
                percent = presets[max(0, min(3, index + (1 if pressed & 2048 else -1)))]
                if hasattr(source, 'actions'):
                    source.actions.deadzone = percent*32767//100
                if settings is not None:
                    path = Path(settings);path.parent.mkdir(parents=True, exist_ok=True)
                    temporary = path.with_suffix('.next')
                    temporary.write_text(json.dumps(dict(movement_deadzone_percent=percent))+'\n')
                    os.replace(temporary, path)
            if on_poll is not None:
                word = on_poll(word)
            if control is not None:
                action = source.diagnostic
                # Blocking a held confirm across a native transition must not
                # collapse real USB analog motion to legacy digital sectors.
                # Synthetic callbacks/replays still use the old clear path.
                filtered_faces = (touhou and action.get('source') == 'flydigi_usb'
                                  and not (word & ~physical_word)
                                  and not ((word ^ physical_word) & 0x100f0))
                if extended and (word == physical_word or filtered_faces) and word & 0x10000 and 'left' in action:
                    pack = lambda v: (v[0] & 65535) | ((v[1] & 65535) << 16)
                    # Replay diagnostics retain the D-pad vector; derive its
                    # four digital directions without a second deadzone.
                    dpad = action['dpad']
                    hat = (1 if dpad[1] < 0 else 0) | (2 if dpad[1] > 0 else 0) | (4 if dpad[0] < 0 else 0) | (8 if dpad[0] > 0 else 0)
                    snapshot_percent = (action['deadzone']*100+16383)//32767
                    aux = action['triggers'][0] | action['triggers'][1] << 8 | hat << 16 | action['stick_clicks'] << 20 | snapshot_percent << 22
                    # Brotato normalizes direction after its deadzone. Carry
                    # the original direction outside the circle, avoiding loss
                    # of angle from a tiny remapped magnitude near the edge.
                    left = action['left'] if touhou else action['raw_left'] if action['left'] != (0, 0) else (0, 0)
                    control.write(0x94, pack(left))
                    control.write(0x98, pack(action['right']))
                    control.write(0x9c, aux)
                    control.write(0xa0, word)
                else:
                    control.write(0x40, word)
            if word != previous:
                print('USB_INPUT', json.dumps(dict(word=word, state=source.diagnostic)), flush=True)
                previous = word
            sleep(max(0, .02-(clock()-begin)))
    finally:
        try:
            source.disconnect()
        finally:
            if control is not None:
                control.write(0x40, 0)
