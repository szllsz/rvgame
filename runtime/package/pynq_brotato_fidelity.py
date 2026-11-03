"""Own one temporary candidate and its DDR until all CPU/display traffic drains."""
import hashlib
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

class TrialExit(Exception):pass

def write(name,value):
    # Only the owner writes these run-local files; caller never substitutes an unverified package.
    (BASE/name).write_text(json.dumps(value,indent=2)+'\n')

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
    for name,digest in manifest['files'].items():
        assert hashlib.sha256((BASE/name).read_bytes()).hexdigest()==digest,name
    assert Device.active_device is not None
    test_input='--test-input' in sys.argv
    scripted='--scripted-run' in sys.argv
    assert not scripted or test_input,'Scripted controller requires explicit test input'
    assert not manifest.get('input_replay') or test_input,'Replay image requires explicit test input'
    assert not manifest.get('board_scenario') or test_input,'Stress image requires explicit test input'
    mailbox=None;stop_requested=False;final_requested=None;runtime_fault=None
    session_menu=manifest.get('session_menu')
    voxel_ui=manifest.get('telemetry_kind')=='voxel_probe_v1'
    assert not voxel_ui or (test_input and not scripted and not session_menu),'Voxel probe requires explicit static automated test'
    assert manifest.get('session_start','menu') in ('menu','game')
    menu_ui=(bool(session_menu) and manifest.get('session_start','menu')=='menu') or manifest.get('telemetry_kind')=='menu_ui_v1'
    assert not session_menu or (not scripted and not manifest.get('input_replay') and not manifest.get('board_scenario') and manifest.get('telemetry_transport')=='state_ack_v2')
    assert not menu_ui or session_menu or (test_input and not scripted and not manifest.get('input_replay'))
    if manifest.get('telemetry_transport') in ('state_ack_v1','state_ack_v2'):
        from bt_mailbox import Mailbox,valid_menu_sample,valid_voxel_sample
        mailbox=Mailbox()
    def stop(signum,frame):
        nonlocal stop_requested
        if mailbox is not None and ready:stop_requested=True
        else:raise KeyboardInterrupt
    signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
    owner=frame=control=None;hp_maps=[];allocation=None;error=None;ready=False
    segment=0;switch_uncertain=False
    last_sample=0.0;first=True;blocked=0
    try:
        Overlay(str(BASE/manifest['bit']))
        control=MMIO(0x43c00000,0x1000);control.write(0x40,0);control.write(0x0c,0)
        if mailbox is not None:control.write(0x0c,0x100)
        assert control.read(0x18)==(480<<16|640)
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
        for register,value in ((0x6c,physical),(0x70,1)):
            control.write(register,value);assert control.read(register)==value
        image=(BASE/(session_menu['application'] if session_menu and menu_ui else manifest['application'])).read_bytes();allocation=owner.load(image)
        write('allocation.json',{'pid':os.getpid(),'program':allocation,'display':{'physical_base':physical,'allocated_bytes':1048576},
                                  'test_input':test_input,'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip()})
        deadline=time.monotonic()+30;startup_sample=0
        while control.read(0x48)>>16!=manifest.get('startup_page',0x350) or control.read(0x7c)<1:
            now=time.monotonic()
            if now-startup_sample>=.5:
                startup_sample=now
                sample={'monotonic':now,'registers':{hex(k):control.read(k) for k in (0x10,0x14,0x28,0x48,0x60,0x74,0x78,0x7c)}}
                with (BASE/'startup.jsonl').open('a') as log:log.write(json.dumps(sample)+'\n')
            if control.read(0x60)&1 or control.read(0x74)&1:raise RuntimeError('DDR fault while starting candidate')
            if time.monotonic()>deadline:raise TimeoutError('Candidate title did not start')
            time.sleep(.01)
        assert control.read(0x78)==0
        if mailbox is not None:
            # First copy targets bank1. Wait for both banks and a stable read;
            # the former copies>=1 observer could save an untouched black bank0.
            deadline=time.monotonic()+2
            while True:
                frame.invalidate();first_image=bytes(memoryview(frame).cast('B')[:307200])
                frame.invalidate();second_image=bytes(memoryview(frame).cast('B')[:307200])
                if control.read(0x7c)>=3 and first_image==second_image:break
                if time.monotonic()>deadline:raise TimeoutError('No stable startup framebuffer')
                time.sleep(.005)
            (BASE/'title.rgb332').write_bytes(second_image)
        else:
            frame.invalidate();(BASE/'title.rgb332').write_bytes(memoryview(frame).cast('B')[:307200])
        write('ready.json',{'ready':True,'pid':os.getpid(),'geometry':[640,480],'allocation':allocation,
                            'frame_base':physical,'usb_gamepad':str(find_pad()),'underflows':control.read(0x78),'test_input':test_input,
                            'telemetry_transport':manifest.get('telemetry_transport','framebuffer_v1'),
                            'session_start':manifest.get('session_start','menu')})
        ready=True;notify('READY=1\nSTATUS=Brotato temporary candidate ready');print('FIDELITY_TEMPORARY_READY',flush=True)
        ready_time=time.monotonic();latest=None;last_input=None
        def switch_application(to_menu,final_sample):
            nonlocal mailbox,menu_ui,segment,allocation,first,blocked,last_input,latest,last_sample,ready_time,final_requested,switch_uncertain
            started=time.monotonic();image_name=session_menu['application'] if to_menu else manifest['application']
            image=(BASE/image_name).read_bytes()
            assert hashlib.sha256(image).hexdigest()==manifest['files'][image_name]
            receipt={'segment':segment,'from':'menu' if menu_ui else 'game','to':'menu' if to_menu else 'game',
                     'begin':started,'final':final_sample,'pid':os.getpid(),'frame_base':physical,
                     'overlay_reloaded':False,'display_buffer_retained':True}
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
            owner.close();assert owner.buffer is None
            receipt['old_program_freed']=True;switch_uncertain=False
            # Frozen final producers submit no more GPU commands. HP1 idle
            # proves the old bitmap reads/copies have retired; subsequent scan
            # reads address only our retained display buffer. Keep scan enabled
            # (disabling it resets frame counters and line-cache validity).
            assert control.read(0x70)==1;receipt['display_kept_enabled']=True
            control.write(0x0c,0x100);mailbox=Mailbox();final_requested=None
            menu_ui=to_menu;segment+=1
            initial_copies=control.read(0x7c);allocation=owner.load(image)
            deadline=time.monotonic()+30;page=0x3a0 if to_menu else 0x350
            while control.read(0x48)>>16!=page or ((control.read(0x7c)-initial_copies)&0xffffffff)<2:
                if control.read(0x60)&1 or control.read(0x74)&1 or control.read(0x78):raise RuntimeError('Switch hardware fault')
                if time.monotonic()>deadline:raise TimeoutError('Next application did not initialize both display banks')
                time.sleep(.005)
            first=True;blocked=0;last_input=latest=None;last_sample=0;ready_time=time.monotonic()
            receipt.update(ready=ready_time,allocation=allocation,pass_=True)
            write('switch_%03d.json'%(segment-1),receipt)
            write('session_current.json',{'segment':segment,'menu':menu_ui,'pid':os.getpid(),'allocation':allocation,'ready':ready_time})
        def poll_once(word):
            nonlocal last_sample,first,blocked,latest,last_input,final_requested,stop_requested,runtime_fault
            now=time.monotonic()
            physical_word=word
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
            elif test_input and not scripted:
                try:
                    injected=json.loads((BASE/'test_input.json').read_text())
                    if now<injected['until']<now+1:word=0x10000|(injected['mask']&4095)
                except FileNotFoundError:pass
            if first:blocked=word&4095;first=False
            blocked&=word&4095
            if control.read(0x60)&1 or control.read(0x74)&1:raise RuntimeError('Runtime DDR fault')
            if control.read(0x78):raise RuntimeError('Display underflow')
            sample=None
            if mailbox is not None:
                position=control.read(0x48)
                if (stop_requested or position>>16==0x35e) and final_requested is None:
                    final_requested=now;control.write(0x40,0)
                    control.write(0x0c,mailbox.request_final(position))
                if final_requested is not None and now-final_requested>20:
                    raise TimeoutError('Final telemetry did not drain; run fails')
                counter_start=time.monotonic();copies=control.read(0x7c);counter_end=time.monotonic()
                stamp={'monotonic':(counter_start+counter_end)/2,'counter_start':counter_start,'counter_end':counter_end,
                       'scan':control.read(0x10),'copies':copies,'underflows':control.read(0x78),
                       'ddr_status':control.read(0x60),'display_status':control.read(0x74),'position':position}
                sample=mailbox.accept(position,stamp)
                control.write(0x0c,0x100|mailbox.phase|(2 if final_requested is not None else 0))
                if sample is not None:
                    last_sample=now;words=sample['telemetry'];structures=sample['structure_telemetry']
                if now-max(last_sample,ready_time)>20:raise TimeoutError('Telemetry stalled; run fails')
            elif now-last_sample>=.5:
                last_sample=now;frame.invalidate();view=memoryview(frame).cast('B')
                raw=bytes(view[307200-256:307200]);words=[int.from_bytes(raw[i:i+4],'little') for i in range(0,256,4)]
                counter_start=time.monotonic();copies=control.read(0x7c);counter_end=time.monotonic()
                sample={'monotonic':(counter_start+counter_end)/2,'counter_start':counter_start,'counter_end':counter_end,
                        'scan':control.read(0x10),'copies':copies,'underflows':control.read(0x78),
                        'ddr_status':control.read(0x60),'display_status':control.read(0x74),'position':control.read(0x48),'telemetry':words}
                extension=bytes(view[307200-320:307200-256])
                structures=[int.from_bytes(extension[i:i+4],'little') for i in range(0,64,4)]
                sample['structure_telemetry']=structures
            if sample is not None and sample['structure_telemetry'][0]==0x42545339:
                meta=sample['structure_telemetry']
                assert manifest.get('showcase_telemetry') and not menu_ui and not sample.get('mailbox_final')
                assert meta[2]==1 and meta[12]==20261028 and meta[15]==(meta[1]^0xffffffff)
                assert meta[6]+meta[7]+meta[8]<=192 and meta[9]<=512 and all(v==0 for v in sample['telemetry'])
                if meta[14]:runtime_fault='Showcase auxiliary reported a fault';stop_requested=True
                if session_menu:sample.update(session_segment=segment,session_app='game')
                with (BASE/'showcase_samples.jsonl').open('a') as log:log.write(json.dumps(sample)+'\n')
                sample=None
            if sample is not None:
                if session_menu:sample.update(session_segment=segment,session_app='menu' if menu_ui else 'game')
                # Both transports retain the original complete words and checks.
                sample['coherent']=words[0]==0x53595331 and words[16]==0x42544632 and words[15]==(words[1]^0xffffffff) and words[63]==words[15]
                sample['structure_coherent']=sample['coherent'] and structures[0]==0x42545331 and structures[1]==words[1] and structures[15]==words[15]
                if menu_ui:
                    sample['menu_ui']=True
                    sample['coherent']=sample['structure_coherent']=valid_menu_sample(sample)
                if voxel_ui:
                    sample['voxel_probe']=True
                    sample['coherent']=sample['structure_coherent']=valid_voxel_sample(sample)
                if mailbox is not None and not (sample['coherent'] and sample['structure_coherent']):raise RuntimeError('Invalid mailbox snapshot')
                if sample['coherent']:latest=words
                with (BASE/'samples.jsonl').open('a') as log:log.write(json.dumps(sample)+'\n')
                if not menu_ui and sample['coherent'] and (words[17] or words[34] or words[36] or (words[5]>>16)&8):
                    runtime_fault='Candidate reported capacity/tick/stack/GPU fault';stop_requested=True
                completion=completed_replay(manifest,sample)
                if completion and mailbox is None:write('replay_completed.json',completion);raise TrialExit()
                if completion and not sample.get('mailbox_final'):stop_requested=True
                if sample.get('mailbox_final'):
                    write('mailbox_final.json',sample)
                    if voxel_ui:
                        # Prototype is frozen after its last completed draw.
                        # Capture only a stable completed bank before hold/free.
                        capture_deadline=time.monotonic()+2
                        while True:
                            copies=control.read(0x7c);offset=(copies&1)*0x80000
                            frame.invalidate();first_frame=bytes(memoryview(frame).cast('B')[offset:offset+307200])
                            frame.invalidate();last_frame=bytes(memoryview(frame).cast('B')[offset:offset+307200])
                            if first_frame==last_frame and copies==control.read(0x7c):break
                            assert time.monotonic()<capture_deadline,'Voxel final framebuffer not stable'
                        (BASE/'voxel_final.rgb332').write_bytes(last_frame)
                        write('voxel_final_frame.json',{'copies':copies,'bank':copies&1,'bytes':len(last_frame),'sha256':hashlib.sha256(last_frame).hexdigest(),'final_checksum':sample['mailbox_checksum']})
                    if runtime_fault:raise RuntimeError(runtime_fault)
                    if completion:write('replay_completed.json',completion)
                    if session_menu:
                        write('segment_%03d_final.json'%segment,sample)
                        if not stop_requested and (not menu_ui or words[2]==1):
                            switch_application(not menu_ui,sample)
                            return 0
                    raise TrialExit()
            if control.read(0x48)>>16==0x356:raise TrialExit()
            if word!=last_input:
                with (BASE/'input_trace.jsonl').open('a') as log:
                    log.write(json.dumps({'monotonic':now,'physical_word':physical_word,'forwarded_word':word&~blocked,'page':control.read(0x48)>>16,'scripted':scripted,'test_input':test_input})+'\n')
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
    finally:
        cleanup={'pass':False,'pid':os.getpid(),'error':error,'program_freed':False,'display_freed':False,'hp_restored':False}
        try:
            if switch_uncertain:raise RuntimeError('Switch retirement uncertain; keep owner and buffers')
            if owner is not None:
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
            cleanup['hp_restored']=True;cleanup['pass']=True;write('cleanup.json',cleanup)
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

if __name__=='__main__':main()
