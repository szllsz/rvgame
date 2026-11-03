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

import mmap as _observer_mmap
import os as _observer_os
import sys as _observer_sys
class MMIO:
    def __init__(self,base,length):
        assert _observer_sys.byteorder=='little' and isinstance(base,int) and base>=0 and length>0 and length%4==0
        assert (base==0x43c00000 and length==4096) or (base<0x40000000 and base+length<=0x40000000 and length==1048576)
        aligned=base&~(_observer_mmap.PAGESIZE-1);self._offset=base-aligned;self._length=length
        descriptor=_observer_os.open('/dev/mem',_observer_os.O_RDONLY|_observer_os.O_SYNC)
        try:self._map=_observer_mmap.mmap(descriptor,length+self._offset,_observer_mmap.MAP_SHARED,_observer_mmap.PROT_READ,offset=aligned)
        finally:_observer_os.close(descriptor)
        self.array=memoryview(self._map)[self._offset:self._offset+length].cast('I')
    def read(self,offset=0):
        assert isinstance(offset,int) and offset%4==0 and 0<=offset<=self._length-4
        return int(self.array[offset//4])

import hashlib
ROLES=[{'role': 0, 'width': 35, 'height': 40, 'x': -18, 'y': -35, 'sha256': 'f3e313cc41ab55ed29cb4c89e6fe85a4786b3def7dc2c1f5cb7b79dee899781f'}, {'role': 1, 'width': 35, 'height': 40, 'x': -18, 'y': -35, 'sha256': 'a33f929f9995d2c3c369784d5e728fe5a2f6f621d191fc35490777d94a12d78a'}, {'role': 2, 'width': 35, 'height': 40, 'x': -18, 'y': -35, 'sha256': '1bbbb6590a0e4ff10db43d03a6713b60f03a5b0ce3b976a750c17226b9898760'}, {'role': 3, 'width': 38, 'height': 40, 'x': -19, 'y': -35, 'sha256': '1918089391e2226db9399f60a8450bfb5fb1be9971133bd91b5fc9d6087d9147'}, {'role': 4, 'width': 35, 'height': 40, 'x': -18, 'y': -35, 'sha256': '7157ab68986e4cbc7aa9259bb2f5a50786047a6d2a7c9b92ccd7c70be5f20189'}]
control=MMIO(0x43c00000,4096)
frame=None
def capture_actor(role,label='center',anchor=(400,305)):
    spec=ROLES[role];expected=(BASE/('role_reference_'+str(role)+'.rgb332')).read_bytes()
    left=anchor[0]+spec['x'];top=anchor[1]+spec['y'];best=None
    for attempt in range(8):
        drive(.08);copies=control.read(0x7c);offset=(copies&1)*0x80000
        a=bytes(frame.array.cast('B')[offset:offset+480000]);b=bytes(frame.array.cast('B')[offset:offset+480000])
        if a!=b or copies!=control.read(0x7c):continue
        opaque=matched=head_opaque=head_matched=0
        for y in range(spec['height']):
            for x in range(spec['width']):
                pixel=expected[y*spec['width']+x]
                if pixel:
                    opaque+=1;matched+=b[(top+y)*800+left+x]==pixel
                    if y<spec['height']//2:
                        head_opaque+=1;head_matched+=b[(top+y)*800+left+x]==pixel
        ratio=matched/opaque
        if best is None or ratio>best['matched_fraction']:
            name='role_%d_%s.rgb332'%(role,label);(BASE/name).write_bytes(b)
            best=dict(role=role,matched_pixels=matched,opaque_pixels=opaque,matched_fraction=ratio,
                      visible_head_opaque=head_opaque,visible_head_matched=head_matched,
                      image_file=name,sha256=hashlib.sha256(b).hexdigest(),copies=copies,
                      actor_top_left=[left,top],scope='Exact upper-half opaque actor pixels; actual held wrench overlaps lower body; full unoccluded actor separately checked on host')
        if head_opaque>100 and head_matched==head_opaque:break
    assert best and best['visible_head_opaque']>100 and best['visible_head_matched']==best['visible_head_opaque'],('Visible actor head mismatch',role,best)
    return best

try:
    ready=wait(lambda:read('ready.json'),90)
    assert ready['geometry']==[800,600] and ready['test_input'] and ready['compact_exit']
    frame=MMIO(ready['frame_base'],1048576)
    result['runs']=[];result['touhou_performance']=[];result['ready']=ready;result['role_images']=[];result['camera_checks']=[]
    active('menu');menu()
    for role in range(5):
        state=enter(4,'game');drive(.5);pulse(0x1000)
        for _ in range(role):pulse(8)
        for _ in range(5):pulse(0x1000)
        wait(lambda:control.read(0x48)>>16==0x351,12)
        started=time.monotonic();image=capture_actor(role);result['role_images'].append(image)
        if role==0:
            # 450 world units/s, before the first short wave finishes. Initial
            # 0.2s left movement stays in the unclamped camera range; continued
            # 3s movement reaches the unchanged x=24 world boundary.
            drive(.2,4);drive(.12);follow=capture_actor(role,'follow')
            assert follow['actor_top_left']==image['actor_top_left'] and follow['sha256']!=image['sha256']
            drive(3,4);drive(.12)
            hx=320*320*256//119;cx=-96*256+hx
            sx=320-((cx-24*256)*119//(320*256))
            edge=capture_actor(role,'left_edge',(sx*5//4,305))
            assert edge['actor_top_left'][0]<image['actor_top_left'][0]-250
            result['camera_checks'].append(dict(role=role,follow=follow,left_edge=edge,
                scope='Normal input path; center actor unchanged after short movement, then exact actor pixels at expected left-boundary camera position'))
            pulse(0x10);wait(lambda:control.read(0x48)>>16==0x352,12)
            result['checks'].append(dict(name='camera_pause_page',page=control.read(0x48)>>16))
            pulse(0x10);wait(lambda:control.read(0x48)>>16==0x351,12)
            result['checks'].append(dict(name='camera_resume_page',page=control.read(0x48)>>16))

        sample=wait(lambda:latest('samples.jsonl',lambda s:s.get('session_app')=='game' and s['session_segment']==state['segment']
                    and s.get('coherent') and not s.get('mailbox_final') and s['monotonic']>=started),45)
        assert sample['telemetry'][56]==role and not sample['telemetry'][17]
        result['checks'].append(dict(name='normal_role_%d_visible'%role,image=image,sample=sample));write('progress.json',result)
        leave(state)
    state=enter(4,'game');drive(.5);pulse(2);pulse(0x1000)
    start=time.monotonic();closed={};seen=set()
    while time.monotonic()-start<75:
        drive(.1)
        assert not read('owner_error.json'),read('owner_error.json')
        row=latest('showcase_samples.jsonl',lambda s:s.get('session_app')=='game' and s['session_segment']==state['segment'])
        if row:
            w=row['structure_telemetry'];d=row['telemetry']
            assert w[0]==0x42545339 and w[2]==4 and w[12:15]==[20261028,1,0]
            assert d[:3]==[0x42575334,20,5] and d[3]+d[4]==d[5]==2
            assert not row['underflows'] and not row['ddr_status']&1
            previous=d[19:27]
            if previous[0] and previous[2]>1:
                seconds=(previous[3]+(previous[4]<<32))/105000000
                point=dict(epoch=previous[0],population=previous[1],seconds=seconds,frames=previous[6],scans=previous[5],
                           FPS=previous[6]/seconds,nonunit=previous[7],missed=previous[5]-previous[6],meter=previous,
                           stage_strict60=previous[5]==previous[6]==previous[2]-1 and previous[7]==0 and 59.9<=previous[6]/seconds<=60.1)
                key=str(previous[0])
                if key in closed:assert closed[key]==point
                else:
                    closed[key]=point;result['checks'].append(dict(name='closed_epoch_'+key,measurement=point))
                    result['closed_epochs']=closed;write('progress.json',result)
            if time.monotonic()-start>=60 and len(closed)>=6:break
    assert len(closed)>=6,closed
    result['observation_seconds']=time.monotonic()-start
    result['stage_warmup_logic_ticks']=120
    result['performance_window_boundary']='60 seconds of the ordinary three-stage10s cycle; phase meters exclude their original first120 warmup ticks'
    result['closed_epochs']=closed

    # Showcase uses PACIFIST, one of the two repaired entries. Normal-role
    # board pixels and full host renders qualify its shared drawing lookup;
    # this window separately remeasures the actual moving showcase workload.
    leave(state);result['finished']=True
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
