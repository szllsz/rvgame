import hashlib,mmap
"""File-only raw-input observer for the actual persistent native800 owner."""
import json
import os
from pathlib import Path
import signal
import sys
import time
import traceback

BASE=Path(sys.argv[1]);result=dict(pass_=False,geometry=[800,600],manual_tested=False,checks=[])


def read(name):
    path=BASE/name
    return json.loads(path.read_text()) if path.exists() else None


def write(name,value):
    path=BASE/(name+'.next');path.write_text(json.dumps(value,indent=2)+'\n');os.replace(path,BASE/name)


def drive(seconds,buttons=0,lt=0,rt=0):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        write('raw_input.json',dict(connected=True,until=time.monotonic()+.16,
              sample=dict(buttons=buttons,lx=0,ly=0,rx=0,ry=0,lt=lt,rt=rt)))
        time.sleep(.02)


def pulse(buttons):drive(.16,buttons);drive(.24)


def held_action(buttons,predicate,seconds=60):
    until=time.monotonic()+seconds
    try:
        while time.monotonic()<until:
            assert not read('owner_error.json'),read('owner_error.json')
            drive(.08,buttons)
            value=predicate()
            if value:return value
        raise TimeoutError('Native800 held action acknowledgement timed out')
    finally:drive(.4)


def wait(predicate,seconds=45):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        assert not read('owner_error.json'),read('owner_error.json')
        value=predicate()
        if value:return value
        drive(.08)
    raise TimeoutError('Native800 suite observer condition timed out')


def latest(name,predicate):
    path=BASE/name
    if not path.exists():return None
    with path.open('rb') as stream:
        stream.seek(max(0,path.stat().st_size-131072));lines=stream.read().splitlines()
    for line in reversed(lines):
        try:value=json.loads(line)
        except (ValueError,UnicodeError):continue
        if predicate(value):return value


def active(app,after=-1):
    def state():
        value=read('session_current.json')
        return value if value and value['active']==app and value['segment']>after else None
    return wait(state)


def menu():
    return wait(lambda:latest('samples.jsonl',lambda s:s.get('menu_ui') and s['coherent'] and not s.get('mailbox_final') and s['session_segment']==read('session_current.json')['segment']))


def enter(index,app):
    before=active('menu');selected=menu()['structure_telemetry'][4]
    for _ in range(6):
        if selected==index:break
        pulse(8);after=time.monotonic()
        value=wait(lambda:latest('samples.jsonl',lambda s:s.get('menu_ui') and not s.get('mailbox_final') and s['monotonic']>=after))
        selected=value['structure_telemetry'][4]
    assert selected==index
    pulse(0x1000);value=active(app,before['segment'])
    result['checks'].append(dict(name='entered_'+app,state=value));return value


def leave(state):
    pulse(0x20);value=active('menu',state['segment']);selected=menu()['structure_telemetry'][4]
    assert selected=={'coremark':0,'gfx':1,'bitmap':2,'alpha':3,'game':4,'touhou':5}[state['active']],selected
    result['checks'].append(dict(name='returned_from_'+state['active'],state=value))


def footer(state,count=None,sw=0):
    magic={'gfx':0x47584631,'bitmap':0x424d5031,'alpha':0x414c5031}[state['active']]
    return latest('plain_samples.jsonl',lambda s:s['session_app']==state['active'] and s['session_segment']==state['segment']
                  and len(s.get('footer',[]))==16 and s['footer'][0]==magic and s['footer'][3]==sw
                  and (count is None or s['footer'][2]==count) and s['footer'][15]==(s['footer'][1]^0xffffffff))

def meter(words):
    assert len(words)==256 and words[0]==0x43415031 and words[1]==1 and words[24]==(words[2]^0xffffffff) and words[25]==1
    n=words[2];cycles=words[3]+(words[4]<<32);seconds=cycles/105000000
    assert n>2 and sum(words[64:128])==sum(words[128:192])==n and sum(words[192:256])==n-1
    assert words[44]==n and sum(words[45:64])==n
    fps=words[13]/seconds
    scan_strict=59.9<=fps<=60.1 and words[13]==words[5]==n-1 and not any(words[i] for i in (12,14,17))
    return dict(seconds=seconds,frames=words[13],scans=words[5],FPS=fps,count=words[34],software=words[35],
                missed=words[17],nonunit=words[14],work_over_budget=words[12],
                scan_strict=scan_strict,strict60=seconds>=60 and scan_strict,
                draw_ms=(words[6]+(words[7]<<32))/n/105000,work_ms=(words[8]+(words[9]<<32))/n/105000,
                copy_scan_wait_ms=(words[40]+(words[41]<<32))/n/105000,work_max_ms=words[11]/105000,
                copy_scan_wait_min_ms=words[43]/105000,copy_scan_wait_max_ms=words[42]/105000)


def scarlet_sample(state,after=0):
    return latest('samples.jsonl',lambda s:s.get('session_app')=='touhou' and s['session_segment']==state['segment']
                  and s.get('coherent') and not s.get('mailbox_final') and s['monotonic']>=after)


def scarlet_timed(state,density):
    time_started=time.monotonic()
    before=wait(lambda:scarlet_sample(state,time_started),30)
    a=before['telemetry'];first=a[32]|a[33]<<32
    assert a[60:63]==[0,3,density]
    last=wait(lambda:(lambda s:s if s and (s['telemetry'][32]|s['telemetry'][33]<<32)-first>=60*105000000 else None)(scarlet_sample(state,before['monotonic'])),100)
    b=last['telemetry'];seconds=((b[32]|b[33]<<32)-first)/105000000
    frames=b[34]-a[34];scans=b[35]-a[35];nonunit=b[36]-a[36];missed=b[37]-a[37];logic=b[39]-a[39]
    assert a[60:63]==b[60:63]==[0,3,density]
    stats=dict(seconds=seconds,completed_frames=frames,scans=scans,completed_FPS=frames/seconds,
               nonunit_scans=nonunit,missed_scans=missed,missed_logic_ticks=logic,
               strict_every_scan=seconds>=60 and frames==scans and nonunit==missed==logic==0 and 59.9<=frames/seconds<=60.1)
    return dict(density=density,start_sample=before,end_sample=last,timed_interval=stats)


def scarlet_density(state,target):
    # Density is an edge action. Release before parsing slow coherent packets;
    # a leased long hold can disconnect/reconnect and produce a second edge.
    after=time.monotonic()
    for _ in range(8):
        current=wait(lambda:scarlet_sample(state,after),30)
        level=current['telemetry'][62]
        if level==target:return current
        pulse(8 if level<target else 4)
        after=time.monotonic()
    raise AssertionError('Scarlet density did not reach the requested level')

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
LEVELS=[1, 2, 9]

def egg_sample(state,after=0):
    return latest('showcase_samples.jsonl',lambda s:s.get('session_app')=='game' and s['session_segment']==state['segment'] and s['monotonic']>=after)

def stage(state,wave,after=0):
    row=egg_sample(state,after)
    if row and row['structure_telemetry'][3]==wave:
        w,d=row['structure_telemetry'],row['telemetry']
        assert w[0]==0x42545339 and w[2]==4 and w[12:15]==[20261028,1,0]
        assert d[:3]==[0x42575334,20,5] and d[3]+d[4]==d[5]==2
        assert not row['underflows'] and not row['ddr_status']&1
        return row

def egg_timed(state,wave):
    began=time.monotonic();closed={};population={1:64,10:85,20:100}[wave]
    while time.monotonic()-began<75:
        drive(.1);assert not read('owner_error.json'),read('owner_error.json')
        row=egg_sample(state,began)
        if row:
            assert row['structure_telemetry'][3]==wave, 'Unrequested automatic wave change'
            stage(state,wave,began)
            prev=row['telemetry'][19:27]
            seconds=(prev[3]|prev[4]<<32)/105000000
            if prev[0] and prev[1]==population and prev[2]>1 and 7.8<=seconds<=8.2:
                point=dict(epoch=prev[0],population=population,seconds=seconds,frames=prev[6],scans=prev[5],presents=prev[2]-1,
                    FPS=prev[6]/seconds,nonunit=prev[7],missed=prev[5]-prev[6],
                    strict60=prev[6]==prev[5]==prev[2]-1 and prev[7]==0 and 59.9<=prev[6]/seconds<=60.1,
                    sample=row)
                if prev[0] in closed:assert closed[prev[0]]['frames']==point['frames']
                else:closed[prev[0]]=point
        if time.monotonic()-began>=35 and len(closed)>=2:break
    assert len(closed)>=2
    points=list(closed.values())
    return dict(wave=wave,population=population,held_wall_seconds=time.monotonic()-began,closed_epochs=points,
        measurement=dict(FPS=sum(p['FPS'] for p in points)/len(points),strict60=all(p['strict60'] for p in points),
                         scope='Repeated original 10s scene; each meter excludes first2s; not one continuous60s window'))

try:
    ready=wait(lambda:read('ready.json'),90);assert ready['test_input'] and ready['compact_exit']
    result.update(runs=[],images=[],egg_performance=[],ready=ready)
    control=ReadOnlyMMIO(0x43c00000,4096);frame=ReadOnlyMMIO(ready['frame_base'],1048576)
    active('menu');menu();state=enter(4,'game');drive(.5);capture('egg_title')
    pulse(2);pulse(0x1000);wait(lambda:control.read(0x48)>>16==0x351,15)
    for index,wave in enumerate((1,10,20)):
        if index:drive(.8,rt=255);drive(.3)
        after=time.monotonic();sample=wait(lambda:stage(state,wave,after),45)
        drive(3);capture('egg_manual_stage_'+str(index+1));time_start=time.monotonic()
        measure=egg_timed(state,wave);result['egg_performance'].append(measure)
        result['checks'].append(dict(name='manual_wave_'+str(wave)+'_retained',measurement=measure['measurement']))
        write('progress.json',result)
    drive(.8,rt=255);drive(.3);after=time.monotonic();sample=wait(lambda:stage(state,20,after),45)
    for wave in (10,1):
        drive(.8,lt=255);drive(.3);after=time.monotonic();sample=wait(lambda:stage(state,wave,after),45)
    drive(.8,lt=255);drive(.3);after=time.monotonic();sample=wait(lambda:stage(state,1,after),45)
    drive(.8,lt=255,rt=255);drive(.3);after=time.monotonic();sample=wait(lambda:stage(state,1,after),45)
    result['checks'].append(dict(name='manual_wave_bounds_and_trigger_chord',sample=sample))
    pulse(0x10);wait(lambda:control.read(0x48)>>16==0x352,12);capture('egg_pause')
    drive(.8,rt=255);drive(.3);pulse(0x10);wait(lambda:control.read(0x48)>>16==0x351,12)
    after=time.monotonic();sample=wait(lambda:stage(state,1,after),45)
    result['checks'].append(dict(name='pause_trigger_ignored_resume_same_wave',sample=sample));leave(state)
    state=enter(4,'game');drive(.5);pulse(0x1000)
    for _ in range(5):pulse(0x1000)
    wait(lambda:control.read(0x48)>>16==0x351,12);drive(.2,4);drive(.2);capture('egg_normal_camera')
    drive(.8,lt=255,rt=255);drive(.3);after=time.monotonic()
    sample=wait(lambda:latest('samples.jsonl',lambda s:s.get('session_app')=='game' and s['session_segment']==state['segment'] and s.get('coherent') and not s.get('mailbox_final') and s['monotonic']>=after),45)
    assert not sample['telemetry'][17]
    result['checks'].append(dict(name='normal_game_after_manual_showcase',sample=sample));leave(state)
    home=enter(5,'touhou');pulse(0x1000);state=active('touhou',home['segment']);drive(8);capture('star_combined_battle')
    sample=wait(lambda:scarlet_sample(state));assert sample['telemetry'][62]==1 and sample['telemetry'][31]&15==5
    drive(.8,rt=255);drive(.3);after=time.monotonic()
    sample=wait(lambda:(lambda s:s if s and s['telemetry'][62]==2 else None)(scarlet_sample(state,after)),45)
    result['checks'].append(dict(name='star_RT_same_combined_runtime',sample=sample));leave(state)
    result['finished']=True
except Exception as error:result.update(error=repr(error),traceback=traceback.format_exc())

finally:
    ready=read('ready.json')
    if ready and not read('cleanup.json'):
        pid=ready['pid'];command=Path('/proc',str(pid),'cmdline')
        if command.exists():
            assert str(BASE/'pynq_brotato_session.py').encode() in command.read_bytes()
            os.kill(pid,signal.SIGINT)
    until=time.monotonic()+70
    while not read('cleanup.json') and time.monotonic()<until:drive(.1)
    cleanup=read('cleanup.json');result['cleanup']=cleanup
    result['pass_']=bool(not result.get('error') and cleanup and cleanup['pass'] and not cleanup.get('error')
                         and all(cleanup.get(k) for k in ('program_freed','display_freed','hp_restored','trace_writer_flushed')))
    write('result.json',result)
assert result['pass_'],result.get('error')
