"""Host-only contracts for the published menu and synchronized PS runtime."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import types
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'runtime/package'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rejected(action, error=AssertionError):
    try:
        action()
    except error:
        return
    raise AssertionError('Invalid input was accepted')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    out = parser.parse_args().output
    out.mkdir(parents=True, exist_ok=False)
    checks = []

    pointer = load('published_pointer', PACKAGE / 'pointer_controls.py')
    for i in range(6):
        x, y = (54 + i % 3 * 200) * 5 // 4, (100 + i // 3 * 164) * 5 // 4
        assert pointer.menu_card(x, y) == i
        assert pointer.menu_card(x + 164, y + 164) == i
        assert pointer.menu_card(x + 165, y + 165) is None
    assert pointer.menu_card(-1, -1) is None
    checks.append('All six public menu hit regions and exclusive edges')

    assets = load('published_assets', PACKAGE / 'boot_assets.py')
    fixture = out / 'assets'
    fixture.mkdir()
    payload = b'validated program bytes'
    (fixture / 'demo.bin').write_bytes(payload)
    record = {'files': {'demo.bin': hashlib.sha256(payload).hexdigest()}}
    (fixture / 'candidate_manifest.json').write_text(json.dumps(record))
    cache = assets.Assets(fixture)
    assert cache.read('demo.bin') == payload
    (fixture / 'demo.bin').write_bytes(b'changed bytes')
    assert cache.read('demo.bin') == payload
    rejected(lambda: assets.Assets(fixture).read('demo.bin'))
    rejected(lambda: cache.read('../outside.bin'))
    checks.append('Immutable cache, corrupted first use and directory escape')

    supervisor = load('published_supervisor', ROOT / 'runtime/supervisor.py')
    _, deferred = supervisor.verify_release()
    assert 'package/menu.bin' in deferred
    assert all(Path(n).suffix in {'.bin', '.pcm', '.bit', '.hwh', '.rgb332'} for n in deferred)
    _, full_deferred = supervisor.verify_release(full=True)
    assert full_deferred == []
    checks.append('Current package verifies in deferred and full modes')

    events = []

    class Control:
        def __init__(self):
            self.values = {0: 0x5247414d, 0x68: 0x01000000}

        def read(self, register):
            return self.values.get(register, 0)

        def write(self, register, value):
            self.values[register] = value
            events.append(('write', register, value))

    class Buffer(bytearray):
        physical_address = 0x1000000

        def flush(self):
            events.append(('flush',))

        def freebuffer(self):
            events.append(('free',))

    ddr = load('published_ddr', PACKAGE / 'pynq_ddr.py')
    control = Control()
    owner = ddr.DdrProgram(control, allocator=lambda shape, dtype: Buffer(shape[0]))

    def before_release():
        assert control.read(0x28) == 1 and ('flush',) in events
        events.append(('publish',))

    owner.load(b'program', before_release=before_release)
    assert events[-2:] == [('publish',), ('write', 0x28, 0)]
    checks.append('Boot publication follows flush and immediately precedes CPU release')

    def failed_publish():
        raise RuntimeError('fixture failure')

    rejected(lambda: owner.load(b'next', before_release=failed_publish), RuntimeError)
    assert control.read(0x28) == 1 and owner.buffer is not None
    checks.append('Failed boot publication keeps the CPU held and memory owned')

    calls = []

    def native_write(port, words, count):
        calls.append((port, list(words)[:count]))
        return 0

    with patch.object(ctypes, 'CDLL', return_value=types.SimpleNamespace(rvgame_pcm_write=native_write)):
        fifo = load('published_fifo', PACKAGE / 'pcm_fifo_write.py')
    mapping = types.SimpleNamespace(array=np.zeros(64, dtype='<u4'))
    fifo.write_pcm(mapping, struct.pack('<II', 0x12345678, 0xabcdef01))
    assert calls == [(mapping.array.ctypes.data + 0xb0, [0x12345678, 0xabcdef01])]
    for raw in (b'1', bytes(8196), bytearray(4)):
        rejected(lambda raw=raw: fifo.write_pcm(mapping, raw), ValueError)
    rejected(lambda: fifo.write_pcm(types.SimpleNamespace(array=np.zeros(2, dtype='<u4')), bytes(4)), ValueError)
    checks.append('PCM wrapper uses the FIFO register and rejects invalid bounds/mappings')

    batches = []
    fake_fifo = types.SimpleNamespace(write_pcm=lambda control, raw: batches.append(raw))
    with patch.dict(sys.modules, {'pcm_fifo_write': fake_fifo}):
        pcm = load('published_pcm', PACKAGE / 'scarlet_pcm.py')
    raw = struct.pack('<10H', *range(10))
    music = pcm.ScarletMusic(raw, 2, 10, hashlib.sha256(raw).hexdigest())
    values = {0x28: 0, 0xc0: 0x50434d31, 0xcc: 32000, 0xb4: (1 << 16) | 2041}
    audio = types.SimpleNamespace(read=lambda register: values[register])
    assert music.poll(audio) == 7
    assert batches[-1] == struct.pack('<14H', *list(range(10)), 2, 3, 4, 5)
    assert music.cursor == 6 and music.loops == 1
    values[0xb4] = (1 << 16) | 2044
    assert music.poll(audio) == 4
    assert batches[-1] == struct.pack('<8H', 6, 7, 8, 9, 2, 3, 4, 5)
    values[0x28] = 1
    count = len(batches)
    assert music.poll(audio) == 0 and len(batches) == count
    checks.append('PCM batching preserves intro, repeated loop, cursor and held-CPU behavior')

    result = dict(pass_=True, host_only=True, board_tested=False, checks=checks)
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PUBLIC_RUNTIME_HOST_CHECKS', len(checks), 'PASS')


if __name__ == '__main__':
    main()
