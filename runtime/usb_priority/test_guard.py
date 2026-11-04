"""Exercise same-address retry, silence discrimination and recovery bounds."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import types
from unittest.mock import patch

ROOT=Path(__file__).resolve().parent
if sys.platform=='win32':sys.modules.setdefault('fcntl',types.SimpleNamespace())
spec=importlib.util.spec_from_file_location('wake_guard',ROOT/'usb_priority_guard.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)


def run():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    out=parser.parse_args().output;out.mkdir(parents=True,exist_ok=False)
    checks=[];clock=[100.0];state={'gamepad_connected':True,'gamepad_source':'flydigi_usb','gamepad_stale':True,'gamepad_error':None,'at_us':99000000}
    with patch.object(g.time,'monotonic',lambda:clock[0]),patch.object(g.subprocess,'run',return_value=types.SimpleNamespace(stdout='active')),patch.object(g,'usb_irq',return_value=17):
        guard=g.Guard(out/'state.json',True)
        calls=[]
        guard.mouse=Path('/qualified/mouse');guard.identity=('046d','c092','fixture','3')
        guard.set_binding=lambda number,bound:calls.append(('binding',number,bound))
        guard.status=lambda:dict(state)
        guard.validate_topology=lambda:calls.append(('topology',))
        resets=[];guard.recover_host=lambda:resets.append(clock[0])
        guard.health(('1-1.2','4',1000))
        assert guard.main_changed and guard.wait_started==100 and calls[0]==('binding',0,False)
        checks.append('Same USB address with new stale status reserves gamepad first')
        clock[0]=106
        with patch.object(g,'descriptor_status',return_value=18):guard.health(('1-1.2','4',1100))
        assert not resets
        checks.append('Quiet IRQ plus responding control endpoints does not reset a sleeping device')
        clock[0]=117
        with patch.object(g,'descriptor_status',side_effect=[-7,18]):guard.health(('1-1.2','4',1200))
        assert not resets
        checks.append('One timed-out control endpoint is insufficient for host recovery')
        clock[0]=128
        with patch.object(g,'descriptor_status',side_effect=[-7,-7]):guard.health(('1-1.2','4',1300))
        assert resets==[128]
        checks.append('Stale input plus stalled IRQ and two descriptor timeouts permits recovery')
        guard.recover_stalled_host=False;clock[0]=140
        with patch.object(g,'descriptor_status',side_effect=AssertionError('probe forbidden')):guard.health(('1-1.2','4',1400))
        assert resets==[128]
        checks.append('Without explicit recovery option no control probe or host reset occurs')
        fresh=dict(state,gamepad_stale=False,at_us=141000000)
        line=json.dumps({'__MONOTONIC_TIMESTAMP':'141000000','MESSAGE':'INPUT_SOURCE_STATUS '+json.dumps(fresh)})
        assert g.latest_pad_status(line,140000000) and not g.latest_pad_status(line,142000000)
        checks.append('Fresh decoded status required after recovery epoch')
        limited=g.Guard(out/'limited.json',True);limited.recoveries=[100,110]
        limited.validate_topology=lambda:(_ for _ in ()).throw(AssertionError('must not touch USB'))
        limited.recover_host();assert limited.events[-1]['phase']=='recovery_rate_limited'
        checks.append('Two host resets within five minutes block further mutation')
        # Exercise the actual cold-boot timeout branch without sysfs operations.
        cold=g.Guard(out/'cold.json');cold.mouse=Path('/qualified/mouse');cold.identity=('fixture',)
        cold.same_mouse=lambda:True;cold.pad=lambda:('1-1.2','4',13);cold.health=lambda pad:None
        cold.pad_key=('1-1.2','4');cold.wait_started=100;cold.urb_start=13;cold.main_changed=True
        cold.set_binding=lambda n,b:calls.append(('cold_binding',n,b))
        clock[0]=130;cold.tick();assert cold.main_changed and cold.wait_started==100
        cold.application_active=True;cold.tick();assert not cold.main_changed and cold.wait_started is None
        checks.append('Initial timeout waits for application startup before bounded failure')
        cold.wait_started=100;cold.main_changed=True;cold.application_active=False;clock[0]=191;cold.tick()
        assert not cold.main_changed
        checks.append('Startup wait remains bounded at ninety seconds')
        boot=g.Guard(out/'application_grace.json');boot.wait_started=100;boot.application_active=False
        boot.status=lambda:None;clock[0]=151
        boot.health(('1-1.2','7',50))
        assert boot.wait_started==151 and boot.urb_start==50 and boot.application_active
        clock[0]=153;boot.health(('1-1.2','7',70))
        assert boot.wait_started==151
        checks.append('Application readiness starts one fresh reservation window; later polls do not extend it')
    # A foreign device is rejected before looking up or writing the host driver.
    class Foreign:
        name='foreign'
        def __truediv__(self,key):return key
    class Bus:
        def iterdir(self):return iter([Foreign()])
    strict=g.Guard(out/'foreign.json',True);strict.mouse=out;strict.same_mouse=lambda:True
    with patch.object(g,'USB',Bus()),patch.object(g,'text',side_effect=lambda key:{'idVendor':'1234','idProduct':'5678'}.get(key)):
        try:strict.validate_topology()
        except RuntimeError as error:assert 'Unqualified USB' in str(error)
        else:raise AssertionError('Foreign topology admitted')
    checks.append('Unknown USB identity refuses host recovery')
    # Diagnostic failures must not terminate the service or authorize USB resets.
    clock[0]=300
    with patch.object(g.time,'monotonic',lambda:clock[0]):
        q=g.Guard(out/'queries.json',True)
        with patch.object(g.subprocess,'check_output',side_effect=g.subprocess.TimeoutExpired('journalctl',5)) as query:
            assert q.status() is None and q.status() is None and query.call_count==1
            assert q.status_unavailable and q.cached_status is None and q.next_status_query==302
        checks.append('Journal timeout is unknown evidence, cached briefly, not an exception or USB fault')
        clock[0]=302
        with patch.object(g.subprocess,'check_output',side_effect=g.subprocess.CalledProcessError(1,'journalctl')):
            assert q.status() is None and q.next_status_query==306
        clock[0]=306
        with patch.object(g.subprocess,'check_output',return_value='{malformed'):
            assert q.status() is None and q.next_status_query==314
        checks.append('Command failure and malformed journal back off without claiming healthy input')
        clock[0]=314
        line=json.dumps({'__MONOTONIC_TIMESTAMP':'314000000','MESSAGE':'INPUT_SOURCE_STATUS '+json.dumps(dict(gamepad_connected=True,gamepad_source='flydigi_usb',gamepad_stale=False,gamepad_error=None))})
        with patch.object(g.subprocess,'check_output',return_value=line) as query:
            assert g.pad_healthy(q.status()) and g.pad_healthy(q.status()) and query.call_count==1
            assert not q.status_unavailable and q.status_failures==0
            q.since_us=315000000
            assert q.status() is None and query.call_count==2
        checks.append('Valid query recovers; changing the USB epoch cannot reuse an old healthy cache')
        q.application_active=True;clock[0]=320;q.next_health=0
        q.pause_mouse=lambda:(_ for _ in ()).throw(AssertionError('No binding on unavailable evidence'))
        q.recover_host=lambda:(_ for _ in ()).throw(AssertionError('No reset on unavailable evidence'))
        with patch.object(g.subprocess,'run',side_effect=g.subprocess.TimeoutExpired('systemctl',2)) as query:
            q.health(('1-1.2','4',20));q.health(('1-1.2','4',20))
            assert query.call_count==1 and q.service_query_unavailable and q.application_active
        checks.append('Service-query timeout preserves state, does not mutate bindings, and backs off')
        clock[0]=323;q.status=lambda:None
        with patch.object(g.subprocess,'run',return_value=types.SimpleNamespace(stdout='active')),patch.object(g,'usb_irq',return_value=20):
            q.health(('1-1.2','4',21));assert not q.service_query_unavailable
        checks.append('Service-query recovery resumes the normal health loop')
    with patch.dict(g.os.environ,{},clear=True),patch.object(g,'mouse_identity',side_effect=AssertionError('unconfigured lookup')):
        assert g.qualified_mouse(out) is None
    checks.append('No configured mouse serial prevents device lookup')
    with patch.dict(g.os.environ,{'RVGAME_G102_SERIAL':'approved'},clear=True),patch.object(g,'mouse_identity',return_value=('046d','c092','foreign','3')):
        assert g.qualified_mouse(out) is None
    checks.append('Configured serial rejects another mouse of the same model')
    with patch.dict(g.os.environ,{'RVGAME_G102_SERIAL':'approved'},clear=True),patch.object(g,'mouse_identity',return_value=('046d','c092','approved','3')),patch.object(g,'text',return_value='foreign'):
        assert g.qualified_mouse(out) is None
    checks.append('Matching serial still requires the approved hub topology')
    result=dict(pass_=True,host_only=True,board_tested=False,checks=checks)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('USB_WAKE_HOST_CHECKS',len(checks),'PASS')


if __name__=='__main__':run()
