"""Explicit --test-input only: raw controller replay inside the sole owner.

The observer writes leased JSON, never MMIO. No kernel devices are claimed.
This does not prove physical USB disconnection or radio behavior.
"""
import json
from pathlib import Path
import threading
import time
from input_actions import Actions
from input_backend import NEUTRAL


class ReplaySource:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.actions = Actions()
        self.diagnostic = {}
        self.sequence = 0
        self.value = None
        self.error = None
        self.stopped = threading.Event()
        # File access can exceed the PCM FIFO budget. This worker owns files
        # only; decoding and all input/music MMIO remain in the existing owner.
        self.reader = threading.Thread(target=self._read, name='rvgame-raw-replay', daemon=True)
        self.reader.start()

    def _read(self):
        while not self.stopped.is_set():
            try:
                self.value = json.loads((self.directory / 'raw_input.json').read_text())
            except FileNotFoundError:
                self.value = None
            except Exception as error:
                self.error = error
                return
            self.stopped.wait(.02)

    def poll(self, menu=False):
        connected, sample = False, NEUTRAL
        if self.error is not None:
            raise self.error
        value = self.value
        if value is not None:
            if time.monotonic() < value['until'] < time.monotonic()+1.1:
                connected = bool(value['connected'])
                sample = value['sample'] if connected else NEUTRAL
        if not connected:
            self.actions.reset()
        action = self.actions.decode(sample, menu)
        self.diagnostic = dict(action, source='explicit_raw_replay', connected=connected)
        return action['mask'] | 0x10000 if connected else 0

    def disconnect(self):
        self.stopped.set()
        self.actions.reset()
