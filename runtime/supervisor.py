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


def main():
    global child
    signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
    release=json.loads((BASE/'release_manifest.json').read_text())
    for name,digest in release['files'].items():assert hashlib.sha256((BASE/name).read_bytes()).hexdigest()==digest,name
    runs=BASE/'runs';runs.mkdir(exist_ok=True)
    test='--test-input' in sys.argv;start_menu=False;classic=None
    while not stopping:
        target='classic' if classic is not None else 'session'
        run=runs/(str(time.monotonic_ns())+'_'+target);run.mkdir()
        for file in (BASE/'package').iterdir():
            if file.is_file():shutil.copyfile(file,run/file.name)
        if classic is None:
            command=[sys.executable,'-u',str(run/'pynq_brotato_session.py')]
            if start_menu:command.append('--start-menu')
        else:
            # The preserved classic owner closes/drains before native PL returns.
            command=[sys.executable,'-u',str(run/'pynq_classic_session.py'),str(classic)]
        if test:command.append('--test-input')
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
