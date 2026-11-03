"""Read-only Linux P008 HID reports and evdev relative mice; no MMIO/USB claims.

P008 descriptor/52-byte decoder follows the project's qualified 20261027 reader.
Workers perform filesystem and input I/O; the application thread only drains a
bounded queue. No EVIOCGRAB, USB reset, kernel-driver detach or uinput injection.
"""
from collections import deque
import errno
import hashlib
import os
from pathlib import Path
import re
import select
import struct
import threading
import time

REPORT_SHA = 'd394982f1108740d7c19e93c7e669e75b0e24a94c59fd45a67b54ce48eb26d16'
REPORT = re.compile(r'^report \(size (\d+)\) \(numbered\) =\s*([0-9a-f ]+)\s*$')
EVENT = struct.Struct('@llHHi')


def decode_touch(raw):
    if len(raw) != 52 or raw[0] != 1:
        raise ValueError('Unexpected P008 report')
    contacts = []
    for i in range(6):
        flags, contact, x, y, width = struct.unpack_from('<BBHHH', raw, 1+i*8)
        if flags & 1:
            if x > 4096 or y > 4096:
                raise ValueError('Touch coordinate outside descriptor range')
            contacts.append(dict(id=contact, x=x, y=y))
    if len({p['id'] for p in contacts}) != len(contacts):
        raise ValueError('Duplicate P008 contact ID')
    return contacts


def capabilities(path):
    return sum(int(part, 16) << (i*struct.calcsize('@L')*8)
               for i, part in enumerate(reversed(path.read_text().split())))


class PointerDevices:
    def __init__(self):
        self.events = deque()
        self.lock = threading.Lock()
        self.stopped = threading.Event()
        self.connected = set()
        self.statistics = dict(touch_reports=0, mouse_frames=0, overflows=0, errors=0)
        self.thread = threading.Thread(target=self._read, name='rvgame-pointer-read', daemon=True)
        self.thread.start()

    def emit(self, event):
        with self.lock:
            if len(self.events) >= 256:
                self.events.clear()
                self.events.extend((('lost', 'touch'), ('lost', 'mouse')))
                self.statistics['overflows'] += 1
                return
            self.events.append(event)

    def drain(self):
        with self.lock:
            events = list(self.events)
            self.events.clear()
        return events

    def _read(self):
        touch = debug = mouse = None
        touch_path = mouse_path = None
        buffer = ''
        next_scan = 0
        left = right = False
        dx = dy = wheel = 0
        dropped = False
        try:
            while not self.stopped.is_set():
                now = time.monotonic()
                if now >= next_scan:
                    next_scan = now+1
                    if debug is None:
                        matches = []
                        for h in Path('/sys/bus/hid/devices').glob('*:1A86:E5E3.*'):
                            try:
                                nodes = list((h/'input').glob('input*/event*'))
                                if len(nodes) == 1 and hashlib.sha256((h/'report_descriptor').read_bytes()).hexdigest() == REPORT_SHA:
                                    matches.append((h, nodes[0].name))
                            except OSError:
                                pass
                        if len(matches) == 1:
                            h, node = matches[0]
                            try:
                                debug = os.open('/sys/kernel/debug/hid/'+h.name+'/events', os.O_RDONLY | os.O_NONBLOCK)
                                touch = os.open('/dev/input/'+node, os.O_RDONLY | os.O_NONBLOCK)
                                touch_path = h
                                buffer = ''
                                self.connected.add('touch')
                                print('POINTER_OPEN touch', node, flush=True)
                            except OSError:
                                for fd in (debug, touch):
                                    if fd is not None:
                                        os.close(fd)
                                debug = touch = None
                    if mouse is None:
                        matches = []
                        for node in Path('/sys/class/input').glob('event*'):
                            try:
                                caps = node/'device/capabilities'
                                if capabilities(caps/'rel') & 3 == 3 and capabilities(caps/'key') & (1 << 272):
                                    matches.append(node)
                            except OSError:
                                pass
                        if len(matches) == 1:
                            try:
                                mouse_path = matches[0]
                                mouse = os.open('/dev/input/'+mouse_path.name, os.O_RDONLY | os.O_NONBLOCK)
                                left = right = False
                                dx = dy = wheel = 0
                                dropped = False
                                self.connected.add('mouse')
                                print('POINTER_OPEN mouse', mouse_path.name, flush=True)
                            except OSError:
                                mouse = None
                fds = [fd for fd in (debug, touch, mouse) if fd is not None]
                ready = select.select(fds, [], [], .02)[0] if fds else []
                if not fds:
                    self.stopped.wait(.05)
                for fd in ready:
                    kind = 'mouse' if fd == mouse else 'touch'
                    try:
                        raw = os.read(fd, EVENT.size*128 if fd != debug else 16384)
                        if not raw:
                            raise OSError(errno.ENODEV, 'Input stream ended')
                        if fd == debug:
                            buffer += raw.decode('ascii', errors='replace')
                            if len(buffer) > 65536:
                                raise ValueError('P008 debug buffer overflow')
                            lines = buffer.split('\n')
                            buffer = lines.pop()
                            for line in lines:
                                match = REPORT.fullmatch(line)
                                if match:
                                    payload = bytes.fromhex(match[2])
                                    if len(payload) != int(match[1]):
                                        self.emit(('lost', 'touch'))
                                        continue
                                    self.emit(('touch', decode_touch(payload)))
                                    self.statistics['touch_reports'] += 1
                        elif fd == mouse:
                            if len(raw) % EVENT.size:
                                raise ValueError('Partial evdev record')
                            for sec, usec, type_, code, value in EVENT.iter_unpack(raw):
                                if type_ == 0 and code == 3:
                                    dropped = True
                                    left = right = False
                                    dx = dy = wheel = 0
                                    self.emit(('lost', 'mouse'))
                                elif dropped:
                                    # Reopen after SYN_REPORT: held state is never
                                    # guessed after loss; a fresh press is required.
                                    if type_ == 0 and code == 0:
                                        raise OSError(errno.EOVERFLOW, 'evdev SYN_DROPPED')
                                elif type_ == 2:
                                    if code == 0: dx += value
                                    elif code == 1: dy += value
                                    elif code == 8: wheel += value
                                elif type_ == 1 and code == 272:
                                    left = bool(value)
                                elif type_ == 1 and code == 273:
                                    right = bool(value)
                                elif type_ == 0 and code == 0:
                                    self.emit(('mouse', (dx, dy, left, right, wheel)))
                                    self.statistics['mouse_frames'] += 1
                                    dx = dy = wheel = 0
                                    right = False
                    except (OSError, ValueError) as error:
                        if isinstance(error, BlockingIOError):
                            continue
                        self.statistics['errors'] += 1
                        self.emit(('lost', kind))
                        self.connected.discard(kind)
                        if kind == 'mouse':
                            os.close(mouse)
                            mouse = None
                        else:
                            for handle in (touch, debug):
                                if handle is not None:
                                    os.close(handle)
                            touch = debug = None
                        print('POINTER_REOPEN', kind, type(error).__name__, flush=True)
                if debug is not None and not touch_path.exists():
                    self.emit(('lost', 'touch'))
                    self.connected.discard('touch')
                    os.close(touch); os.close(debug)
                    touch = debug = None
                if mouse is not None and not mouse_path.exists():
                    self.emit(('lost', 'mouse'))
                    self.connected.discard('mouse')
                    os.close(mouse); mouse = None
        finally:
            for fd in (touch, debug, mouse):
                if fd is not None:
                    os.close(fd)
            self.connected.clear()
            self.emit(('lost', 'touch'))
            self.emit(('lost', 'mouse'))

    def close(self):
        self.stopped.set()
        self.thread.join(2)
        if self.thread.is_alive():
            raise RuntimeError('Pointer reader did not close')
