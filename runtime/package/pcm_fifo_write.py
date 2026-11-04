"""Bounded synchronous FIFO stores on the caller's existing MMIO mapping."""
import ctypes
from pathlib import Path

_library = ctypes.CDLL(str(Path(__file__).with_name('libpcm_fifo_write.so')))
_write = _library.rvgame_pcm_write
_write.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32), ctypes.c_size_t]
_write.restype = ctypes.c_int


def write_pcm(control, raw):
    if not isinstance(raw, bytes) or len(raw) % 4 or len(raw) > 8192:
        raise ValueError('Invalid PCM FIFO batch')
    if not raw:
        return
    array = control.array
    if (array.ndim != 1 or array.dtype.str != '<u4' or
            not array.flags.c_contiguous or not array.flags.writeable or
            array.nbytes < 0xb4 or array.ctypes.data % 4):
        raise ValueError('Invalid existing PCM control mapping')
    words = (ctypes.c_uint32 * (len(raw) // 4)).from_buffer_copy(raw)
    if _write(array.ctypes.data + 0xb0, words, len(words)) != 0:
        raise RuntimeError('Native PCM FIFO batch rejected')
