"""Verify the packaged helper; --install enables its exact files on PYNQ Linux."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

FILES={'keeper.py':'c8544c6abc140567236e63e0f8a20398c36533c2135eaf2d15a420e31a888b79',
       'rvgame-p008-poll.service':'716555570659cb2f06598d458752100e62bd722b6daf2d56cbb0fe2545ea624f'}


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--install',action='store_true');a=parser.parse_args()
    root=Path(__file__).resolve().parent
    for name,digest in FILES.items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,name
    if not a.install:print('P008_HELPER_BYTES_VERIFIED');return
    assert sys.platform=='linux' and os.geteuid()==0,'Board Linux root/PYNQ environment required'
    assert Path('/usr/local/share/pynq-venv/bin/python3').is_file(),'PYNQ Python interpreter missing'
    targets={'keeper.py':Path('/usr/local/lib/rvgame-p008-poll-20261102-a/keeper.py'),
             'rvgame-p008-poll.service':Path('/etc/systemd/system/rvgame-p008-poll.service')}
    # Idempotent only for exactly these qualified bytes. Never overwrite an
    # unrelated local service or a user's modified helper.
    for name,p in targets.items():
        if p.exists():assert p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest()==FILES[name],str(p)
    for name,p in targets.items():
        p.parent.mkdir(parents=True,exist_ok=True)
        if not p.exists():p.write_bytes((root/name).read_bytes());p.chmod(0o644)
    subprocess.run(['systemd-analyze','verify',str(targets['rvgame-p008-poll.service'])],check=True)
    subprocess.run(['systemctl','daemon-reload'],check=True)
    subprocess.run(['systemctl','enable','--now','rvgame-p008-poll.service'],check=True)
    print('P008_HELPER_ENABLED_NO_USB_RESET')


if __name__=='__main__':main()
