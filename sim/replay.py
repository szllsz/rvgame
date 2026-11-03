"""Replay frozen native800 fixtures in a new directory; no board access."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bundle', type=Path, default=Path(__file__).resolve().parent)
    ap.add_argument('--destination', type=Path, required=True)
    ap.add_argument('--vivado-bin', type=Path, required=True)
    a = ap.parse_args()
    bundle, out, tool = a.bundle.resolve(), a.destination.resolve(), a.vivado_bin.resolve()
    manifest = json.loads((bundle / 'fixtures.json').read_text())
    if out.exists():
        raise RuntimeError('Use a new destination; existing evidence is never deleted')
    for case in manifest['cases']:
        for name, digest in case['files'].items():
            assert sha(bundle / 'cases' / case['name'] / name) == digest, (case['name'], name)
    out.mkdir(parents=True)
    result = dict(pass_=False, board_tested=False, tool_bin=str(tool),
                  fixture_manifest_sha256=sha(bundle / 'fixtures.json'), cases=[])
    def save():
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    try:
        for case in manifest['cases']:
            cwd = out / case['name']
            shutil.copytree(bundle / 'cases' / case['name'], cwd)
            receipt = dict(name=case['name'], pass_=False, commands=[], expected_rejection=case['negative'])
            result['cases'].append(receipt)
            logs = []
            for index, args in enumerate(case['commands']):
                command = [str(tool / args[0]), *args[1:]]
                assert Path(command[0]).is_file(), command[0]
                logpath = cwd / f'{index}.log'
                start = time.monotonic()
                save()
                with logpath.open('wb') as handle:
                    p = subprocess.run(command, cwd=cwd, stdout=handle,
                                       stderr=subprocess.STDOUT, timeout=900)
                log = logpath.read_text(errors='replace')
                logs.append(log)
                receipt['commands'].append(dict(command=command, exit_code=p.returncode,
                                                 seconds=time.monotonic() - start, log_sha256=sha(logpath)))
                expected_failure = case['negative'] and index == len(case['commands']) - 1
                if expected_failure:
                    # XSim -runall returns 0 after $fatal on this toolchain.
                    fatals = [line.strip() for line in log.splitlines() if line.startswith('Fatal:')]
                    assert p.returncode == 0 and fatals == [case['markers'][0]], 'Expected exact negative assertion'
                    assert 'ERROR:' not in log and 'PASS ' not in log, 'Unexpected outcome in negative simulation'
                    assert '$finish called at time' in log
                else:
                    assert p.returncode == 0 and 'Fatal:' not in log, logpath
                save()
            combined = '\n'.join(logs)
            for marker in case['markers']:
                assert marker in combined, marker
            receipt['markers'] = case['markers']
            receipt['pass_'] = True
            save()
            print('SIM_FIXTURE_PASS', case['name'], flush=True)
        result['pass_'] = True
        result['scope'] = manifest['scope']
    except Exception as error:
        result['error'] = repr(error)
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
