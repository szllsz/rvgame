"""Own one temporary candidate and its DDR until all CPU/display traffic drains."""
import hashlib
import gc
import json
import os
from pathlib import Path
import signal
import socket
import sys
import time

BASE=Path(__file__).resolve().parent
sys.path.insert(0,str(BASE))
from pynq import MMIO,Overlay,allocate
from pynq.pl_server.device import Device
from pynq_ddr import DdrProgram,configure_hp0_32
from pynq_gamepad import find_pad,run_input
from native_session import NativeAudio,NativeTransition,game_route,valid_touhou_sample,main_menu_shortcut,prepare_native_error_retirement
from native_session import PLAIN_EXIT_PAGES,prepare_plain_native_retirement
from scarlet_pcm import ScarletMusic
from trace_writer import TraceWriter

# run_input otherwise imports this module after PCM has already started.
# Prepare its Python code before releasing the CPU; input still has one owner.
input_preload_started=time.monotonic()
import replay_input
input_preload_seconds=time.monotonic()-input_preload_started

class TrialExit(Exception):pass

def write(name,value):
    # Only the owner writes these run-local files; caller never substitutes an unverified package.
    temporary=BASE/(name+'.next');temporary.write_text(json.dumps(value,indent=2)+'\n');os.replace(temporary,BASE/name)

def notify(message):
    address=os.environ.get('NOTIFY_SOCKET')
    if address:
        address=bytes([0])+address[1:].encode() if address.startswith('@') else address
        with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as status:
            status.connect(address);status.sendall(message.encode())

def completed_replay(manifest,sample):
    replay=manifest.get('input_replay')
    if not replay or not sample['coherent']:return None
    words=sample['telemetry'];expected=replay['expected']
    if words[61]<replay['actions']:return None
    assert words[61]==words[9]==replay['actions'] and words[8]==expected['state_fnv1a32'],'Replay state hash mismatch'
    assert words[2]&255==expected['mode'] and words[3]==expected['wave'] and words[19]==expected['normal_ticks']
    assert words[20]==0 and words[17]==0 and words[34]==0 and words[36]==0 and words[12]==expected['kills'] and words[47]==expected['damage_taken']
    return {'pass':True,'telemetry':words,'monotonic':sample['monotonic'],'trace_sha256':replay['sha256'],
            'application_sha256':manifest['files'][manifest['application']],'pid':os.getpid()}

def main():
    manifest=json.loads((BASE/'candidate_manifest.json').read_text())
    assert manifest.get('native_geometry')==[800,600]
    assert manifest.get('session_menu') and manifest.get('native_suite800')
    images={};music_data={}
    music_specs=manifest.get('native_music') or {'touhou':manifest.get('scarlet_music')}
    assert set(music_specs)<=set(('game','touhou')) and 'touhou' in music_specs
    for spec in music_specs.values():assert spec and spec['sample_rate']==32000
    for name,digest in manifest['files'].items():
        content=(BASE/name).read_bytes()
        assert hashlib.sha256(content).hexdigest()==digest,name
        if name.endswith('.bin') or name.startswith('transition_'):images[name]=content
        for app,spec in music_specs.items():
            if name==spec['file']:music_data[app]=content
    assert set(music_data)==set(music_specs)
    ui_effects={}
    if manifest.get('ui_effects'):
        from scarlet_ui_pcm import ScarletUiMusic
        for kind,spec in manifest['ui_effects'].items():
            assert spec['sample_rate']==32000 and manifest['files'][spec['file']]==spec['sha256']
            ui_effects[kind]=((BASE/spec['file']).read_bytes(),spec['sha256'])
    musics={}
    for app,spec in music_specs.items():
        assert manifest['files'][spec['file']]==spec['sha256']
        if app=='touhou' and ui_effects:musics[app]=ScarletUiMusic(music_data[app],spec['loop_start'],spec['loop_end'],spec['sha256'],ui_effects)
        else:musics[app]=ScarletMusic(music_data[app],spec['loop_start'],spec['loop_end'],spec['sha256'])
    touhou_mode=0
    touhou_music={1:musics['touhou']}
    if manifest.get('touhou_frontend'):
        spec=manifest['touhou_title_music'];data=(BASE/spec['file']).read_bytes()
        assert manifest['files'][spec['file']]==spec['sha256'] and spec['sample_rate']==32000
        touhou_music[0]=ScarletUiMusic(data,spec['loop_start'],spec['loop_end'],spec['sha256'],ui_effects) if ui_effects else ScarletMusic(data,spec['loop_start'],spec['loop_end'],spec['sha256'])
        musics['touhou']=touhou_music[0]
    assert Device.active_device is not None
    test_input='--test-input' in sys.argv
    assert not ('--fast-switch' in sys.argv and '--full-switch-telemetry' in sys.argv)
    compact_exit=manifest.get('compact_exit_abi')==1 and '--full-switch-telemetry' not in sys.argv and (not test_input or '--fast-switch' in sys.argv)
    assert '--fast-switch' not in sys.argv or compact_exit,'Fast switch requires the matching producer ABI'
    debug_footer='--capture-metadata' in sys.argv
    assert not debug_footer or test_input,'Framebuffer metadata requires explicit automated test input'
    scripted='--scripted-run' in sys.argv
    assert not scripted or test_input,'Scripted controller requires explicit test input'
    assert not manifest.get('input_replay') or test_input,'Replay image requires explicit test input'
    assert not manifest.get('board_scenario') or test_input,'Stress image requires explicit test input'
    mailbox=None;stop_requested=False;final_requested=None;runtime_fault=None
    session_menu=manifest.get('session_menu')
    voxel_ui=manifest.get('telemetry_kind')=='voxel_probe_v1'
    assert not voxel_ui or (test_input and not scripted and not session_menu),'Voxel probe requires explicit static automated test'
    assert manifest.get('session_start','menu') in ('menu','game','touhou')
    menu_ui=('--start-menu' in sys.argv or (bool(session_menu) and manifest.get('session_start','menu')=='menu')) or manifest.get('telemetry_kind')=='menu_ui_v1'
    active='menu' if menu_ui else manifest.get('session_start','game');menu_request=None;classic_choice=None
    assert not session_menu or (not scripted and not manifest.get('input_replay') and not manifest.get('board_scenario') and manifest.get('telemetry_transport')=='state_ack_v2')
    assert not menu_ui or session_menu or (test_input and not scripted and not manifest.get('input_replay'))
    if manifest.get('telemetry_transport') in ('state_ack_v1','state_ack_v2'):
        from bt_mailbox import Mailbox,valid_menu_sample,valid_voxel_sample
        mailbox=Mailbox(compact_final=compact_exit)
    def stop(signum,frame):
        nonlocal stop_requested
        if ready:stop_requested=True
        else:raise KeyboardInterrupt
    signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
    owner=frame=control=None;hp_maps=[];allocation=None;error=None;ready=False
    error_retirement=None;retirement_failed=False
    segment=0;switch_uncertain=False
    traces=TraceWriter(BASE)
    candidate=BASE.parent.parent if BASE.parent.name=='runs' else BASE.parent if BASE.name=='package' else BASE
    audio=NativeAudio(candidate/'input_state/audio_settings.json')
    last_sample=0.0;first=True;blocked=0
    plain_capture_seen=set()
    music_last_snapshot=0.0
    music_last_poll=0.0
    def service_music(final=False):
        nonlocal music_last_snapshot,music_last_poll
        if active not in musics:return None
        music=musics[active]
        started=time.monotonic();gap=started-music_last_poll if music_last_poll else 0.0;music_last_poll=started
        if gap>.04:traces.append('music_refill_gaps.jsonl',dict(monotonic=started,gap_seconds=gap,application=active))
        submitted=music.poll(control)
        elapsed=time.monotonic()-started
        if final or started-music_last_snapshot>=1:
            sample=music.snapshot(control)
            if final and sample is None:raise RuntimeError('Missing frozen music CRC witness')
            if sample is not None:
                spec=manifest['touhou_title_music'] if active=='touhou' and manifest.get('touhou_frontend') and not touhou_mode else music_specs[active]
                sample.update(monotonic=started,final=final,refill_words=submitted,refill_seconds=elapsed,application=active,music_file=spec['file'],music_sha256=spec['sha256'])
                traces.append('music_samples.jsonl',sample)
                music_last_snapshot=started
                if sample['underflows']:raise RuntimeError('Music FIFO underflow')
                if final and (not sample['consumed_samples'] or not sample['serial_frames']):raise RuntimeError('Music never reached serialized output')
                return sample
        return None
    try:
        Overlay(str(BASE/manifest['bit']))
        control=MMIO(0x43c00000,0x1000);control.write(0x40,0);control.write(0x0c,0)
        if mailbox is not None:control.write(0x0c,0x100)
        assert control.read(0x18)==(600<<16|800)
        owner=DdrProgram(control);owner.hold()
        # This reads the accepted S1 contract. It does not rerun any CPU/capacity qualification.
        assert control.read(0x38)==4096
        write('s1_contract.json',{'reported_bytes':4096,'capacity_requalified':False,
                                 'trampoline_policy':'Only DdrProgram.load writes its two necessary boot words'})
        hp,previous=configure_hp0_32(owner);hp_maps.append((hp,previous))
        hp=MMIO(0xf8009000,0x20);previous={offset:hp.read(offset) for offset in (0,0x14)};hp_maps.append((hp,previous))
        for offset,value in previous.items():
            hp.write(offset,value|1);assert hp.read(offset)==value|1
        frame=allocate(shape=(1048576,),dtype='u1');physical=int(frame.physical_address)
        assert physical&63==0 and 0<=physical<=0x3ff00000
        frame[:]=0;frame.flush()
        transition=NativeTransition(control,frame,images)
        for register,value in ((0x6c,physical),(0x70,1)):
            control.write(register,value);assert control.read(register)==value
        image=images[session_menu['application'] if session_menu and menu_ui else manifest['applications']['touhou'] if active=='touhou' else manifest['application']]
        audio.publish_boot(control)
        if active in musics:musics[active].begin(segment)
        pad_description=str(find_pad())
        # Overlay/input imports leave a large cyclic-GC graph. Collect while
        # held, before PCM starts; keep normal automatic GC and fault checks.
        assert control.read(0x28)==1,'Startup collection requires held CPU'
        gc_counts_before=gc.get_count();gc_started=time.monotonic()
        gc_startup_collected=gc.collect(2)
        gc_startup_seconds=time.monotonic()-gc_started
        initial_copies=control.read(0x7c);allocation=owner.load(image)
        service_music()
        traces.write_json('allocation.json',{'pid':os.getpid(),'program':allocation,'display':{'physical_base':physical,'allocated_bytes':1048576},
                                  'test_input':test_input,'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip()})
        deadline=time.monotonic()+30;startup_sample=0
        startup_pages=(0x3d0,0x3e0,0x3e1) if active=='touhou' else (0x3a0 if menu_ui else manifest.get('startup_page',0x350),)
        while control.read(0x48)>>16 not in startup_pages or ((control.read(0x7c)-initial_copies)&0xffffffff)<2:
            service_music()
            now=time.monotonic()
            if now-startup_sample>=.5:
                startup_sample=now
                sample={'monotonic':now,'registers':{hex(k):control.read(k) for k in (0x10,0x14,0x28,0x48,0x60,0x74,0x78,0x7c)}}
                traces.append('startup.jsonl',sample)
            if control.read(0x60)&1 or control.read(0x74)&1:raise RuntimeError('DDR fault while starting candidate')
            if time.monotonic()>deadline:raise TimeoutError('Candidate title did not start')
            time.sleep(.01)
        assert control.read(0x78)==0
        if mailbox is not None:
            # First copy targets bank1. Wait for both banks and a stable read;
            # the former copies>=1 observer could save an untouched black bank0.
            deadline=time.monotonic()+2
            while True:
                frame.invalidate();first_image=bytes(memoryview(frame).cast('B')[:480000])
                frame.invalidate();second_image=bytes(memoryview(frame).cast('B')[:480000])
                if ((control.read(0x7c)-initial_copies)&0xffffffff)>=3 and first_image==second_image:break
                if time.monotonic()>deadline:raise TimeoutError('No stable startup framebuffer')
                time.sleep(.005)
            traces.write_bytes('title.rgb332',second_image)
        else:
            frame.invalidate();traces.write_bytes('title.rgb332',bytes(memoryview(frame).cast('B')[:480000]))
        service_music()
        traces.write_json('ready.json',{'ready':True,'pid':os.getpid(),'geometry':[800,600],'allocation':allocation,
                            'frame_base':physical,'usb_gamepad':pad_description,'underflows':control.read(0x78),'test_input':test_input,
                            'telemetry_transport':manifest.get('telemetry_transport','framebuffer_v1'),
                            'session_start':active,'persistent_session':True,'compact_exit':compact_exit,
                            'input_preload_seconds':input_preload_seconds,
                            'gc_startup_seconds':gc_startup_seconds,'gc_startup_collected':gc_startup_collected,
                            'gc_counts_before':gc_counts_before,'gc_counts_after':gc.get_count()})
        traces.write_json('session_current.json',{'segment':segment,'active':active,'menu':menu_ui,'pid':os.getpid(),'frame_base':physical,'ready':time.monotonic()})
        service_music()
        ready=True;notify('READY=1\nSTATUS=Brotato temporary candidate ready');print('FIDELITY_TEMPORARY_READY',flush=True)
        ready_time=time.monotonic();latest=None;last_input=None
        menu_selection={'game':4,'touhou':5}.get(active,0);ui_previous=0
        def switch_application(target,final_sample,held_buttons=0,next_touhou_mode=0):
            nonlocal mailbox,menu_ui,active,menu_request,segment,allocation,first,blocked,last_input,latest,last_sample,ready_time,final_requested,switch_uncertain,music_last_snapshot
            nonlocal menu_selection,touhou_mode,ui_previous
            started=time.monotonic();image_name=session_menu['application'] if target=='menu' else manifest['application'] if target=='game' else manifest['applications'][target]
            if active=='menu' and target!='menu':
                menu_selection={'coremark':0,'validation':0,'gfx':1,'bitmap':2,'alpha':3,'game':4,'touhou':5}[target]
            if active in PLAIN_EXIT_PAGES:
                # Scan fetches may become busy again between two PS reads.
                # Reuse the bounded producer-exit proof; HOLD then waits for
                # any scan fetch, with no unfinished native frame to block it.
                plain_retirement=prepare_plain_native_retirement(control,active)
                assert plain_retirement['kind']=='plain_exit' and plain_retirement['exit_page']==PLAIN_EXIT_PAGES[active]
            # Immutable bytes were SHA checked and cached before the first run.
            image=images[image_name]
            receipt={'segment':segment,'from':active,'to':target,'request_begin':final_requested if final_requested is not None else started,
                     'begin':started,'final':final_sample,'pid':os.getpid(),'frame_base':physical,
                     'overlay_reloaded':False,'display_buffer_retained':True,'immutable_image_cached':True,
                     'cached_program_bytes':sum(len(data) for name,data in images.items() if name.endswith('.bin'))}
            if active in PLAIN_EXIT_PAGES:receipt['plain_retirement']=plain_retirement
            audio.capture(control,active,final_sample)
            receipt['music_final']=service_music(final=True)
            receipt['audio_preferences']=audio.value
            transition.show(target,2)
            receipt['transition_banner_updates']=transition.updates
            # Retain the display allocation, but retire all CPU, renderer and
            # HP1 traffic before freeing the old ROM. Uncertain drain must keep
            # every owner/buffer alive, including on the exception path.
            switch_uncertain=True
            owner.hold();control.write(0x40,0);control.write(0x0c,0)
            start=control.read(0x10);deadline=time.monotonic()+.5
            while ((control.read(0x10)-start)&0xffffffff)<3:
                if time.monotonic()>deadline:raise TimeoutError('Switch final frame edges did not retire')
                time.sleep(.001)
            receipt['held_final_frames']=True
            assert control.read(0x70)==1
            deadline=time.monotonic()+3
            while control.read(0x74)&2:
                if time.monotonic()>deadline:raise TimeoutError('Switch HP1 drain uncertain')
                time.sleep(.001)
            receipt['hp1_drained']=True;receipt['hp1_idle_observed']=time.monotonic()
            if test_input and menu_ui and final_sample is not None and not final_sample.get('retirement_only'):
                copies=control.read(0x7c);offset=(copies&1)*0x80000;frame.invalidate()
                is_loading=bool(final_sample['structure_telemetry'][5]&0x100)
                name=('loading_frozen_' if is_loading else 'menu_frozen_')+'%03d.rgb332'%segment
                (BASE/name).write_bytes(bytes(memoryview(frame).cast('B')[offset:offset+480000]))
                receipt['loading_frozen_frame' if is_loading else 'menu_frozen_frame']=name
            owner.close();assert owner.buffer is None
            receipt['old_program_freed']=True;switch_uncertain=False
            # Frozen final producers submit no more GPU commands. HP1 idle
            # proves the old bitmap reads/copies have retired; subsequent scan
            # reads address only our retained display buffer. Keep scan enabled
            # (disabling it resets frame counters and line-cache validity).
            assert control.read(0x70)==1;receipt['display_kept_enabled']=True
            # Scan fetch can become busy again after the drain read. Retry
            # this guarded footer while held; never lose it in that idle gap.
            transition.reset();banner_deadline=time.monotonic()+.5
            while not transition.show(target,2):
                if time.monotonic()>banner_deadline:raise TimeoutError('Stopped-producer transition footer remained busy')
                time.sleep(.001)
            receipt['transition_banner_updates']=transition.updates
            if test_input and target=='menu' and transition.updates:
                frame.invalidate();name='return_banner_%03d.rgb332'%segment
                traces.write_bytes(name,bytes(memoryview(frame).cast('B')[520*800:600*800]));receipt['return_banner_file']=name
            control.write(0x0c,0x104 if target not in ('menu','game','touhou') and debug_footer else 0x100)
            mailbox=Mailbox(compact_final=compact_exit) if target in ('menu','game','touhou') else None;final_requested=None
            active=target;menu_ui=target=='menu';menu_request=None;segment+=1
            if active=='touhou' and manifest.get('touhou_frontend'):
                assert next_touhou_mode in (0,1)
                touhou_mode=next_touhou_mode;musics['touhou']=touhou_music[touhou_mode]
            if target in musics:musics[target].begin(segment)
            music_last_snapshot=0.0
            if target in ('menu','game','touhou'):audio.publish_boot(control,menu_selection if target=='menu' else touhou_mode if target=='touhou' and manifest.get('touhou_frontend') else 0)
            initial_copies=control.read(0x7c);allocation=owner.load(image)
            deadline=time.monotonic()+30;page={'menu':0x3a0,'game':0x350,'touhou':0x3d0,'coremark':0x360,'validation':0x360,'gfx':0x370,'bitmap':0x380,'alpha':0x390}[target]
            valid_pages=(0x360,0x361) if target in ('coremark','validation') else (0x3d0,0x3e0,0x3e1) if target=='touhou' else (page,)
            while control.read(0x48)>>16 not in valid_pages or ((control.read(0x7c)-initial_copies)&0xffffffff)<2:
                service_music()
                if control.read(0x60)&1 or control.read(0x74)&1 or control.read(0x78):raise RuntimeError('Switch hardware fault')
                if time.monotonic()>deadline:raise TimeoutError('Next application did not initialize both display banks')
                time.sleep(.005)
            # Only block the key held across this transition. A fresh confirm
            # arriving after readiness must not be swallowed as an initial key.
            first=False;blocked=held_buttons&4095;ui_previous=0;last_input=latest=None;last_sample=0;ready_time=time.monotonic()
            control.write(0x40,held_buttons&0x10000)
            receipt.update(ready=ready_time,total_seconds=ready_time-receipt['request_begin'],allocation=allocation,pass_=True)
            transition.reset()
            # The new CPU may already be playing PCM. SD publication must not
            # block its sole MMIO refill thread for a complete FIFO horizon.
            service_music()
            traces.write_json('switch_%03d.json'%(segment-1),receipt)
            traces.write_json('session_current.json',{'segment':segment,'active':active,'menu':menu_ui,'pid':os.getpid(),'frame_base':physical,'allocation':allocation,'ready':ready_time})
            service_music()
        def poll_once(word):
            nonlocal last_sample,first,blocked,latest,last_input,final_requested,stop_requested,runtime_fault,menu_request,classic_choice,ui_previous
            service_music()
            now=time.monotonic()
            physical_word=word;metadata_requested=False
            if voxel_ui:word=0x10000  # Explicit fixed-view benchmark, no physical input claim.
            elif scripted and now-ready_time>.6:
                # UI transitions may occur between .5 s telemetry samples. Never
                # repeat START from a stale SHOP sample after combat has begun.
                mode=(control.read(0x48)>>16)-0x350
                pulse=int((now-ready_time)*5)%2==0
                mask=0
                if mode in (0,2):mask=8 if pulse else 0
                elif mode==1:mask=0 if (manifest.get('board_scenario') or {}).get('showcase') else (128,32,64,16)[((latest[4] if latest else 0)//240)%4]
                elif mode in (7,8):mask=2 if pulse else 0
                elif mode==3:mask=8 if pulse else 0
                word=0x10000|mask
            # Native automated input already comes from ReplaySource through
            # the sole run_input writer. The legacy640 override file is unused
            # here; even a missing SD-card file can block longer than PCM slack.
            if first:blocked=word&4095;first=False
            blocked&=word&4095
            keys=(word&~blocked&4095) if word&0x10000 else 0
            pressed_ui=keys&~ui_previous;ui_previous=keys
            if active=='touhou' and ui_effects and final_requested is None:
                page_ui=control.read(0x48)>>16
                in_ui=page_ui==0x3d0 or page_ui in (0x3e8,0x3e9)
                selectable=in_ui and not (manifest.get('touhou_showcase_only') and page_ui==0x3d0)
                effect='cancel' if pressed_ui&1 else 'confirm' if pressed_ui&(2|8) else 'select' if selectable and pressed_ui&0xf0 else None
                if effect:musics['touhou'].play_ui(effect)
            if control.read(0x60)&1 or control.read(0x74)&1:raise RuntimeError('Runtime DDR fault')
            if control.read(0x78):raise RuntimeError('Display underflow')
            position=control.read(0x48);page=position>>16
            return_shortcut=main_menu_shortcut(active,page,word,blocked)
            if active not in ('game','menu','touhou'):
                if test_input:control.write(0x0c,0x104 if debug_footer or metadata_requested else 0x100)
                if test_input and now-last_sample>=.5:
                    last_sample=now;copies=control.read(0x7c)
                    plain=dict(monotonic=now,session_app=active,session_segment=segment,page=page,
                               copies=copies,scan=control.read(0x10),underflows=control.read(0x78))
                    if debug_footer:
                        frame.invalidate();offset=(copies&1)*0x80000
                        data=bytes(memoryview(frame).cast('B')[offset:offset+480000])
                        if copies==control.read(0x7c):
                            plain['footer']=[int.from_bytes(data[i:i+4],'little') for i in range(480000-64,480000,4)]
                            history=[int.from_bytes(data[i:i+4],'little') for i in range(480000-128,480000-64,4)]
                            if history[0]==0x46505332 and history[1]==plain['footer'][1] and history[15]==(history[1]^0xffffffff):
                                plain['fps_history']=history
                            cap=data[480000-1024:]
                            if int.from_bytes(cap[:4],'little')==0x43415031:
                                plain['capacity_words']=[int.from_bytes(cap[i:i+4],'little') for i in range(0,1024,4)]
                            if active in ('coremark','validation') and page==0x361:
                                plain['cpu_text']=data[800*592:].split(bytes([0]))[0].decode('ascii',errors='replace')
                            if (('cpu_text' in plain) or (plain['footer'][0] in (0x47584631,0x424d5031,0x414c5031) and plain['footer'][10]==1)) and (segment,copies) not in plain_capture_seen:
                                name='native_plain_%03d_%08x.rgb332'%(segment,copies)
                                traces.write_bytes(name,data);plain['image_file']=name;plain_capture_seen.add((segment,copies))
                    traces.append('plain_samples.jsonl',plain)
                if page==0x3ff:raise RuntimeError('Native application fault: '+active)
                if stop_requested or return_shortcut or page==PLAIN_EXIT_PAGES[active] or final_requested is not None:
                    if final_requested is None:final_requested=now
                    if page==PLAIN_EXIT_PAGES[active] and not control.read(0x74)&2:
                        if stop_requested:raise TrialExit()
                        switch_application('menu',None,word);return word&0x10000
                    assert now-final_requested<35,'Plain native exit stalled; quarantine before HOLD'
                    return 0x10004
                return word&~blocked
            if menu_ui and page in (0x3a1,0x3a2,0x3a3,0x3a4) and menu_request is None:
                choice=position&2047
                if page==0x3a3:
                    raise RuntimeError('Removed classic game route')
                elif page==0x3a4:menu_request='game'
                elif page==0x3a2:
                    assert choice==0;menu_request='validation'
                else:
                    assert choice<=5
                    menu_request=game_route(choice-4)[0] if choice>=4 else ('coremark','gfx','bitmap','alpha')[choice]
            sample=None
            if mailbox is not None:
                position=control.read(0x48)
                if (stop_requested or return_shortcut or position>>16==0x35e or menu_request is not None) and final_requested is None:
                    final_requested=now;control.write(0x40,0)
                    control.write(0x0c,mailbox.request_final(position))
                if final_requested is not None and now-final_requested>20:
                    raise TimeoutError('Final telemetry did not drain; run fails')
                counter_start=time.monotonic();copies=control.read(0x7c);counter_end=time.monotonic()
                stamp={'monotonic':(counter_start+counter_end)/2,'counter_start':counter_start,'counter_end':counter_end,
                       'scan':control.read(0x10),'copies':copies,'underflows':control.read(0x78),
                       'ddr_status':control.read(0x60),'display_status':control.read(0x74),'position':position}
                sample=mailbox.accept(position,stamp)
                if final_requested is not None and mailbox.final:
                    transition.show(menu_request if menu_ui else 'menu',1,
                                    mailbox.count if sample is not None else len(mailbox.symbols or []),mailbox.count or 1)
                control.write(0x0c,0x100|mailbox.phase|((6 if compact_exit else 2) if final_requested is not None else 0)|
                              (4 if test_input and metadata_requested and active=='game' else 0))
                if sample is not None:
                    last_sample=now;words=sample['telemetry'];structures=sample['structure_telemetry']
                mailbox.check_liveness(now,ready_time)
            elif now-last_sample>=.5:
                last_sample=now;frame.invalidate();view=memoryview(frame).cast('B')
                raw=bytes(view[480000-256:480000]);words=[int.from_bytes(raw[i:i+4],'little') for i in range(0,256,4)]
                counter_start=time.monotonic();copies=control.read(0x7c);counter_end=time.monotonic()
                sample={'monotonic':(counter_start+counter_end)/2,'counter_start':counter_start,'counter_end':counter_end,
                        'scan':control.read(0x10),'copies':copies,'underflows':control.read(0x78),
                        'ddr_status':control.read(0x60),'display_status':control.read(0x74),'position':control.read(0x48),'telemetry':words}
                extension=bytes(view[480000-320:480000-256])
                structures=[int.from_bytes(extension[i:i+4],'little') for i in range(0,64,4)]
                sample['structure_telemetry']=structures
            if sample is not None and sample['structure_telemetry'][0]==0x494e5031:
                meta=sample['structure_telemetry']
                assert test_input and active=='game' and not sample.get('mailbox_final')
                assert meta[15]==(meta[1]^0xffffffff) and meta[8] in (0,1) and meta[13]<=10
                assert all(v==0 for v in sample['telemetry'])
                sample.update(session_segment=segment,session_app='game',input_diagnostic=True)
                traces.append('input_samples.jsonl',sample)
                sample=None
            if sample is not None and sample['structure_telemetry'][0]==0x42545339:
                meta=sample['structure_telemetry']
                assert manifest.get('showcase_telemetry') and not menu_ui and not sample.get('mailbox_final')
                assert meta[2] in (1,2,3,4) and meta[12]==20261028 and meta[15]==(meta[1]^0xffffffff)
                assert meta[6]+meta[7]+meta[8]<=192 and meta[9]<=512
                if meta[2]==1:assert all(v==0 for v in sample['telemetry'])
                elif meta[2]==2:
                    data=sample['telemetry'];assert data[:3]==[0x42575332,20,5] and data[5]==2 and data[3]+data[4]<=2 and all(v==0 for v in data[6:])
                else:
                    data=sample['telemetry'];assert data[:3]==[0x42575333 if meta[2]==3 else 0x42575334,20,5] and data[5]==2 and data[3]+data[4]<=2
                    assert meta[13]==1 and 76*256<=data[6]<=(2048-76)*256 and 76*256<=data[7]<=(1536-76)*256
                    assert data[10]==1 and all(v==0 for v in data[27 if meta[2]==4 else 11:])
                if meta[14]:runtime_fault='Showcase auxiliary reported a fault';stop_requested=True
                if session_menu:sample.update(session_segment=segment,session_app='game')
                traces.append('showcase_samples.jsonl',sample)
                sample=None
            if sample is not None:
                if session_menu:sample.update(session_segment=segment,session_app=active)
                # Ordinary snapshots retain every field. RTR1 proves only
                # retirement; never publish it as a coherent performance sample.
                sample['coherent']=words[0]==0x53595331 and words[16]==0x42544632 and words[15]==(words[1]^0xffffffff) and words[63]==words[15]
                sample['structure_coherent']=sample['coherent'] and structures[0]==0x42545331 and structures[1]==words[1] and structures[15]==words[15]
                if menu_ui:
                    sample['menu_ui']=True
                    sample['coherent']=sample['structure_coherent']=valid_menu_sample(sample)
                elif active=='touhou':
                    sample['touhou_game']=True
                    sample['coherent']=sample['structure_coherent']=valid_touhou_sample(sample)
                if voxel_ui:
                    sample['voxel_probe']=True
                    sample['coherent']=sample['structure_coherent']=valid_voxel_sample(sample)
                if sample.get('retirement_only'):
                    assert compact_exit and sample['mailbox_final'] and sample['retirement']['application']==active,'Retirement application mismatch'
                    sample['coherent']=sample['structure_coherent']=False
                    sample['retirement_verified']=True
                    if sample['retirement']['fault']:
                        runtime_fault='Retiring application reported a fault';stop_requested=True
                if mailbox is not None and not (sample.get('retirement_verified') or (sample['coherent'] and sample['structure_coherent'])):raise RuntimeError('Invalid mailbox snapshot')
                if sample['coherent']:latest=words
                if test_input and debug_footer and active=='touhou' and not sample.get('mailbox_final') and words[58]&0xffff0000 in (0x46510000,0x46530000):
                    key=(segment,words[58])
                    if key not in plain_capture_seen:
                        copies=control.read(0x7c);frame.invalidate()
                        data=bytes(memoryview(frame).cast('B')[(copies&1)*0x80000:(copies&1)*0x80000+480000])
                        if copies==control.read(0x7c):
                            name='native_frontend_%03d_%08x.rgb332'%key
                            traces.write_bytes(name,data);sample['image_file']=name;plain_capture_seen.add(key)
                traces.append('samples.jsonl',sample)
                if active=='game' and sample['coherent'] and (words[17] or words[34] or words[36] or (words[5]>>16)&8):
                    runtime_fault='Candidate reported capacity/tick/stack/GPU fault';stop_requested=True
                completion=completed_replay(manifest,sample)
                if completion and mailbox is None:write('replay_completed.json',completion);raise TrialExit()
                if completion and not sample.get('mailbox_final'):stop_requested=True
                if sample.get('mailbox_final'):
                    if active=='touhou' and ui_effects:
                        audio_wait_started=time.monotonic();audio_deadline=audio_wait_started+.95
                        while not control.read(0xb4)&(1<<17):
                            service_music()
                            if time.monotonic()>audio_deadline:raise TimeoutError('Final UI audio did not acknowledge pause after CRC')
                            time.sleep(.001)
                        traces.write_json('audio_exit_wait_%03d.json'%segment,dict(seconds=time.monotonic()-audio_wait_started,CRC_verified=True))
                    write('mailbox_final.json',sample)
                    if voxel_ui:
                        # Prototype is frozen after its last completed draw.
                        # Capture only a stable completed bank before hold/free.
                        capture_deadline=time.monotonic()+2
                        while True:
                            copies=control.read(0x7c);offset=(copies&1)*0x80000
                            frame.invalidate();first_frame=bytes(memoryview(frame).cast('B')[offset:offset+480000])
                            frame.invalidate();last_frame=bytes(memoryview(frame).cast('B')[offset:offset+480000])
                            if first_frame==last_frame and copies==control.read(0x7c):break
                            assert time.monotonic()<capture_deadline,'Voxel final framebuffer not stable'
                        (BASE/'voxel_final.rgb332').write_bytes(last_frame)
                        write('voxel_final_frame.json',{'copies':copies,'bank':copies&1,'bytes':len(last_frame),'sha256':hashlib.sha256(last_frame).hexdigest(),'final_checksum':sample['mailbox_checksum']})
                    if runtime_fault:raise RuntimeError(runtime_fault)
                    # Also persist when classic exits this native owner, or a
                    # cooperative stop ends the whole session without a switch.
                    audio.capture(control,active,sample)
                    if active in musics:write('music_segment_%03d_final.json'%segment,service_music(final=True))
                    if completion:write('replay_completed.json',completion)
                    if session_menu:
                        write('segment_%03d_final.json'%segment,sample)
                        if not stop_requested and menu_request!='classic':
                            route=sample['retirement']['route'] if sample.get('retirement_only') else sample['telemetry'][58]-0x46520000
                            if active=='touhou' and manifest.get('touhou_frontend') and route in (1,2):
                                switch_application('touhou',sample,word,1 if route==2 else 0);return word&0x10000
                            switch_application(menu_request if menu_ui else 'menu',sample,word);return word&0x10000
                    raise TrialExit()
            if control.read(0x48)>>16==0x356:raise TrialExit()
            if word!=last_input:
                traces.append('input_trace.jsonl',{'monotonic':now,'physical_word':physical_word,'forwarded_word':word&~blocked,'page':control.read(0x48)>>16,'scripted':scripted,'test_input':test_input})
                last_input=word
            return 0 if final_requested is not None else word&~blocked
        def poll(word):
            result=poll_once(word)
            # Gameplay/input retains the accepted polling cadence. Once the
            # RV32 has frozen its final snapshot, drain at frame-edge cadence
            # without another 20 ms USB sleep between each acknowledgement.
            while mailbox is not None and (stop_requested or final_requested is not None or control.read(0x48)>>16 in (0x35e,0x35f)):
                time.sleep(.001)
                result=poll_once(word)
            return result
        run_input(control,on_poll=poll)
    except (KeyboardInterrupt,TrialExit):
        print('FIDELITY_TEMPORARY_STOP',flush=True)
    except Exception as caught:
        error=repr(caught);write('owner_error.json',{'error':error,'ready':ready});print('FIDELITY_OWNER_ERROR',error,flush=True)
        if owner is not None:
            try:
                refill=(lambda:musics[active].poll(control)) if active in musics else None
                error_retirement=(prepare_plain_native_retirement(control,active) if active in PLAIN_EXIT_PAGES
                                  else prepare_native_error_retirement(control,mailbox,refill))
                write('error_retirement.json',error_retirement)
                if error_retirement['final'] is not None:write('mailbox_final.json',error_retirement['final'])
            except BaseException as retire_error:
                retirement_failed=True
                write('error_retirement.json',dict(safe_to_hold=False,error=repr(retire_error)))
    finally:
        cleanup={'pass':False,'pid':os.getpid(),'error':error,'program_freed':False,'display_freed':False,'hp_restored':False}
        try:
            if switch_uncertain:raise RuntimeError('Switch retirement uncertain; keep owner and buffers')
            if owner is not None:
                if retirement_failed:raise RuntimeError('Native retirement unknown; quarantine before HOLD')
                if error_retirement is None:
                    refill=(lambda:musics[active].poll(control)) if active in musics else None
                    error_retirement=(prepare_plain_native_retirement(control,active) if active in PLAIN_EXIT_PAGES
                                      else prepare_native_error_retirement(control,mailbox,refill))
                assert error_retirement['safe_to_hold']
                owner.hold();control.write(0x40,0)
                if mailbox is not None:control.write(0x0c,0)
                # Retire the final renderer/copy before disabling persistent scan, matching the accepted switch policy.
                start=control.read(0x10);deadline=time.monotonic()+.5
                while ((control.read(0x10)-start)&0xffffffff)<3:
                    if time.monotonic()>deadline:raise TimeoutError('Final renderer frame edges did not retire')
                    time.sleep(.001)
                control.write(0x70,0);assert control.read(0x70)==0
                deadline=time.monotonic()+3
                while control.read(0x74)&2:
                    if time.monotonic()>deadline:raise TimeoutError('Final HP1 traffic did not drain')
                    time.sleep(.001)
                owner.close();cleanup['program_freed']=owner.buffer is None
            else:cleanup['program_freed']=True
            if frame is not None:frame.freebuffer();frame=None
            cleanup['display_freed']=True
            for hp,previous in reversed(hp_maps):
                for offset,value in previous.items():hp.write(offset,value);assert hp.read(offset)==value
            cleanup['hp_restored']=True
            try:traces.close();cleanup['trace_writer_flushed']=True
            except Exception as trace_error:
                error=error or repr(trace_error);cleanup.update(error=error,trace_writer_error=repr(trace_error))
                write('owner_error.json',{'error':error,'ready':ready})
            cleanup['pass']=True;write('cleanup.json',cleanup)
            print('FIDELITY_RESOURCES_RELEASED',flush=True)
        except BaseException as caught:
            cleanup['cleanup_error']=repr(caught);cleanup['quarantined']=True;write('cleanup.json',cleanup)
            print('FIDELITY_QUARANTINED_KEEP_OWNER_AND_BUFFERS',repr(caught),flush=True)
            notify('STATUS=Quarantined: DDR ownership retained; do not kill or start another loader')
            # No overlay reset, free, automatic process kill, or overlapping restore is safe on uncertain drain.
            while True:
                try:time.sleep(30)
                except BaseException:pass
    if error:raise SystemExit(1)
    if classic_choice is not None:write('exit_route.json',{'classic':classic_choice,'pid':os.getpid()})

if __name__=='__main__':main()
