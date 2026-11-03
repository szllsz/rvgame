"""Compose input sources without creating another MMIO writer."""
import json
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
        self.previous_status = None

    def poll(self, menu=False):
        word = self.source.poll(menu)
        pad = dict(self.source.diagnostic)
        action = pad
        if 'left' not in action:
            action = Actions(self.actions.deadzone).decode(dict(buttons=0, lx=0, ly=0, rx=0, ry=0, lt=0, rt=0), menu)
            action['dpad'] = (int(bool(word & 128))-int(bool(word & 64)), int(bool(word & 32))-int(bool(word & 16)))
        page = self.control.read(0x48) >> 16
        result, action = self.pointer.poll(page, word, action, self.devices.drain())
        pointers = tuple(sorted(self.devices.connected))
        pad_connected = bool(word & 0x10000)
        connected = pad_connected or bool(pointers)
        self.diagnostic = dict(action, source='mixed_pointer', connected=connected,
                               gamepad=pad, pointer_devices=pointers)
        # A live mouse must not hide a failing pad while the combined CONNECTED
        # bit stays high. Log only transport status changes, never every packet.
        status = (pad_connected, pad.get('source'), pad.get('error'), pad.get('stale'), pointers)
        if status != self.previous_status:
            print('INPUT_SOURCE_STATUS', json.dumps(dict(gamepad_connected=pad_connected,
                gamepad_source=pad.get('source'), gamepad_error=pad.get('error'),
                gamepad_stale=pad.get('stale'), pointer_devices=pointers)), flush=True)
            self.previous_status = status
        return result | 0x10000 if connected else 0

    def disconnect(self):
        try:
            self.devices.close()
        finally:
            self.source.disconnect()
