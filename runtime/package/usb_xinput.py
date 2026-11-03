"""VADER4 standard Xbox 360 input via existing libusb; no kernel changes.

Only claims an unbound interface 0. Never detaches a driver, resets USB, writes
firmware, sends output packets, or accesses PL registers. Extra buttons ignored.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import time


def decode_packet(data):
    if len(data) < 20 or data[:2] != b'\x00\x14':
        return None
    buttons, lt, rt, lx, ly, rx, ry = struct.unpack_from('<HBBhhhh', data, 2)
    return dict(buttons=buttons, lt=lt, rt=rt, lx=lx, ly=ly, rx=rx, ry=ry)


def devices():
    found = []
    for node in Path('/sys/bus/usb/devices').iterdir():
        try:
            if (node / 'idVendor').read_text().strip() != '045e' or (node / 'idProduct').read_text().strip() != '028e':
                continue
            if (node / 'product').read_text().strip() != 'Flydigi VADER4':
                continue
            interface = node / (node.name + ':1.0')
            if (interface / 'driver').exists():
                continue
            if [(interface / name).read_text().strip() for name in
                    ('bInterfaceClass', 'bInterfaceSubClass', 'bInterfaceProtocol')] != ['ff', '5d', '01']:
                continue
            found.append({'usb_path': node.name, 'bus': int((node / 'busnum').read_text()),
                          'address': int((node / 'devnum').read_text()),
                          'bcdDevice': (node / 'bcdDevice').read_text().strip()})
        except OSError:
            continue
    return found


class UsbInput:
    def __init__(self):
        self.lib = C.CDLL('libusb-1.0.so.0')
        p, i = C.c_void_p, C.c_int
        prototypes = {
            'init': ([C.POINTER(p)], i),
            'exit': ([p], None),
            'get_device_list': ([p, C.POINTER(C.POINTER(p))], C.c_ssize_t),
            'free_device_list': ([C.POINTER(p), i], None),
            'get_bus_number': ([p], C.c_ubyte),
            'get_device_address': ([p], C.c_ubyte),
            'open': ([p, C.POINTER(p)], i),
            'close': ([p], None),
            'kernel_driver_active': ([p, i], i),
            'claim_interface': ([p, i], i),
            'release_interface': ([p, i], i),
            'interrupt_transfer': ([p, C.c_ubyte, C.POINTER(C.c_ubyte), i, C.POINTER(i), C.c_uint], i),
        }
        for name, (args, result) in prototypes.items():
            fn = getattr(self.lib, 'libusb_' + name)
            fn.argtypes, fn.restype = args, result
        self.context = p(); self.handle = None; self.selected = None
        status = self.lib.libusb_init(C.byref(self.context))
        if status:
            raise OSError('libusb init failed', status)

    def open(self, device):
        self.disconnect()
        entries = C.POINTER(C.c_void_p)()
        count = self.lib.libusb_get_device_list(self.context, C.byref(entries))
        if count < 0:
            raise OSError('USB enumeration failed', count)
        handle = C.c_void_p()
        try:
            for n in range(count):
                ptr = entries[n]
                if self.lib.libusb_get_bus_number(ptr) == device['bus'] and self.lib.libusb_get_device_address(ptr) == device['address']:
                    status = self.lib.libusb_open(ptr, C.byref(handle))
                    if status:
                        raise OSError('USB open failed', status)
                    break
        finally:
            self.lib.libusb_free_device_list(entries, 1)
        if not handle:
            raise OSError('Device disappeared before open')
        active = self.lib.libusb_kernel_driver_active(handle, 0)
        if active != 0:
            self.lib.libusb_close(handle)
            raise OSError('Refusing to detach an existing driver', active)
        status = self.lib.libusb_claim_interface(handle, 0)
        if status:
            self.lib.libusb_close(handle)
            raise OSError('USB interface claim failed', status)
        self.handle, self.selected = handle, device

    def read(self, timeout_ms=100):
        if not self.handle:
            raise OSError('No input interface open')
        buffer = (C.c_ubyte * 64)(); length = C.c_int()
        status = self.lib.libusb_interrupt_transfer(self.handle, 0x81, buffer, 64, C.byref(length), timeout_ms)
        if status == -7:
            return None
        if status:
            raise OSError('USB input transfer failed', status)
        return decode_packet(bytes(buffer[:length.value]))

    def disconnect(self):
        if self.handle:
            self.lib.libusb_release_interface(self.handle, 0)
            self.lib.libusb_close(self.handle)
        self.handle = None; self.selected = None

    def close(self):
        self.disconnect()
        if self.context:
            self.lib.libusb_exit(self.context)
            self.context = None


def stream(seconds):
    reader = UsbInput()
    end = time.monotonic() + seconds if seconds else float('inf')
    retry = 0; last = None; last_emit = 0; sample = None; last_valid = 0
    try:
        while time.monotonic() < end:
            now = time.monotonic()
            state = {'connected': False, 'count': 0}
            try:
                if not reader.handle and now >= retry:
                    candidates = devices(); retry = now + 0.5
                    if len(candidates) == 1:
                        reader.open(candidates[0]); sample = None; last_valid = 0
                    elif len(candidates) > 1:
                        state['error'] = 'Multiple VADER4 inputs: transport priority must be bound first'
                if reader.handle:
                    received = reader.read()
                    now = time.monotonic()
                    if received is not None:
                        sample = received; last_valid = now
                    if sample is not None:
                        fresh = now - last_valid <= 0.25
                        # Silence does not prove unplug, but stale held keys must not stick.
                        state = dict(sample if fresh else dict(buttons=0, lx=0, ly=0, rx=0, ry=0, lt=0, rt=0),
                                     connected=True, count=1, fresh=fresh, device=reader.selected)
            except OSError as error:
                state = {'connected': False, 'count': 0, 'error': str(error)}
                reader.disconnect(); sample = None; retry = now + 0.5
            if (state != last and (now - last_emit >= 0.02 or state['connected'] != (last or {}).get('connected'))) or now - last_emit >= 0.2:
                print(json.dumps(dict(state, monotonic_ns=time.monotonic_ns())), flush=True)
                last = state; last_emit = now
            if not reader.handle:
                time.sleep(0.02)
    finally:
        reader.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--seconds', type=float, default=900)
    args = parser.parse_args()
    try:
        stream(args.seconds)
    except KeyboardInterrupt:
        pass
