"""Explicit --test-input only: raw controller replay inside the sole owner.

The observer writes leased JSON, never MMIO. No kernel devices are claimed.
This does not prove physical USB disconnection or radio behavior.
"""
import json
from collections import deque
from pathlib import Path
import threading
import time
from input_actions import Actions
from input_backend import NEUTRAL
from pointer_controls import PointerControls


class ReplaySource:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.actions = Actions()
        self.diagnostic = {}
        self.sequence = 0
        self.value = None
        self.error = None
        self.stopped = threading.Event()
        self.pointer = PointerControls()
        self.page = 0
        self.pointer_sequence = None
        self.pointer_events = deque()
        # File access can exceed the PCM FIFO budget. This worker owns files
        # only; decoding and all input/music MMIO remain in the existing owner.
        self.reader = threading.Thread(target=self._read, name='rvgame-raw-replay', daemon=True)
        self.reader.start()

    def _read(self):
        while not self.stopped.is_set():
            try:
                value = json.loads((self.directory / 'raw_input.json').read_text())
                if value.get('sequence') != self.pointer_sequence and 'pointer_events' in value:
                    self.pointer_sequence = value['sequence']
                    if len(self.pointer_events) > 256:
                        self.pointer_events.clear()
                        self.pointer_events.extend((('lost', 'touch'), ('lost', 'mouse')))
                    else:
                        self.pointer_events.extend(value['pointer_events'])
                self.value = value
            except FileNotFoundError:
                self.value = None
            except Exception as error:
                self.error = error
                return
            self.stopped.wait(.02)

    def set_page(self, page):
        self.page = page

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
        word = action['mask'] | 0x10000 if connected else 0
        if value is not None and connected and 'pointer_events' in value:
            events = []
            while self.pointer_events:
                events.append(self.pointer_events.popleft())
            word, action = self.pointer.poll(self.page, word, action, events)
        self.diagnostic = dict(action, source='explicit_raw_replay', connected=connected)
        return word

    def disconnect(self):
        self.stopped.set()
        self.actions.reset()
