"""Candidate input entrypoint; loading helpers and the 0810 ABI stay unchanged."""
import sys
from pathlib import Path
from legacy_gamepad import *
import legacy_gamepad
from input_backend import run
from usb_xinput import devices


def find_pad():
    node = legacy_gamepad.find_pad()
    if node:
        return node
    candidates = devices()
    return Path('/sys/bus/usb/devices') / candidates[0]['usb_path'] if len(candidates) == 1 else None


def run_input(control=None, seconds=0, on_poll=None):
    source = None
    if '--test-input' in sys.argv:
        from replay_input import ReplaySource
        source = ReplaySource(Path(sys.argv[0]).resolve().parent)
    directory = Path(sys.argv[0]).resolve().parent
    candidate = directory.parent.parent if directory.parent.name == 'runs' else directory.parent if directory.name == 'package' else directory
    settings = candidate / 'input_state/input_settings.json'
    return run(legacy_gamepad, control, seconds, on_poll, sources=source, settings=settings)


if __name__ == '__main__':
    legacy_gamepad.run_input = run_input
    legacy_gamepad.main()
