"""AX7020 native USB gamepad -> rvgame AXI snapshot (run on PYNQ Linux).

Uses Linux input.h EVIOCGKEY/EVIOCGABS for complete current state, so idle
controllers stay connected and missed event queues cannot leave keys stuck.
Linux input UAPI: https://github.com/torvalds/linux/blob/master/include/uapi/linux/input.h
PYNQ Overlay/MMIO: https://github.com/Xilinx/PYNQ
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import time

BUTTON_CODES = (289, 290, 296, 297, 288, 291, 292, 293)
BUTTON_BITS = (0, 1, 2, 3, 8, 9, 10, 11)

def decode_snapshot(keys, x, y):
    mask = sum(1 << bit for code, bit in zip(BUTTON_CODES, BUTTON_BITS)
               if keys[code // 8] & (1 << (code % 8)))
    if y < 64: mask |= 1 << 4
    if y > 191: mask |= 1 << 5
    if x < 64: mask |= 1 << 6
    if x > 191: mask |= 1 << 7
    return mask

def find_pad():
    for event in sorted(Path('/sys/class/input').glob('event*')):
        device = event / 'device'
        try:
            if ((device / 'id/vendor').read_text().strip() == '0810' and
                (device / 'id/product').read_text().strip() == '0001'):
                node = Path('/dev/input') / event.name
                if not node.exists():
                    subprocess.run(['udevadm', 'trigger', '--subsystem-match=input'],
                                   check=True, timeout=2, capture_output=True)
                return node
        except (OSError, subprocess.SubprocessError):
            continue
    return None

def snapshot(fd):
    import fcntl
    keys = bytearray(96)
    fcntl.ioctl(fd, 0x80604518, keys, True)  # EVIOCGKEY(96)
    axes = []
    for axis in (0, 1):
        state = bytearray(24)
        fcntl.ioctl(fd, 0x80184540 + axis, state, True)  # EVIOCGABS
        value, minimum, maximum, _, _, _ = struct.unpack('=6i', state)
        if (minimum, maximum) != (0, 255):
            raise OSError('Unexpected gamepad axis range')
        axes.append(value)
    return decode_snapshot(keys, *axes)

def prepare_loads(control, program=None, assets=None):
    """Validate all files/capabilities before stopping the CPU."""
    loads = []
    if program:
        image = Path(program).read_bytes()
        if len(image) != control.read(0x38) or len(image) % 4:
            raise RuntimeError('Program must match the complete 96 KiB image')
        loads.append(('PROGRAM', 0, image))
    if assets:
        image = Path(assets).read_bytes()
        capacity, base = control.read(0x4c), control.read(0x50)
        if capacity != 0xc000 or base != 0x30000000 or not 0 < len(image) <= capacity:
            raise RuntimeError('Assets require the 48 KiB asset-capable overlay')
        # Clear unused tail too: replacing a larger image must not retain stale data.
        loads.append(('ASSETS', base, image.ljust(capacity, b'\0')))
    return loads

def load_images(control, loads):
    if not loads: return
    # PYNQ MMIO.read/write use this same uint32 mapping. Keep register
    # transactions scalar and ordered: 0x30 is a data port, not image RAM.
    registers = getattr(control, 'array', None)
    device = getattr(control, 'device', None)
    direct = (device is not None and device.has_capability('MEMORY_MAPPED')
              and registers is not None and registers.dtype.kind == 'u'
              and registers.dtype.itemsize == 4 and registers.size >= 13)
    control.write(0x28, 1)
    # Keep CPU held on any load failure; release only after every region passed.
    for name, base, image in loads:
        if direct:
            for index, (value,) in enumerate(struct.iter_unpack('<I', image)):
                address = base + index * 4
                registers[11] = address
                registers[12] = value
                if int(registers[12]) != value:
                    raise RuntimeError(f'{name} readback failed at {address:#x}')
        else:
            for offset in range(0, len(image), 4):
                value = int.from_bytes(image[offset:offset+4], 'little')
                control.write(0x2c, base + offset)
                control.write(0x30, value)
                if control.read(0x30) != value:
                    raise RuntimeError(f'{name} readback failed at {base+offset:#x}')
        print(name + '_LOAD_PASS', len(image), flush=True)
    control.write(0x28, 0)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--overlay', type=Path)
    parser.add_argument('--program', type=Path)
    parser.add_argument('--assets', type=Path, help='Load independent game assets before CPU restart')
    parser.add_argument('--seconds', type=float, default=0)
    parser.add_argument('--probe', action='store_true', help='Read USB only; no PL access')
    args = parser.parse_args()
    control = None
    if not args.probe:
        from pynq import Overlay, MMIO
        if args.overlay:
            overlay = Overlay(str(args.overlay))
            print('OVERLAY', sorted(overlay.ip_dict), flush=True)
        control = MMIO(0x43C00000, 0x1000)
        if control.read(0) != 0x5247414d:
            raise RuntimeError('rvgame identity mismatch')
        control.write(0x40, 0)
        control.write(0x0c, 0)  # Preserve physical-board fallback.
        load_images(control, prepare_loads(control, args.program, args.assets))
    run_input(control, args.seconds)

def run_input(control=None, seconds=0, on_poll=None):
    """Reuse an already initialized MMIO instance when launched at boot."""
    fd = None
    previous = None
    deadline = time.monotonic() + seconds if seconds else float('inf')
    retry = 0
    try:
        while time.monotonic() < deadline:
            connected = False
            mask = 0
            try:
                if fd is None and time.monotonic() >= retry:
                    node = find_pad()
                    if node:
                        fd = os.open(node, os.O_RDONLY | os.O_NONBLOCK)
                        print('USB_OPEN', str(node), flush=True)
                    retry = time.monotonic() + 1
                if fd is not None:
                    mask = snapshot(fd)
                    connected = True
            except OSError as error:
                print('USB_DISCONNECT', str(error), flush=True)
                if fd is not None: os.close(fd)
                fd = None
                retry = time.monotonic() + 1
            word = mask | (int(connected) << 16)
            if on_poll is not None:
                word = on_poll(word)
            if control: control.write(0x40, word)
            if word != previous:
                report = {'connected': connected, 'mask': hex(mask)}
                if control:
                    report.update(status=hex(control.read(0x44)),
                                  position=hex(control.read(0x48)))
                print(json.dumps(report), flush=True)
                previous = word
            time.sleep(.02)
    finally:
        if fd is not None: os.close(fd)
        if control: control.write(0x40, 0)

if __name__ == '__main__':
    main()
