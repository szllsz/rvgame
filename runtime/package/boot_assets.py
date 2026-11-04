"""Immutable asset reads with hash verification; no PYNQ imports or device access."""
import hashlib
import json
from pathlib import Path
import threading
import time

class Assets:
    def __init__(self, base):
        self.base=Path(base).resolve()
        self.manifest=json.loads((self.base/'candidate_manifest.json').read_text())
        self.cache={};self.events=[];self.error=None;self.worker=None

    def read(self, name):
        if name in self.cache:return self.cache[name]
        path=self.base/name
        assert path.resolve().parent==self.base and not path.is_symlink()
        started=time.monotonic();data=path.read_bytes()
        assert hashlib.sha256(data).hexdigest()==self.manifest['files'][name],name
        self.cache[name]=data
        self.events.append(dict(name=name,bytes=len(data),seconds=time.monotonic()-started))
        return data

    def start(self):
        assert self.worker is None
        def work():
            try:
                for name in self.manifest['files']:self.read(name)
            except Exception as error:self.error=error
        self.worker=threading.Thread(target=work,name='rvgame-readonly-asset-preload',daemon=True)
        self.worker.start()

    def finish(self):
        if self.worker is not None:self.worker.join()
        if self.error is not None:raise self.error
