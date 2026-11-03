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
    drive(.08,events=[['touch',[raw]]]);drive(.1,events=[['touch',[]]])

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

def latest(name,predicate):
    path=BASE/name
    if not path.exists():return None
    with path.open('rb') as stream:
        stream.seek(max(0,path.stat().st_size-131072));lines=stream.read().splitlines()
    for line in reversed(lines):
        try:value=json.loads(line)
        except (ValueError,UnicodeError):continue
        if predicate(value):return value

try:
    ready=wait(lambda:read('ready.json'),90);assert ready['test_input'] and ready['compact_exit']
    result['ready']=ready;control=ReadOnlyMMIO(0x43c00000,4096)
    frame=ReadOnlyMMIO(ready['frame_base'],1048576)
    result['scope']='Actual board framebuffer screenshots plus direct pointer entry/return replay; not physical photographs or fresh long-window performance qualification'
    active('menu');drive(.5);capture('04_main_menu')
    for kind in ('mouse','touch'):
        for index,app in enumerate(('coremark','gfx','bitmap','alpha','game','touhou')):
            before=active('menu');at=time.monotonic()
            x,y=150+(index%3)*250,210+(index//3)*205
            if kind=='mouse':
                move(x,y);assert read('session_current.json')['active']=='menu'
                mouse_click(x,y)
            else:
                tap(x,y);assert read('session_current.json')['active']=='menu'
                tap(x,y)
            state=active(app,before['segment']);drive(.5)
            assert (control.read(0x9c)>>22)&63!=63, 'Pointer ABI leaked into application'
            assert control.read(0x78)==0 and not control.read(0x60)&1
            check(kind+'_enter_'+app,state=state,expected_card=index)
            if kind=='mouse':
                if app=='coremark':
                    cpu=wait(lambda:latest('plain_samples.jsonl',lambda s:s['session_app']=='coremark' and s['session_segment']==state['segment'] and s['page']==0x361 and s.get('cpu_text')),90)
                    assert cpu['cpu_text'].count('Correct operation validated.')==2 and 'ERROR!' not in cpu['cpu_text']
                    result['cpu_result']=cpu;drive(.5);capture('05_cpu_performance')
                    drive(.12,0x8000);drive(.4);capture('06_cpu_crc_details')
                elif app in ('gfx','bitmap','alpha'):
                    magic={'gfx':0x47584631,'bitmap':0x424d5031,'alpha':0x414c5031}[app]
                    count={'gfx':512,'bitmap':96,'alpha':32}[app]
                    sample=wait(lambda:latest('plain_samples.jsonl',lambda s:s['session_app']==app and s['session_segment']==state['segment'] and len(s.get('footer',[]))==16 and s['footer'][0]==magic and s['footer'][2]==count and s['footer'][3]==0 and s['footer'][15]==(s['footer'][1]^0xffffffff)),45)
                    result.setdefault('graphics_samples',{})[app]=sample;drive(2)
                    capture({'gfx':'07_tile','bitmap':'08_ddr_bitmap','alpha':'09_alpha'}[app])
                elif app=='game':
                    drive(.12,2);drive(.2);drive(.12,0x1000);drive(8)
                    capture('10_egg_bros_showcase')
                elif app=='touhou':
                    drive(.12,0x1000);drive(.2);state=active('touhou',state['segment']);drive(8)
                    sample=wait(lambda:latest('music_samples.jsonl',lambda s:s['application']=='touhou' and s['segment']==state['segment'] and s.get('digital_crc_verified')),30)
                    manifest=read('candidate_manifest.json');spec=manifest['native_music']['touhou']
                    assert sample['music_sha256']==spec['sha256'] and sample['music_file']==spec['file']
                    assert sample['underflows']==sample['serial_faults']==sample['serial_quiet_faults']==0
                    result['trimmed_battle_music']=sample;capture('11_star_barrage_battle')
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
