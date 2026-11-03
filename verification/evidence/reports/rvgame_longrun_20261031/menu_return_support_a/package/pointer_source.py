"""Compose input sources without creating another MMIO writer."""
from input_actions import Actions
from pointer_controls import PointerControls
from pointer_devices import PointerDevices


class PointerSource:
    def __init__(self, source, control):
        self.source = source
        self.control = control
        self.actions = source.actions
        self.pointer = PointerControls()
        self.devices = PointerDevices()
        self.diagnostic = {}

    def poll(self, menu=False):
        word = self.source.poll(menu)
        action = self.source.diagnostic
        if 'left' not in action:
            action = Actions(self.actions.deadzone).decode(dict(buttons=0, lx=0, ly=0, rx=0, ry=0, lt=0, rt=0), menu)
            action['dpad'] = (int(bool(word & 128))-int(bool(word & 64)), int(bool(word & 32))-int(bool(word & 16)))
        page = self.control.read(0x48) >> 16
        result, action = self.pointer.poll(page, word, action, self.devices.drain())
        connected = bool(word & 0x10000) or bool(self.devices.connected)
        self.diagnostic = dict(action, source='mixed_pointer', connected=connected)
        return result | 0x10000 if connected else 0

    def disconnect(self):
        try:
            self.devices.close()
        finally:
            self.source.disconnect()
