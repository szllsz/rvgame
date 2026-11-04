"""One persistent native owner; only classic hardware requires another cycle."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

BASE=Path(__file__).resolve().parent
child=None
stopping=False


def stop(signum,frame):
    global stopping
    stopping=True
    if child is not None and child.poll() is None:child.send_signal(signal.SIGINT)


def verify_release(full=False):
    release=json.loads((BASE/'release_manifest.json').read_text())
    name='package/candidate_manifest.json'
    raw=(BASE/name).read_bytes()
    assert hashlib.sha256(raw).hexdigest()==release['files'][name],name
    native=json.loads(raw)
    # Code and both manifests are checked before importing the child. Large
    # data is checked by the child before its first use, Overlay or program load.
    data_suffixes={'.bin','.pcm','.bit','.hwh','.rgb332'}
    deferred=[]
    for name,digest in release['files'].items():
        path=BASE/name
        assert path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(BASE)
        if (not full and path.parent==BASE/'package' and path.suffix in data_suffixes
                and native['files'].get(path.name)==digest):
            deferred.append(name)
        else:assert hashlib.sha256(path.read_bytes()).hexdigest()==digest,name
    return release,deferred


def main():
    global child
    signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
    startup_begin=time.monotonic()
    release,deferred=verify_release()
    print('RVGAME_BOOT_STAGE '+json.dumps(dict(stage='release_verified',monotonic=time.monotonic(),supervisor_begin=startup_begin,child_verified_payloads=len(deferred))),flush=True)
    runs=BASE/'runs';runs.mkdir(exist_ok=True)
    test='--test-input' in sys.argv;start_menu=False;classic=None
    while not stopping:
        target='classic' if classic is not None else 'session'
        run=runs/(str(time.monotonic_ns())+'_'+target);run.mkdir()
        for file in (BASE/'package').iterdir():
            if file.is_file():
                assert not file.is_symlink()
                os.link(file,run/file.name,follow_symlinks=False)
        if classic is None:
            command=[sys.executable,'-u',str(run/'pynq_brotato_session.py')]
            if start_menu:command.append('--start-menu')
        else:
            # The preserved classic owner closes/drains before native PL returns.
            verify_release(full=True)
            command=[sys.executable,'-u',str(run/'pynq_classic_session.py'),str(classic)]
        if test:
            command.append('--test-input')
            if classic is None:command.extend(['--fast-switch','--capture-metadata'])
        print('RVGAME_BOOT_STAGE '+json.dumps(dict(stage='payload_links_ready',monotonic=time.monotonic(),run=str(run))),flush=True)
        child=subprocess.Popen(command)
        state=dict(supervisor_pid=os.getpid(),child_pid=child.pid,target=target,directory=str(run),run=str(run),
            persistent_native_owner=True,test_input=test,monotonic=time.monotonic(),boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip())
        temporary=BASE/'supervisor_state.json.next';temporary.write_text(json.dumps(state,indent=2)+'\n');os.replace(temporary,BASE/'supervisor_state.json')
        if stopping:child.send_signal(signal.SIGINT)
        code=child.wait();state.update(exit_code=code,end=time.monotonic(),stop_requested=stopping)
        (run/'child_exit.json').write_text(json.dumps(state,indent=2)+'\n')
        assert code==0
        if target=='session':
            cleanup=json.loads((run/'cleanup.json').read_text());assert cleanup['pass'] and cleanup['pid']==child.pid
            assert all(cleanup[k] for k in ['program_freed','display_freed','hp_restored'])
            assert not (run/'owner_error.json').exists()
            route=run/'exit_route.json';classic=json.loads(route.read_text())['classic'] if route.exists() else None
            if not stopping and classic is None:break
        else:
            result=json.loads((run/'classic_result.json').read_text());assert result['pass_']
            classic=None;start_menu=True
        child=None
        if stopping:break


if __name__=='__main__':main()
