"""Verify a detached native800 bundle, then explicitly start its sole owner."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

BASE=Path(__file__).resolve().parent


def verify():
    release=json.loads((BASE/'release_manifest.json').read_text(encoding='utf-8'))
    for name,digest in release['files'].items():
        path=(BASE/name).resolve();assert path.is_relative_to(BASE)
        assert hashlib.sha256(path.read_bytes()).hexdigest()==digest,name
    native=json.loads((BASE/'package/candidate_manifest.json').read_text())
    assert native['native_geometry']==[800,600] and native['session_start']=='menu'
    for name,digest in native['files'].items():assert hashlib.sha256((BASE/'package'/name).read_bytes()).hexdigest()==digest,name
    assert len(native['applications'])==8
    return len(release['files'])


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--start',action='store_true',help='Start on the authorized, idle AX7020 only')
    a=ap.parse_args();count=verify();print('STANDALONE_BYTES_VERIFIED',count,flush=True)
    if not a.start:return
    assert sys.platform=='linux' and os.geteuid()==0,'Board Linux root/PYNQ environment required'
    # Inspect known owners before importing PYNQ or touching any MMIO.
    owners=[]
    for folder in Path('/proc').iterdir():
        if not folder.name.isdigit() or int(folder.name)==os.getpid():continue
        try:cmd=(folder/'cmdline').read_bytes().replace(b'\0',b' ')
        except (FileNotFoundError,PermissionError,ProcessLookupError):continue
        if any(name in cmd for name in (b'pynq_brotato_session.py',b'pynq_classic_session.py',b'/supervisor.py')):owners.append(int(folder.name))
    assert not owners,'Existing owner must retire normally before start; PIDs='+str(owners)
    import fcntl
    with (BASE/'owner.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        child=subprocess.Popen([sys.executable,'-u',str(BASE/'supervisor.py')],cwd=BASE)
        def stop(signum,frame):
            if child.poll() is None:child.send_signal(signal.SIGINT)
        signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
        code=child.wait();assert code==0,'Owner failed; inspect run logs and preserve resources'
        state=json.loads((BASE/'supervisor_state.json').read_text());run=Path(state['run'])
        clean=json.loads((run/'cleanup.json').read_text());assert clean['pass'] and not clean.get('error')
        assert all(clean[k] for k in ('program_freed','display_freed','hp_restored'))
        print('STANDALONE_OWNER_RETIRED',flush=True)


if __name__=='__main__':main()
