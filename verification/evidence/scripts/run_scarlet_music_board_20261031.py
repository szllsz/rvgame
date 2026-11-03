"""Use the established single-owner N guard for new digital music qualification.
Default prepares only a plan. --execute is explicit automatic board work.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import zlib

from scarlet_resources_20261031 import ROOT, BASE, sha


def load(path):
    spec = importlib.util.spec_from_file_location('scarlet_received_native', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def crc_prefix(data, start, end, count):
    assert 0 <= count <= 32000 * 7200
    if count <= end:return zlib.crc32(data[:count * 2])
    crc = zlib.crc32(data[:end * 2])
    whole, remaining = divmod(count - end, end - start)
    loop = data[start * 2:end * 2]
    for _ in range(whole):crc = zlib.crc32(loop, crc)
    return zlib.crc32(data[start * 2:(start + remaining) * 2], crc)


def validate_music_runs(folder, candidate):
    from test_brotato_fidelity_board_guarded_20261028 import mailbox_summary
    results = []
    runs = sorted((folder / 'runs').iterdir())
    assert len(runs) == 1, 'Music probe requires exactly one persistent owner'
    for run in runs:
        exit_ = json.loads((run / 'child_exit.json').read_text())
        cleanup = json.loads((run / 'cleanup.json').read_text())
        assert exit_['exit_code'] == 0 and cleanup['pass'] and cleanup['pid'] == exit_['child_pid']
        assert all(cleanup[k] for k in ('program_freed', 'display_freed', 'hp_restored'))
        assert not (run / 'owner_error.json').exists()
        assert sha(run / 'candidate_manifest.json') == sha(candidate / 'package/candidate_manifest.json')
        manifest = json.loads((run / 'candidate_manifest.json').read_text())
        for name in ('native_session.py', 'bt_mailbox.py', 'scarlet_pcm.py', 'pynq_brotato_session.py'):
            assert sha(run / name) == manifest['files'][name]
        native = load(run / 'native_session.py')
        samples = [json.loads(line) for line in (run / 'samples.jsonl').read_text().splitlines()]
        final = json.loads((run / 'segment_000_final.json').read_text())
        assert len(samples) >= 3 and all(s['session_segment'] == 0 and s['session_app'] == 'touhou' for s in samples)
        transport = mailbox_summary(samples, final, manifest)
        assert transport['pass'] and samples[-1] == final
        assert sha(run / 'touhou_build.json') == manifest['files']['touhou_build.json']
        stack = json.loads((run / 'touhou_build.json').read_text())['static_stack_available']
        assert all(s['coherent'] and s['structure_coherent'] and native.valid_touhou_sample(s) and s['underflows'] == 0 and
                   stack - s['structure_telemetry'][11] >= 256 for s in samples)
        spec = manifest['scarlet_music']
        source = candidate / 'package' / spec['file']
        assert sha(source) == spec['sha256'] == manifest['files'][spec['file']]
        data = source.read_bytes()
        logs = [json.loads(line) for line in (run / 'music_samples.jsonl').read_text().splitlines()]
        assert len(logs) >= 10 and any(row['final'] for row in logs)
        previous = 0
        for row in logs:
            count = row['consumed_samples']
            assert previous <= count <= row['submitted_samples'] and row['segment'] == 0
            assert row['sample_rate'] == 32000 and row['fifo_words'] <= 2048
            assert row['underflows'] == row['serial_faults'] == row['serial_quiet_faults'] == 0
            assert row['crc32'] == crc_prefix(data, spec['loop_start'], spec['loop_end'], count)
            cursor = count if count < spec['loop_end'] else spec['loop_start'] + (count - spec['loop_end']) % (spec['loop_end'] - spec['loop_start'])
            assert row['source_cursor'] == cursor and row['clock_ticks'] >= count
            previous = count
        sealed = json.loads((run / 'music_segment_000_final.json').read_text())
        assert sealed['final'] and sealed['paused'] and sealed['consumed_samples'] > 0 and sealed in logs
        results.append(dict(run=run.name, samples=len(samples), music_crc_snapshots=len(logs),
                            consumed_samples=sealed['consumed_samples'], crc32=sealed['crc32'],
                            final_CRC_verified=True, complete_original_mailbox_CRC=True,
                            cleanup=cleanup, scope='digital music only; no strict60/pixel/loop300 qualification claim'))
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tag', required=True)
    ap.add_argument('--candidate', type=Path, required=True)
    ap.add_argument('--hardware', type=Path, required=True)
    ap.add_argument('--execute', action='store_true')
    ap.add_argument('--integrated-navigation',action='store_true')
    args = ap.parse_args()
    assert args.tag.replace('_', '').isalnum()
    candidate, hardware = args.candidate.resolve(), args.hardware.resolve()
    release = json.loads((candidate / 'release_manifest.json').read_text())
    assert not release['adopted'] and not release['board_tested']
    for name, digest in release['files'].items():assert sha(candidate / name) == digest, name
    audio = json.loads((hardware / 'scarlet_audio_qualification.json').read_text())
    assert audio['pass_'] and audio['files']['top.bit'] == sha(candidate / 'package/top.bit')
    assert audio['physical_qualification_sha256'] == sha(hardware / 'hardware_qualification.json')
    support = BASE / ('scarlet_music_support_' + args.tag)
    support.mkdir(exist_ok=False)
    plan = dict(deadline_seconds=300, normal_input_check=False, stage='Scarlet digital music first board probe',
                owner='existing persistent native owner; observer writes JSON leases only',
                manual_acceptance='user ready for final integrated test; this run remains automatic')
    path = support / 'plan.json'
    path.write_text(json.dumps(plan, indent=2) + '\n')
    driver = ROOT / 'software/touhou_scarlet_20261031/music_board_driver.py'
    if args.integrated_navigation:
        driver=ROOT/'software/scarlet_integrated_20261031/music_board_driver.py'
    from observer_readonly_20261031 import convert
    parent_driver=driver;driver=support/'music_board_driver.py'
    driver.write_text(convert(parent_driver.read_text(encoding='utf-8')),encoding='utf-8')
    compile(driver.read_text(encoding='utf-8'), str(driver), 'exec')
    if not args.execute:
        print('SCARLET_MUSIC_BOARD_PLAN_PREPARED_NO_BOARD', path)
        return
    import run_display_scale_20261030 as board
    original_download = board.download

    def download(sftp, remote, local):
        original_download(sftp, remote, local)
        if remote.endswith('/runs'):
            for run in local.iterdir():
                manifest = json.loads((run / 'candidate_manifest.json').read_text())
                for name in ('native_session.py', 'bt_mailbox.py', 'scarlet_pcm.py', 'pynq_brotato_session.py'):
                    sftp.get(remote + '/' + run.name + '/' + name, str(run / name))
                    assert sha(run / name) == manifest['files'][name]

    board.download = download
    board.validate_runs = lambda folder: validate_music_runs(folder, candidate)
    board.OUT = BASE
    board.DRIVER = driver
    sys.argv = [board.__file__, '--tag', 'scarlet_music_' + args.tag, '--candidate', str(candidate),
                '--hardware', str(hardware / 'hardware_qualification.json'), '--plan', str(path)]
    # This unchanged guard freshly discovers formal N/boot/owner/protection,
    # retires and restores it only after proved owner/buffer release.
    board.main()


if __name__ == '__main__':
    main()
