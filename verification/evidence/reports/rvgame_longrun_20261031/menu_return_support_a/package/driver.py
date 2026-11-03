"""Explicit menu entry/return replay through the existing owner."""
import hashlib
import json
import mmap
import os
from pathlib import Path
import signal
import sys
import time
import traceback
BASE=Path(sys.argv[1]);sequence=0
result=dict(pass_=False,manual_tested=False,application_entry_tested=True,runs=[],checks=[],images=[])

def read(name):
    p=BASE/name;return json.loads(p.read_text()) if p.exists() else None

def write(name,value):
    p=BASE/(name+'.next');p.write_text(json.dumps(value)+'\n');os.replace(p,BASE/name)

def drive(seconds,buttons=0,events=()):
    global sequence
    sequence+=1;end=time.monotonic()+seconds
    while time.monotonic()<end:
        write('raw_input.json',dict(sequence=sequence,connected=True,until=time.monotonic()+.2,pointer_events=events,
            sample=dict(buttons=buttons,lx=0,ly=0,rx=0,ry=0,lt=0,rt=0)))
        time.sleep(.02)

def move(x,y):
    drive(.15,events=[['mouse',[-10000,-10000,False,False,0]],['mouse',[x,y,False,False,0]]])

def tap(x,y):
    raw=dict(id=1,x=(x+112)*4,y=y*4096//600)
    drive(.18,events=[['touch',[raw]]]);drive(.2,events=[['touch',[]]])

def wait(fn,seconds=30):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        assert not read('owner_error.json'),read('owner_error.json')
        value=fn()
        if value:return value
        drive(.08)
    raise TimeoutError('Menu pointer condition timed out')

def last_menu(after=0):
    p=BASE/'samples.jsonl'
    if not p.exists():return None
    with p.open('rb') as f:
        f.seek(max(0,p.stat().st_size-100000));lines=f.read().splitlines()
    for line in reversed(lines):
        try:r=json.loads(line)
        except (ValueError,UnicodeError):continue
        if r.get('menu_ui') and r.get('coherent') and not r.get('mailbox_final') and r['monotonic']>=after:return r

def selected(index,after=0):
    row=wait(lambda:last_menu(after) if last_menu(after) and last_menu(after)['structure_telemetry'][4]==index else None)
    assert read('session_current.json')['active']=='menu'
    return row

def check(name,**kw):
    result['checks'].append(dict(name=name,**kw));write('progress.json',result)

class ReadOnlyMMIO:
    def __init__(self,base,length):
        assert (base==0x43c00000 and length==4096) or (0<=base<0x40000000 and base+length<=0x40000000 and length==1048576)
        aligned=base&~(mmap.PAGESIZE-1);offset=base-aligned
        fd=os.open('/dev/mem',os.O_RDONLY|os.O_SYNC)
        try:self.mapping=mmap.mmap(fd,length+offset,mmap.MAP_SHARED,mmap.PROT_READ,offset=aligned)
        finally:os.close(fd)
        self.array=memoryview(self.mapping)[offset:offset+length].cast('I')
    def read(self,offset):return int(self.array[offset//4])

def capture(name):
    for _ in range(20):
        copies=control.read(0x7c);offset=(copies&1)*0x80000;raw=bytes(frame.array.cast('B')[offset:offset+480000])
        if copies==control.read(0x7c):
            (BASE/(name+'.rgb332')).write_bytes(raw)
            result['images'].append(dict(name=name,sha256=hashlib.sha256(raw).hexdigest()))
            return raw
        drive(.03)
    raise AssertionError('Coherent frame unavailable')

def cursor(kind,x,y):
    hot={'arrow':(2,2),'hand':(13,2)}[kind];ref=(BASE/('pointer_'+kind+'.rgb332')).read_bytes()
    raw=capture(kind+'_actual');matched=0
    for row in range(36):
        for col in range(32):
            color=ref[row*32+col];xx=x-hot[0]+col;yy=y-hot[1]+row
            if color!=227 and 0<=xx<800 and 0<=yy<598:
                assert raw[yy*800+xx]==color,(kind,row,col);matched+=1
    check('cursor_'+kind+'_exact',opaque_pixels=matched)

def active(app,after=-1):
    def state():
        s=read('session_current.json')
        return s if s and s['active']==app and s['segment']>after else None
    return wait(state)

def mouse_click(x,y):
    move(x,y)
    drive(.18,events=[['mouse',[0,0,True,False,0]]])
    drive(.25,events=[['mouse',[0,0,False,False,0]]])

try:
    ready=wait(lambda:read('ready.json'),90);assert ready['test_input'] and ready['compact_exit']
    result['ready']=ready;control=ReadOnlyMMIO(0x43c00000,4096)
    active('menu');drive(.4)
    for kind in ('mouse','touch'):
        for index,app in enumerate(('coremark','gfx','bitmap','alpha','game','touhou')):
            before=active('menu');at=time.monotonic()
            x,y=150+(index%3)*250,210+(index//3)*205
            if kind=='mouse':move(x,y)
            else:tap(x,y)
            row=selected(index,at)
            if kind=='mouse':mouse_click(450,570)
            else:tap(450,570)
            state=active(app,before['segment']);drive(.5)
            assert (control.read(0x9c)>>22)&63!=63, 'Pointer ABI leaked into application'
            assert control.read(0x78)==0 and not control.read(0x60)&1
            check(kind+'_enter_'+app,selection_sample=row,state=state)
            if kind=='mouse':
                drive(.18,events=[['mouse',[0,0,False,True,0]]]);drive(.2,events=[['mouse',[0,0,False,False,0]]])
            else:tap(400,570)
            returned=active('menu',state['segment']);drive(.4)
            row=selected(index)
            check(kind+'_return_'+app,state=returned,selection_sample=row)
    result['finished']=True
except Exception as error:result.update(error=repr(error),traceback=traceback.format_exc())
finally:
    ready=read('ready.json')
    if ready and not read('cleanup.json'):
        pid=ready['pid'];cmd=Path('/proc',str(pid),'cmdline')
        if cmd.exists():
            assert str(BASE/'pynq_brotato_session.py').encode() in cmd.read_bytes();os.kill(pid,signal.SIGINT)
    end=time.monotonic()+90
    while not read('cleanup.json') and time.monotonic()<end:drive(.1)
    clean=read('cleanup.json');result['cleanup']=clean
    result['pass_']=bool(result.get('finished') and not result.get('error') and clean and clean['pass'] and not clean.get('error')
        and all(clean.get(k) for k in ('program_freed','display_freed','hp_restored','trace_writer_flushed')))
    write('result.json',result)
assert result['pass_'],result.get('error')
