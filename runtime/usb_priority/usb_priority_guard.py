"""Exact G102/VADER4/single-TT input scheduling and bounded USB recovery.

The mouse companion is unused by this work. On a gamepad enumeration change,
temporarily release the mouse main interface, wait for decoded gamepad input,
then restore the main interface. Same-address input failures also reserve first.
An explicit option allows a host rebind only after stalled IRQs plus two failed
standard descriptor reads on the qualified topology. Never accesses PL/MMIO.
"""
import argparse
import ctypes as C
import fcntl
import hashlib
import json
import os
import signal
import subprocess
import time
from pathlib import Path

USB = Path('/sys/bus/usb/devices')
DRIVER = Path('/sys/bus/usb/drivers/usbhid')
MOUSE_SHA = '9516e2eb08355b209fba2a015ac349eb341d9fb6cf61fc38382a2e44ace9f611'
COMPANION_SHA = '2796c74a2e615814c81e46f6993426adf4c1d4ed3c20411a6deac4c96af2b2b0'


def text(path):
    try:
        return path.read_text().strip()
    except FileNotFoundError:
        return None


def mouse_identity(path):
    return tuple(text(path / key) for key in ('idVendor', 'idProduct', 'serial', 'devnum'))


def qualified_mouse(path):
    expected_serial = os.environ.get('RVGAME_G102_SERIAL', '')
    if not expected_serial:
        return None
    identity = mouse_identity(path)
    if identity[:3] != ('046d', 'c092', expected_serial):
        return None
    parent = path.resolve().parent
    if tuple(text(parent / key) for key in ('idVendor', 'idProduct', 'bDeviceProtocol')) != ('05e3', '0608', '01'):
        return None
    if text(path / 'speed') != '12':
        return None
    for number, digest in ((0, MOUSE_SHA), (1, COMPANION_SHA)):
        interface = path / (path.name + ':1.' + str(number))
        descriptors = list(interface.glob('0003:046D:C092.*/report_descriptor'))
        if len(descriptors) != 1 or hashlib.sha256(descriptors[0].read_bytes()).hexdigest() != digest:
            return None
        if not (interface / 'driver').exists() or (interface / 'driver').resolve() != DRIVER:
            return None
    return identity


def latest_pad_state(lines, since_us):
    latest = None
    for line in lines.splitlines():
        row = json.loads(line)
        if int(row.get('__MONOTONIC_TIMESTAMP', 0)) < since_us:
            continue
        message = row.get('MESSAGE', '')
        if message.startswith('INPUT_SOURCE_STATUS '):
            latest = json.loads(message.partition(' ')[2])
            latest['at_us'] = int(row.get('__MONOTONIC_TIMESTAMP', 0))
        elif message.startswith('USB_INPUT '):
            state = json.loads(message.partition(' ')[2]).get('state', {})
            pad = state.get('gamepad', {})
            if pad:
                latest = dict(gamepad_connected=pad.get('connected'),
                              gamepad_source=pad.get('source'),
                              gamepad_stale=pad.get('stale'), gamepad_error=pad.get('error'),
                              at_us=int(row.get('__MONOTONIC_TIMESTAMP', 0)))
    return latest


def pad_healthy(latest):
    return bool(latest and latest.get('gamepad_connected') and
                latest.get('gamepad_source') == 'flydigi_usb' and
                latest.get('gamepad_stale') is False and not latest.get('gamepad_error'))


def latest_pad_status(lines, since_us):
    return pad_healthy(latest_pad_state(lines, since_us))


def usb_irq():
    for line in Path('/proc/interrupts').read_text().splitlines():
        if 'e0002000.usb' not in line:
            continue
        values=[]
        for word in line.partition(':')[2].split():
            if not word.isdecimal():break
            values.append(int(word))
        return sum(values) if values else None
    return None


def descriptor_status(path):
    """GET_DESCRIPTOR only; no interface claim, output packet or reset."""
    bus, address = int(text(path/'busnum')), int(text(path/'devnum'))
    lib=C.CDLL('libusb-1.0.so.0');p=C.c_void_p
    specs={'init':([C.POINTER(p)],C.c_int),'exit':([p],None),
           'get_device_list':([p,C.POINTER(C.POINTER(p))],C.c_ssize_t),
           'free_device_list':([C.POINTER(p),C.c_int],None),
           'get_bus_number':([p],C.c_ubyte),'get_device_address':([p],C.c_ubyte),
           'open':([p,C.POINTER(p)],C.c_int),'close':([p],None),
           'control_transfer':([p,C.c_ubyte,C.c_ubyte,C.c_ushort,C.c_ushort,C.POINTER(C.c_ubyte),C.c_ushort,C.c_uint],C.c_int)}
    for name,(args,result) in specs.items():
        fn=getattr(lib,'libusb_'+name);fn.argtypes=args;fn.restype=result
    context=p();entries=C.POINTER(p)();handle=p()
    status=lib.libusb_init(C.byref(context))
    if status:return status
    try:
        count=lib.libusb_get_device_list(context,C.byref(entries))
        if count<0:return count
        for i in range(count):
            dev=entries[i]
            if (lib.libusb_get_bus_number(dev),lib.libusb_get_device_address(dev))!=(bus,address):continue
            status=lib.libusb_open(dev,C.byref(handle))
            if status:return status
            data=(C.c_ubyte*18)()
            status=lib.libusb_control_transfer(handle,0x80,6,0x0100,0,data,18,500)
            if status==18:
                expected=(int(text(path/'idVendor'),16),int(text(path/'idProduct'),16))
                if (data[8]|data[9]<<8,data[10]|data[11]<<8)!=expected:return -99
            return status
        return -4
    finally:
        if handle:lib.libusb_close(handle)
        if entries:lib.libusb_free_device_list(entries,1)
        lib.libusb_exit(context)


class Guard:
    def __init__(self, output, recover_stalled_host=False):
        self.output = output
        self.mouse = None
        self.identity = None
        self.companion_changed = False
        self.main_changed = False
        self.pad_key = None
        self.wait_started = None
        self.urb_start = 0
        self.since_us = 0
        self.last_status_check = 0
        self.stopped = False
        self.events = []
        self.recover_stalled_host = recover_stalled_host
        self.recoveries = []
        self.irq_value = None
        self.irq_changed = time.monotonic()
        self.next_health = 0
        self.bad_status_at = 0
        self.next_control_check = 0
        self.application_active = False
        self.record('starting')

    def record(self, phase, **fields):
        event = dict(at=time.monotonic(), phase=phase, **fields)
        self.events.append(event)
        result = dict(phase=phase, events=self.events, mouse_identity=self.identity,
                      main_paused=self.main_changed, companion_disabled=self.companion_changed,
                      host_reset=bool(self.recoveries), host_recovery_count=len(self.recoveries),
                      host_recovery_enabled=self.recover_stalled_host, MMIO_access=False)
        temporary = self.output.with_suffix('.next')
        temporary.write_text(json.dumps(result, indent=2) + '\n')
        temporary.replace(self.output)
        print('USB_PRIORITY', json.dumps(event), flush=True)

    def status(self):
        log = subprocess.check_output(['journalctl', '-b', '-u', 'rvgame-unified-boot.service',
                                       '-n', '120', '--no-pager', '-o', 'json'], text=True, timeout=5)
        return latest_pad_state(log, self.since_us)

    def validate_topology(self):
        if not self.same_mouse():raise RuntimeError('Mouse identity changed before recovery')
        hub=self.mouse.resolve().parent
        expected={'1d6b:0002','05e3:0608','046d:c092','045e:028e','1a86:e5e3'}
        seen=[]
        for path in USB.iterdir():
            vendor=text(path/'idVendor')
            if vendor is None:continue
            identity=vendor+':'+str(text(path/'idProduct'))
            if identity not in expected:raise RuntimeError('Unqualified USB device: '+identity)
            if identity not in ('1d6b:0002','05e3:0608') and path.resolve().parent!=hub:
                raise RuntimeError('Device outside qualified hub')
            seen.append(identity)
        if len(seen)!=5 or set(seen)!=expected:raise RuntimeError('Qualified topology incomplete or duplicated')
        if (text(hub/'idVendor'),text(hub/'idProduct'),text(hub/'bDeviceProtocol'))!=('05e3','0608','01'):
            raise RuntimeError('Hub identity changed')
        driver=Path('/sys/bus/platform/devices/ci_hdrc.0/driver')
        if not driver.exists() or driver.resolve()!=Path('/sys/bus/platform/drivers/ci_hdrc'):
            raise RuntimeError('Unexpected USB host driver')

    def recover_host(self):
        now=time.monotonic()
        if len([t for t in self.recoveries if now-t<300])>=2:
            self.record('recovery_rate_limited')
            self.next_control_check=now+300
            return
        self.validate_topology()
        self.recoveries.append(now)
        self.record('host_recovery_start')
        driver=Path('/sys/bus/platform/drivers/ci_hdrc')
        link=Path('/sys/bus/platform/devices/ci_hdrc.0/driver')
        # This method runs under the guard's existing exclusive recovery lock.
        # Host re-enumeration invalidates all previous interface identities.
        try:
            (driver/'unbind').write_text('ci_hdrc.0')
        finally:
            if not link.exists():(driver/'bind').write_text('ci_hdrc.0')
        assert link.exists() and link.resolve()==driver
        self.mouse=self.identity=None
        self.main_changed=self.companion_changed=False
        self.pad_key=None;self.wait_started=None
        self.since_us=int(now*1000000)
        self.irq_value=None;self.irq_changed=time.monotonic()
        self.record('host_rebound_reservation_pending')

    def health(self, pad):
        now=time.monotonic()
        if now<self.next_health:return
        self.next_health=now+1
        state=subprocess.run(['systemctl','is-active','rvgame-unified-boot.service'],
                             capture_output=True,text=True,timeout=2)
        was_active=self.application_active
        self.application_active=state.stdout.strip()=='active'
        if self.application_active and not was_active and self.wait_started is not None:
            # The bounded decoded-input window begins when the application can
            # actually read USB, rather than including its Overlay load time.
            self.wait_started=now
            self.urb_start=pad[2] if pad else 0
            self.record('application_ready_reservation_window')
        irq=usb_irq()
        if irq is None or irq!=self.irq_value:
            self.irq_changed=now;self.irq_value=irq
        if not self.application_active or pad is None:return
        latest=self.status()
        bad=latest and (latest.get('gamepad_stale') is True or bool(latest.get('gamepad_error')))
        if bad and latest['at_us']>self.bad_status_at and self.wait_started is None:
            self.bad_status_at=latest['at_us']
            self.pause_mouse();self.wait_started=now;self.urb_start=pad[2]
            self.since_us=latest['at_us']
            self.record('same_address_input_retry',key=pad[:2],diagnostic=latest)
        if (not self.recover_stalled_host or not bad or irq is None or
                now-self.irq_changed<5 or now<self.next_control_check):return
        self.next_control_check=now+10
        self.validate_topology()
        statuses=[descriptor_status(self.mouse.resolve().parent),descriptor_status(self.mouse)]
        self.record('stalled_irq_control_probe',statuses=statuses,irq=irq)
        # Silence or a sleeping controller alone is not a broken USB bus.
        if statuses==[-7,-7]:self.recover_host()

    def same_mouse(self):
        return self.mouse is not None and mouse_identity(self.mouse) == self.identity

    def set_binding(self, number, bound):
        if not self.same_mouse():
            raise RuntimeError('Mouse identity changed before driver operation')
        interface = self.mouse / (self.mouse.name + ':1.' + str(number))
        driver = interface / 'driver'
        if bound:
            if not driver.exists():
                (DRIVER / 'bind').write_text(interface.name)
            assert driver.resolve() == DRIVER
        else:
            if driver.exists():
                assert driver.resolve() == DRIVER
                (DRIVER / 'unbind').write_text(interface.name)
            assert not driver.exists()

    def pause_mouse(self):
        if not self.main_changed:
            # Mark before the write so cleanup also handles a partially completed operation.
            self.main_changed = True
            self.set_binding(0, False)
            self.record('mouse_paused')

    def resume_mouse(self, reason):
        if self.main_changed:
            self.set_binding(0, True)
            self.main_changed = False
            self.record('mouse_restored', reason=reason)
        self.wait_started = None

    def pad(self):
        if not self.same_mouse():
            return None
        for path in USB.iterdir():
            if (text(path / 'idVendor'), text(path / 'idProduct'), text(path / 'product')) != ('045e', '028e', 'Flydigi VADER4'):
                continue
            if path.resolve().parent != self.mouse.resolve().parent:
                continue
            address = text(path / 'devnum')
            urb = text(path / 'urbnum')
            if address is not None and urb is not None:
                return path.name, address, int(urb)
        return None

    def tick(self):
        now = time.monotonic()
        if self.mouse is not None and not self.same_mouse():
            self.record('mouse_removed')
            self.mouse = self.identity = None
            self.main_changed = self.companion_changed = False
            self.wait_started = None
        if self.mouse is None:
            for path in USB.iterdir():
                identity = qualified_mouse(path)
                if identity is None:
                    continue
                self.mouse, self.identity = path, identity
                self.companion_changed = True
                self.set_binding(1, False)
                self.record('companion_disabled')
                # Force an initial reservation order even if the mouse was already open.
                self.pad_key = ('initial',)
                if not self.recoveries:self.since_us = 0
                break
            if self.mouse is None:
                return
        pad = self.pad()
        self.health(pad)
        if self.mouse is None:return  # Recovery changed USB identities.
        key = pad[:2] if pad else None
        if key != self.pad_key:
            initial = self.pad_key == ('initial',)
            self.pad_key = key
            self.pause_mouse()
            self.wait_started = now
            self.urb_start = pad[2] if pad else 0
            # Reconnection must show a fresh decoded-status event, not an old connection.
            if not initial and key is None:
                self.since_us = int(now * 1000000)
            self.record('waiting_gamepad', key=key, urb_start=self.urb_start)
        if self.wait_started is None:
            return
        if pad is None:
            if now - self.wait_started >= 3:
                self.resume_mouse('no_gamepad_mouse_only_available')
            return
        if pad[2] - self.urb_start >= 80 and now - self.last_status_check >= .5:
            self.last_status_check = now
            if pad_healthy(self.status()):
                self.record('gamepad_decoded_input_confirmed', key=key, urb_delta=pad[2]-self.urb_start)
                self.resume_mouse('gamepad_reserved_first')
                return
        if now - self.wait_started >= 12:
            # During cold boot the app may not yet have opened the controller.
            if not self.application_active and now-self.wait_started<90:return
            self.record('gamepad_wait_failed', key=key)
            self.resume_mouse('bounded_wait_expired')

    def close(self):
        if self.same_mouse():
            if self.main_changed:
                self.set_binding(0, True)
                self.main_changed = False
            if self.companion_changed:
                self.set_binding(1, True)
                self.companion_changed = False
        else:
            self.main_changed = self.companion_changed = False
        self.record('stopped_restored')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=900)
    parser.add_argument('--recover-stalled-host', action='store_true')
    args = parser.parse_args()
    assert 0 <= args.seconds <= 1800
    args.output.parent.mkdir(parents=True, exist_ok=True)
    guard = Guard(args.output, args.recover_stalled_host)
    def stop(signum, frame):
        guard.stopped = True
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    with open('/run/lock/p008-usb-host-recovery.lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        deadline = time.monotonic() + args.seconds if args.seconds else float('inf')
        try:
            while not guard.stopped and time.monotonic() < deadline:
                guard.tick()
                time.sleep(.1)
        finally:
            guard.close()


if __name__ == '__main__':
    main()
