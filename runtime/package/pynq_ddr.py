"""Keep a contiguous DDR program alive while the PL RV32I CPU executes it.

PYNQ buffer semantics: https://github.com/Xilinx/PYNQ/blob/master/pynq/buffer.py
Use one persistent owner; never exit its process while the CPU uses the buffer.
"""
import time


def configure_hp0_32(owner, hp_control=None):
    """AFI width is PS runtime state; downloading PL alone does not set it.

    Match ps7_init.c mask writes at F8008000/F8008014, preserving other bits.
    """
    owner.hold()
    if hp_control is None:
        from pynq import MMIO
        hp_control = MMIO(0xf8008000, 0x20)
    previous = {offset: hp_control.read(offset) for offset in (0, 0x14)}
    for offset, value in previous.items():
        hp_control.write(offset, value | 1)
        if hp_control.read(offset) != value | 1:
            raise RuntimeError('PS HP0 32-bit configuration did not read back')
    return hp_control, previous


class DdrProgram:
    def __init__(self, control, allocator=None, timeout=2):
        if allocator is None:
            from pynq import allocate
            allocator = allocate
        self.control = control
        self.allocator = allocator
        self.timeout = timeout
        self.buffer = None
        if control.read(0) != 0x5247414d or control.read(0x68) != 0x01000000:
            raise RuntimeError('Load the DDR-capable rvgame overlay first')

    def hold(self):
        self.control.write(0x28, 1)
        deadline = time.monotonic() + self.timeout
        while self.control.read(0x60) & 2:
            if time.monotonic() >= deadline:
                raise TimeoutError('DDR burst did not drain; keep its buffer allocated')
            time.sleep(.001)
        if self.control.read(0x28) != 1:
            raise RuntimeError('CPU hold failed')

    def close(self):
        # On a drain timeout, retain ownership: freeing active memory is unsafe.
        self.hold()
        self.control.write(0x58, 0)
        if self.control.read(0x58) != 0:
            raise RuntimeError('DDR disable failed; keep its buffer allocated')
        if self.buffer is not None:
            self.buffer.freebuffer()
            self.buffer = None

    def load(self, image, before_release=None):
        image = bytes(image)
        size = (len(image) + 63) & ~63
        if not image or size > self.control.read(0x68):
            raise ValueError('DDR program must fit the 16 MiB window')
        self.close()
        self.buffer = self.allocator(shape=(size,), dtype='u1')
        base = int(self.buffer.physical_address)
        if base & 63 or base < 0 or base + size > 0x40000000:
            self.close()
            raise RuntimeError('Allocated buffer is outside aligned AX7020 DDR')
        view = memoryview(self.buffer).cast('B')
        view[:] = image.ljust(size, b'\0')
        self.buffer.flush()
        for register, value in ((0x54, base), (0x64, size), (0x58, 1)):
            self.control.write(register, value)
            if self.control.read(register) != value:
                raise RuntimeError('DDR configuration readback failed; CPU remains held')
        self.control.write(0x5c, 1)
        # Boot at BRAM address zero, then jump to external address 0x80000000.
        for address, word in ((0, 0x800000b7), (4, 0x00008067)):
            self.control.write(0x2c, address)
            self.control.write(0x30, word)
            if self.control.read(0x30) != word:
                raise RuntimeError('DDR trampoline readback failed; CPU remains held')
        if self.control.read(0x60):
            raise RuntimeError('DDR status is not clear before CPU release')
        # Atomic boot input expires after one second. Publish only after
        # allocation/copy/flush, immediately before the held CPU is released.
        if before_release is not None:
            before_release()
        self.control.write(0x28, 0)
        if self.control.read(0x28) != 0:
            raise RuntimeError('DDR CPU release failed')
        return {'physical_base': base, 'allocated_bytes': size, 'image_bytes': len(image)}
