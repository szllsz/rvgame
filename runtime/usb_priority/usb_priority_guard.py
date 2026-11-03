"""Exact G102/VADER4/single-TT compatibility trial; no USB host or PL reset.

The mouse companion is unused by this work. On a gamepad enumeration change,
temporarily release the mouse main interface, wait for decoded gamepad input,
then restore the main interface. All driver changes are undone on normal exit.
"""
import argparse
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


def latest_pad_status(lines, since_us):
    latest = None
    for line in lines.splitlines():
        row = json.loads(line)
        if int(row.get('__MONOTONIC_TIMESTAMP', 0)) < since_us:
            continue
        message = row.get('MESSAGE', '')
        if message.startswith('INPUT_SOURCE_STATUS '):
            latest = json.loads(message.partition(' ')[2])
        elif message.startswith('USB_INPUT '):
            state = json.loads(message.partition(' ')[2]).get('state', {})
            pad = state.get('gamepad', {})
            if pad:
                latest = dict(gamepad_connected=pad.get('connected'),
                              gamepad_source=pad.get('source'),
                              gamepad_stale=pad.get('stale'), gamepad_error=pad.get('error'))
    return bool(latest and latest.get('gamepad_connected') and
                latest.get('gamepad_source') == 'flydigi_usb' and
                latest.get('gamepad_stale') is False and not latest.get('gamepad_error'))


class Guard:
    def __init__(self, output):
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
        self.record('starting')

    def record(self, phase, **fields):
        event = dict(at=time.monotonic(), phase=phase, **fields)
        self.events.append(event)
        result = dict(phase=phase, events=self.events, mouse_identity=self.identity,
                      main_paused=self.main_changed, companion_disabled=self.companion_changed,
                      host_reset=False, MMIO_access=False)
        temporary = self.output.with_suffix('.next')
        temporary.write_text(json.dumps(result, indent=2) + '\n')
        temporary.replace(self.output)
        print('USB_PRIORITY', json.dumps(event), flush=True)

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
                self.since_us = 0
                break
            if self.mouse is None:
                return
        pad = self.pad()
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
            log = subprocess.check_output(['journalctl', '-b', '-u', 'rvgame-unified-boot.service',
                                           '-n', '400', '--no-pager', '-o', 'json'], text=True, timeout=10)
            if latest_pad_status(log, self.since_us):
                self.record('gamepad_decoded_input_confirmed', key=key, urb_delta=pad[2]-self.urb_start)
                self.resume_mouse('gamepad_reserved_first')
                return
        if now - self.wait_started >= 12:
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
    args = parser.parse_args()
    assert 0 <= args.seconds <= 1800
    args.output.parent.mkdir(parents=True, exist_ok=True)
    guard = Guard(args.output)
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
