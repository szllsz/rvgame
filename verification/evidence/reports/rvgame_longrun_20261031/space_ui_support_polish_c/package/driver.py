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


def drive(seconds,buttons=0):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        write('raw_input.json',dict(connected=True,until=time.monotonic()+.16,
              sample=dict(buttons=buttons,lx=0,ly=0,rx=0,ry=0,lt=0,rt=0)))
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

try:
    ready=wait(lambda:read('ready.json'),90)
    assert ready['geometry']==[800,600] and ready['test_input'] and ready['compact_exit']
    result.update(runs=[],touhou_performance=[],ready=ready)
    active('menu');menu()
    home=enter(5,'touhou');drive(1)
    pulse(0x1000);state=active('touhou',home['segment']);drive(3)
    default=scarlet_timed(state,1)
    result['touhou_performance'].append(default)
    result['checks'].append(dict(name='star_barrage_default_60s',measurement=default['timed_interval']))
    write('progress.json',result)
    pulse(0x10);after=time.monotonic();paused=wait(lambda:scarlet_sample(state,after))
    tick=paused['structure_telemetry'][3]
    drive(.7);after=time.monotonic();still=wait(lambda:scarlet_sample(state,after))
    assert still['structure_telemetry'][3]==tick
    old_volume=still['telemetry'][31]&15
    pulse(8 if old_volume<8 else 4);after=time.monotonic()
    volume=wait(lambda:(lambda s:s if s and (s['telemetry'][31]&15)!=old_volume else None)(scarlet_sample(state,after)))
    pulse(0x4000);after=time.monotonic()
    muted=wait(lambda:(lambda s:s if s and (s['telemetry'][31]^volume['telemetry'][31])&256 else None)(scarlet_sample(state,after)))
    pulse(2);changed=scarlet_density(state,2)
    pulse(2);pulse(8);after=time.monotonic()
    scene=wait(lambda:(lambda s:s if s and s['telemetry'][61]==0 else None)(scarlet_sample(state,after)))
    pulse(4);after=time.monotonic()
    scene_restored=wait(lambda:(lambda s:s if s and s['telemetry'][61]==3 else None)(scarlet_sample(state,after)))
    pulse(0x2000);after=time.monotonic()
    resumed=wait(lambda:(lambda s:s if s and s['structure_telemetry'][3]>scene_restored['structure_telemetry'][3] else None)(scarlet_sample(state,after)))
    assert read('session_current.json')['segment']==state['segment']
    result['checks'].append(dict(name='pause_volume_mute_density_background_B_resume',paused=still,volume=volume,muted=muted,scene=scene,resumed=resumed))
    adjacent=scarlet_timed(state,2);result['touhou_performance'].append(adjacent)
    result['checks'].append(dict(name='star_barrage_adjacent_60s',measurement=adjacent['timed_interval']))
    write('progress.json',result)
    pulse(0x1000);after=time.monotonic()
    # A sealed pre-switch packet must finish before the first SW snapshot.
    # The real owner bounds each packet at120s and no-symbol progress at20s.
    # Allow the observer to receive the slow snapshot, without changing its
    # timestamp, CRC, mode predicate, or measured FPS.
    software=wait(lambda:(lambda s:s if s and s['telemetry'][60]==1 else None)(scarlet_sample(state,after)),180)
    drive(.8,0x1000);drive(.4);after=time.monotonic()
    hardware=wait(lambda:(lambda s:s if s and s['telemetry'][60]==0 else None)(scarlet_sample(state,after)),60)
    result['checks'].append(dict(name='software_hardware_modes',software=software,hardware=hardware))
    pulse(0x2000);home=active('touhou',state['segment']);drive(1)
    pulse(0x2000);active('menu',home['segment']);menu()
    result['checks'].append(dict(name='B_demo_home_menu',home=home))
    result['finished']=True
    assert default['timed_interval']['strict_every_scan'],default['timed_interval']
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
