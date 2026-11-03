"""Bounded file-only logging; the owner thread remains the sole MMIO writer."""
import json
import os
from pathlib import Path
import queue
import threading


class TraceWriter:
    def __init__(self,directory,write_hook=None):
        self.directory=Path(directory);self.pending=queue.Queue(maxsize=4096)
        self.error=None;self.closed=False;self.write_hook=write_hook
        self.thread=threading.Thread(target=self._run,name='rvgame-file-trace',daemon=True);self.thread.start()

    def append(self,name,value):
        if self.closed or self.error:raise RuntimeError('Trace writer unavailable: '+str(self.error))
        if Path(name).name!=name or not name.endswith('.jsonl'):raise ValueError('Invalid trace filename')
        try:self.pending.put_nowait(('append',name,json.dumps(value)+'\n'))
        except queue.Full:raise RuntimeError('Trace backlog exceeded4096 records')

    def write_bytes(self,name,value):
        if self.closed or self.error:raise RuntimeError('Trace writer unavailable: '+str(self.error))
        if Path(name).name!=name or not isinstance(value,bytes) or len(value)>1048576:raise ValueError('Invalid immutable capture')
        try:self.pending.put_nowait(('bytes',name,value))
        except queue.Full:raise RuntimeError('Trace backlog exceeded4096 records')

    def write_json(self,name,value):
        if self.closed or self.error:raise RuntimeError('Trace writer unavailable: '+str(self.error))
        if Path(name).name!=name or not name.endswith('.json'):raise ValueError('Invalid state filename')
        text=json.dumps(value,indent=2)+'\n'
        try:self.pending.put_nowait(('json',name,text))
        except queue.Full:raise RuntimeError('Trace backlog exceeded4096 records')

    def _run(self):
        while True:
            item=self.pending.get()
            if item is None:return
            if self.error is not None:continue
            try:
                kind,name,text=item
                if self.write_hook:self.write_hook(name,text)
                elif kind=='append':
                    with (self.directory/name).open('a') as stream:stream.write(text)
                elif kind=='bytes':(self.directory/name).write_bytes(text)
                elif kind=='json':
                    temporary=self.directory/(name+'.next');temporary.write_text(text)
                    os.replace(temporary,self.directory/name)
                else:raise RuntimeError('Unknown trace operation')
            except Exception as error:self.error=repr(error)

    def close(self):
        if self.closed:return
        self.closed=True;self.pending.put(None,timeout=30);self.thread.join(timeout=30)
        if self.thread.is_alive():raise RuntimeError('Trace writer did not drain')
        if self.error:raise RuntimeError('Trace file write failed: '+self.error)
