"""Own program and display DDR for the temporary 640x480 graphics/audio trial."""
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time

BASE=Path('/home/xilinx/rvgame')
# Asset paths stay on the preserved classic release; input and loader code
# must come from this candidate, including a fresh classic supervisor child.
sys.path.insert(0,str(Path(__file__).resolve().parent))
if str(BASE) not in sys.path:sys.path.append(str(BASE))
from pynq import MMIO, Overlay, allocate
from pynq.pl_server.device import Device
from pynq_ddr import DdrProgram, configure_hp0_32
from pynq_gamepad import run_input, find_pad, load_images, prepare_loads
from bt_mailbox import Mailbox,valid_menu_sample

class TrialExit(Exception): pass

def launch_classic_game(choice):
    """Own the classic overlay directly; neither game passes through the old home."""
    if choice not in (0,1):raise ValueError('Invalid classic game')
    directory=Path('/home/xilinx/rvgame/system640_classic_20261024_b')
    owner_root=Path(__file__).resolve().parent
    manifest=json.loads((directory/'menu_manifest.json').read_text())
    classic=manifest['classic'];name=('mario','contra')[choice]
    for path,expected in manifest['files'].items():
        if hashlib.sha256((BASE/path).read_bytes()).hexdigest()!=expected:
            raise RuntimeError('Classic preflight hash mismatch: '+path)
    state=subprocess.check_output(['systemctl','show','rvgame.service',
        '--property=ActiveState','--value'],text=True).strip()
    if state!='inactive':raise RuntimeError('Formal owner must be stopped before classic load')
    owner=None;hp=None;previous_hp={};c=None;interrupted=False
    def save(stage,**values):
        (owner_root/'classic_active.json').write_text(json.dumps(dict(
            pid=os.getpid(),game=name,stage=stage,uptime=time.monotonic(),**values),indent=2)+'\n')
    save('loading')
    try:
        Overlay(str(BASE/classic['bit']))
        c=MMIO(0x43c00000,4096)
        if c.read(0)!=0x5247414d:raise RuntimeError('Classic overlay identity mismatch')
        c.write(0x40,0);c.write(0x0c,0)
        owner=DdrProgram(c);hp,previous_hp=configure_hp0_32(owner)
        if choice:owner.load((BASE/classic['contra']).read_bytes())
        else:
            owner.close()
            load_images(c,prepare_loads(c,BASE/classic['mario'],BASE/classic['assets']))
        expected=0x321 if choice else 0x303;deadline=time.monotonic()+45
        while c.read(0x48)>>16!=expected:
            if c.read(0x60)&1:raise RuntimeError('Classic DDR fault')
            if time.monotonic()>deadline:raise TimeoutError('Classic title did not start')
            time.sleep(.01)
        save('ready',position=hex(c.read(0x48)),old_home_used=False)
        first=True;blocked=0
        def poll(word):
            nonlocal first,blocked
            if '--test-input' in sys.argv:
                try:
                    injected=json.loads((owner_root/'test_input.json').read_text())
                    if time.monotonic()<injected['until']<time.monotonic()+1:
                        word=0x10000|(injected['mask']&4095)
                except FileNotFoundError:pass
            page=c.read(0x48)>>16
            if page in (0x306,0x331):raise TrialExit
            if c.read(0x60)&1 or page==0x3ff:raise RuntimeError('Classic runtime fault')
            if first:blocked=word&4095;first=False
            blocked&=word&4095
            word&=~blocked
            # Match the native applications: SELECT returns to the system menu.
            # START remains available to the original game for start/pause.
            if word&4:raise TrialExit
            return word
        try:run_input(c,on_poll=poll)
        except TrialExit:pass
    except KeyboardInterrupt:
        interrupted=True
    finally:
        try:
            if c is not None:c.write(0x40,0)
            if owner is not None:
                try:owner.close()
                except Exception:
                    print('QUARANTINED_KEEP_OWNER_AND_BUFFERS',flush=True)
                    while True:
                        try:time.sleep(30)
                        except BaseException:pass
        finally:
            if hp is not None:
                for offset,value in previous_hp.items():
                    hp.write(offset,value)
                    if hp.read(offset)!=value:raise RuntimeError('Classic HP0 restore failed')
        save('returned',old_home_used=False,interrupted=interrupted,hp_restored=True,program_freed=owner is None or owner.buffer is None)
    return interrupted

def run_menu():
    directory=Path(__file__).resolve().parent
    record_directory=directory/('menu_cycle_'+str(time.monotonic_ns()))
    record_directory.mkdir()
    manifest=json.loads((directory/'menu_manifest.json').read_text())
    for name,expected in manifest['files'].items():
        if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=expected:
            raise RuntimeError('Preflight hash mismatch: '+name)
    if Device.active_device is None: raise RuntimeError('No PYNQ device')
    def stop(signum,frame): raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    owner=frame=control=None;hp_maps=[];stopped=False;classic_choice=None
    active=None;allocation=None;final_received=False;final_index=0
    cleanup=dict(pass_=False,pid=os.getpid(),program_freed=False,display_freed=False,hp_restored=False)
    def menu_final():
        nonlocal final_received,final_index
        if active!='menu' or final_received:return
        receiver=Mailbox();control.write(0x40,0)
        control.write(0x0c,receiver.request_final(control.read(0x48)))
        deadline=time.monotonic()+20
        while not receiver.finished:
            position=control.read(0x48)
            sample=receiver.accept(position,dict(monotonic=time.monotonic(),position=position))
            control.write(0x0c,0x102|receiver.phase)
            if sample is not None:
                assert sample['mailbox_final'] and valid_menu_sample(sample)
                (record_directory/('menu_final_%03d.json'%final_index)).write_text(json.dumps(sample,indent=2)+'\n')
            if time.monotonic()>deadline:raise TimeoutError('Menu final packet did not drain; keep owner')
            time.sleep(.001)
        final_received=True;final_index+=1
    try:
        assert subprocess.check_output(['systemctl','show','rvgame.service','--property=ActiveState','--value'],text=True).strip()=='inactive'
        Overlay(str(directory/manifest['bit']))
        control=MMIO(0x43c00000,0x1000);control.write(0x40,0);control.write(0x0c,0)
        if control.read(0x18)!=(480<<16|640): raise RuntimeError('Geometry mismatch')
        owner=DdrProgram(control);hp,previous=configure_hp0_32(owner);hp_maps.append((hp,previous))
        assert control.read(0x38)==4096,'S1 boot capacity mismatch'
        hp=MMIO(0xf8009000,0x20)
        previous={offset:hp.read(offset) for offset in (0,0x14)};hp_maps.append((hp,previous))
        for offset,value in previous.items():
            hp.write(offset,value|1)
            if hp.read(offset)!=value|1: raise RuntimeError('HP1 width readback failed')
        frame=allocate(shape=(1048576,),dtype='u1');base=int(frame.physical_address)
        if base&63 or not 0<=base<=0x3ff00000: raise RuntimeError('Display allocation outside DDR')
        frame[:]=0;frame.flush()
        for register,value in ((0x6c,base),(0x70,1)):
            control.write(register,value)
            if control.read(register)!=value: raise RuntimeError('Display configuration readback failed')
        switches=[]
        def load_app(name):
            nonlocal active,allocation,final_received
            image=(directory/manifest['applications'][name]).read_bytes()
            expected=manifest['files'][manifest['applications'][name]]
            assert hashlib.sha256(image).hexdigest()==expected,name
            if control.read(0x78):raise RuntimeError('Previous application display underflow')
            menu_final()
            owner.hold();control.write(0x40,0)
            # The arena keeps rendering its EXIT page. Let its last draw/copy publish
            # while source/frame buffers are still owned; disabling mid-copy strands
            # the engine's swap_pending/dma_started state across a CPU-only restart.
            scan_start=control.read(0x10);deadline=time.monotonic()+.2
            while ((control.read(0x10)-scan_start)&0xffffffff)<3:
                if time.monotonic()>deadline:raise TimeoutError('GPU retirement frame edges stopped')
                time.sleep(.0005)
            control.write(0x70,0)
            if control.read(0x70):raise RuntimeError('Display disable failed during switch')
            deadline=time.monotonic()+3
            while control.read(0x74)&2:
                if time.monotonic()>deadline:raise TimeoutError('HP1 bitmap/display drain failed; keep old program alive')
                time.sleep(.001)
            owner.close()
            scan=control.read(0x10);deadline=time.monotonic()+.2
            while control.read(0x10)==scan:
                if time.monotonic()>deadline:raise TimeoutError('HDMI frame edge stopped')
                time.sleep(.0005)
            control.write(0x70,1)
            if control.read(0x70)!=1:raise RuntimeError('Display enable failed')
            control.write(0x0c,0x100 if name=='menu' else 0)
            allocation=owner.load(image);active=name;final_received=False
            expected_page={'menu':0x3a0,'coremark':0x360,'validation':0x360,'gfx':0x370,
                'bitmap':0x380,'alpha':0x390,'arena':0x350}[name]
            deadline=time.monotonic()+30
            while control.read(0x48)>>16!=expected_page:
                if control.read(0x60)&1 or control.read(0x74)&1:raise RuntimeError('DDR fault starting '+name)
                if time.monotonic()>deadline:raise TimeoutError('Application did not start: '+name)
                time.sleep(.01)
            switches.append({'app':name,'allocation':allocation,'frame_base':base,'page':hex(expected_page),
                'underflows':control.read(0x78),'uptime':time.monotonic()})
            (directory/'active.json').write_text(json.dumps({'pid':os.getpid(),'active':active,'switches':switches},indent=2)+'\n')
            (record_directory/'switches.json').write_text(json.dumps(switches,indent=2)+'\n')
            print('UNIFIED_APP_LOADED',name,json.dumps(allocation),flush=True)
        allocation=None;load_app('menu')
        deadline=time.monotonic()+30
        while control.read(0x48)>>16!=0x3a0 or control.read(0x7c)<1:
            if control.read(0x60)&1 or control.read(0x74)&1: raise RuntimeError('DDR fault')
            if time.monotonic()>deadline: raise TimeoutError('640 trial title did not start')
            time.sleep(.02)
        before=control.read(0x10);time.sleep(.1)
        if before==control.read(0x10): raise RuntimeError('HDMI scan stopped')
        if control.read(0x78): raise RuntimeError('Display underflow at title')
        frame.invalidate();(directory/'menu.rgb332').write_bytes(memoryview(frame).cast('B')[:307200])
        record={'ready':True,'pid':os.getpid(),'menu_epoch':time.monotonic(),'allocation':allocation,'frame_base':base,
                'record_directory':str(record_directory),
                'test_input':'--test-input' in sys.argv,'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                'geometry':[640,480],'usb_gamepad':str(find_pad()),
                'position':hex(control.read(0x48)),'frame_copies':control.read(0x7c),
                'underflows':control.read(0x78),'frame_status':control.read(0x74)}
        (directory/'ready.json').write_text(json.dumps(record,indent=2)+'\n')
        (record_directory/'ready.json').write_text(json.dumps(record,indent=2)+'\n')
        print('SYSTEM640_INTERACTIVE_READY',json.dumps(record),flush=True)
        notify=os.environ.get('NOTIFY_SOCKET')
        if notify:
            address=bytes([0])+notify[1:].encode() if notify.startswith('@') else notify
            with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as status:
                status.connect(address);status.sendall(b'READY=1\nSTATUS=640x480 unified menu ready')
        blocked=0;first=True
        def poll(word):
            nonlocal blocked,first,classic_choice
            if '--test-input' in sys.argv:
                try:
                    injected=json.loads((directory/'test_input.json').read_text())
                    if time.monotonic()<injected['until']<time.monotonic()+1:
                        word=0x10000|(injected['mask']&4095)
                except FileNotFoundError:pass
            position=control.read(0x48);page=position>>16
            if control.read(0x60)&1 or control.read(0x74)&1:raise RuntimeError('Runtime DDR fault')
            if control.read(0x78):raise RuntimeError('Display underflow')
            if active=='menu' and page==0x3a4:
                classic_choice=2;raise TrialExit
            if active=='menu' and page==0x3a3:
                classic_choice=position&2047
                if classic_choice not in (0,1):raise RuntimeError('Invalid classic menu selection')
                raise TrialExit
            if active=='menu' and page in (0x3a1,0x3a2):
                choice=position&2047
                if page==0x3a2:target='validation'
                elif choice==5:raise TrialExit
                else:
                    if choice>4:raise RuntimeError('Invalid menu selection')
                    if choice==4:
                        classic_choice=2;raise TrialExit
                    target=('coremark','gfx','bitmap','alpha','arena')[choice]
                blocked=word&4095;load_app(target)
            elif active!='menu' and (page in (0x362,0x37f,0x38f,0x39f,0x356) or (page==0x3ff and word&4)):
                blocked=word&4095;load_app('menu')
            if first:blocked=word&4095;first=False
            blocked&=word&4095
            return word&~blocked
        run_input(control,on_poll=poll)
    except (KeyboardInterrupt,TrialExit):
        print('BROTATO640_INTERACTIVE_STOP',flush=True)
    finally:
        try:
            if owner is not None:
                try:
                    menu_final()
                    owner.hold();control.write(0x40,0)
                    start=control.read(0x10);deadline=time.monotonic()+.5
                    while ((control.read(0x10)-start)&0xffffffff)<3:
                        if time.monotonic()>deadline:raise TimeoutError('Final renderer retirement failed')
                        time.sleep(.001)
                    control.write(0x70,0)
                    if control.read(0x70): raise RuntimeError('Display disable failed')
                    deadline=time.monotonic()+3
                    while control.read(0x74)&2:
                        if time.monotonic()>deadline: raise TimeoutError('Display burst did not drain')
                        time.sleep(.001)
                    owner.close()
                    cleanup['program_freed']=owner.buffer is None
                except Exception:
                    # Preserve owner and buffers if drain cannot be proven.
                    print('QUARANTINED_KEEP_OWNER_AND_BUFFERS',flush=True)
                    while True:
                        try:time.sleep(30)
                        except BaseException:pass
            if frame is not None: frame.freebuffer();frame=None
            cleanup['display_freed']=frame is None
            for hp,previous in reversed(hp_maps):
                for offset,value in previous.items():
                    hp.write(offset,value)
                    if hp.read(offset)!=value: raise RuntimeError('HP restore failed')
            cleanup['hp_restored']=True;cleanup['pass_']=all(cleanup[k] for k in ('program_freed','display_freed','hp_restored'))
        finally:
            (record_directory/'cleanup.json').write_text(json.dumps(cleanup,indent=2)+'\n')
            (directory/'cleanup.json').write_text(json.dumps(cleanup,indent=2)+'\n')
    return classic_choice

def main():
    try:
        while True:
            choice=run_menu()
            if choice is None:raise SystemExit(10)
            if choice==2:return
            if launch_classic_game(choice):return
    except Exception:
        raise
if __name__=='__main__': main()
