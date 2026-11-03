"""Buffered PCM producer for the existing single-owner PS polling loop.
This module never acquires hardware, starts threads, loads a bit, or owns input.
The caller must invoke begin only after a known application reset/load.
"""
import hashlib
import struct
import sys
import zlib


class ScarletMusic:
    FIFO_WORDS = 2048
    TARGET_WORDS = 2048
    # The accepted input owner polls every20ms (640 samples). Refill up to96ms
    # of audio in one bounded call;128 words/poll would supply only8ms.
    BURST_WORDS = 2048

    def __init__(self, data, loop_start, loop_end, expected_sha256):
        if sys.byteorder != 'little':
            raise RuntimeError('PCM producer requires little-endian host')
        if not isinstance(data, bytes) or len(data) % 2 or len(data) > 16 * 1024 * 1024:
            raise ValueError('Invalid immutable PCM data')
        if hashlib.sha256(data).hexdigest() != expected_sha256:
            raise ValueError('PCM source identity mismatch')
        if type(loop_start) is not int or type(loop_end) is not int or not 0 <= loop_start < loop_end <= len(data) // 2:
            raise ValueError('PCM loop outside source')
        self.data = data
        self.samples = memoryview(data).cast('H')
        self.loop_start, self.loop_end = loop_start, loop_end
        self.begin(None)

    def begin(self, segment):
        self.segment = segment
        self.cursor = self.sent_samples = self.loops = 0
        self.checked_signature = False
        self.confirmed_samples = self.expected_cursor = self.expected_crc = 0
        self.snapshots = 0

    def _next(self):
        value = int(self.samples[self.cursor])
        self.cursor += 1
        if self.cursor == self.loop_end:
            self.cursor = self.loop_start
            self.loops += 1
        return value

    def poll(self, control):
        if control.read(0x28) != 0:
            return 0
        if not self.checked_signature:
            if control.read(0xc0) != 0x50434d31 or control.read(0xcc) != 32000:
                raise RuntimeError('PCM hardware signature/rate mismatch')
            self.checked_signature = True
        status = control.read(0xb4)
        if not status & (1 << 16) or status & (1 << 17):
            return 0
        queued = status & 4095
        if queued > self.FIFO_WORDS:
            raise RuntimeError('PCM FIFO count outside accepted hardware capacity')
        words = min(self.BURST_WORDS, max(0, self.TARGET_WORDS - queued))
        # No other host writer is allowed. Between this count and each write,
        # the hardware can only consume space or stop and discard safely.
        for _ in range(words):
            low, high = self._next(), self._next()
            control.write(0xb0, low | high << 16)
        self.sent_samples += words * 2
        return words

    def _expected_to(self, count):
        if count < self.confirmed_samples or count > self.sent_samples:
            raise RuntimeError('Unexpected PCM counter reset or consumption beyond submitted data')
        remaining = count - self.confirmed_samples
        while remaining:
            take = min(remaining, self.loop_end - self.expected_cursor)
            chunk = self.data[self.expected_cursor * 2:(self.expected_cursor + take) * 2]
            self.expected_crc = zlib.crc32(chunk, self.expected_crc)
            self.expected_cursor += take
            if self.expected_cursor == self.loop_end:
                self.expected_cursor = self.loop_start
            remaining -= take
        self.confirmed_samples = count
        return self.expected_crc

    def snapshot(self, control):
        before = control.read(0xb4)
        if not before & (1 << 16) or before & (1 << 20):
            return None
        count = control.read(0xb8)
        actual_crc = control.read(0xc4)
        again = control.read(0xb8)
        after = control.read(0xb4)
        if count != again or after & (1 << 20) or not after & (1 << 16):
            return None
        expected_crc = self._expected_to(count)
        if actual_crc != expected_crc:
            raise RuntimeError('PCM digital stream CRC mismatch')
        underflows = control.read(0xbc)
        serial_faults, quiet_faults = control.read(0xd8), control.read(0xdc)
        if serial_faults or quiet_faults:
            raise RuntimeError('I2S serialized data or silence witness failed')
        self.snapshots += 1
        return dict(segment=self.segment, consumed_samples=count, crc32=actual_crc,
                    source_cursor=self.expected_cursor, submitted_samples=self.sent_samples,
                    submitted_loops=self.loops, fifo_words=after & 4095, underflows=underflows,
                    clock_ticks=control.read(0xc8), paused=bool(after & (1 << 17)),
                    serial_frames=control.read(0xd0), serial_last_pair=control.read(0xd4),
                    serial_faults=serial_faults, serial_quiet_faults=quiet_faults,
                    sample_rate=32000, digital_crc_verified=True)
