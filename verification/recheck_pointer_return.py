"""Offline recheck of the recorded pointer entry/return runs and current bytes."""
import argparse
import hashlib
import json
from pathlib import Path
from audit_helpers import checksum

VERIFY=Path(__file__).resolve().parent
ROOT=VERIFY/'evidence'
BASE=ROOT/'reports/rvgame_longrun_20261031'
PACKAGE=VERIFY.parent/'runtime/package'

def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    assert not a.output.exists(),'Preserve previous receipts'
    result=dict(pass_=False,host_only=True,new_board_test=False,physical_USB_acceptance=False)
    try:
        proof=read(ROOT/'reports/input_robustness_20261102_a/received_a/result.json')
        for name,digest in proof['evidence'].items():assert sha(ROOT/name)==digest,name
        support=BASE/'input_robustness_support_a/package';manifest=read(support/'manifest.json')
        board=BASE/'native_session_smoke_board_native_input_robustness_a';run=board/'received'
        parent=read(board/'result.json');observed=read(run/'result.json');ready=read(run/'ready.json')
        assert parent['pass_'] and parent['original_restored'] and parent['board_result']==observed
        assert observed['pass_'] and observed['finished'] and observed['application_entry_tested']
        assert sha(support/'manifest.json')==parent['manifest_sha256']==sha(run/'manifest.json')
        for name,digest in manifest['files'].items():
            p=support/name if (support/name).is_file() else PACKAGE/name
            assert sha(p)==digest,name
        for name,digest in proof['unchanged_application_bins'].items():
            current=sha(PACKAGE/name)
            if current!=digest:
                assert name in ('game.bin','touhou.bin') and sha(support/name)==digest,name
        current_matches={}
        for name in ('menu.bin','pointer_controls.py','pointer_source.py','pointer_devices.py'):
            current_matches[name]=sha(PACKAGE/name)==manifest['files'][name]
            if not current_matches[name]:
                assert name in ('menu.bin','pointer_controls.py') and sha(support/name)==manifest['files'][name],name
        clean=read(run/'cleanup.json');assert clean==parent['cleanup']==observed['cleanup']
        assert clean['pass'] and clean['pid']==ready['pid'] and not clean.get('error')
        assert all(clean[k] for k in ('program_freed','display_freed','hp_restored','trace_writer_flushed'))
        samples=[json.loads(s) for s in (run/'samples.jsonl').read_text().splitlines()]
        for s in samples:
            assert checksum(s)==s['mailbox_checksum'] and s['underflows']==0
            assert not s['ddr_status']&1 and not s['display_status']&1
            if s.get('retirement_only'):assert s['mailbox_final'] and s['retirement_verified'] and not s['retirement']['fault']
            else:assert s['coherent'] and s['structure_coherent']
        switches=[read(p) for p in sorted(run.glob('switch_*.json'))]
        checks={x['name']:x for x in observed['checks']};assert len(switches)==24 and len(checks)==27
        apps=('coremark','gfx','bitmap','alpha','game','touhou')
        for method_index,method in enumerate(('mouse','touch')):
            for index,app in enumerate(apps):
                n=method_index*12+index*2;enter,leave=switches[n:n+2]
                assert (enter['from'],enter['to'],leave['from'],leave['to'])==('menu',app,app,'menu')
                for s in (enter,leave):
                    assert s['pass_'] and s['pid']==ready['pid'] and s['frame_base']==ready['frame_base']
                    assert not s['overlay_reloaded']
                    assert all(s[k] for k in ('display_buffer_retained','held_final_frames','hp1_drained','old_program_freed','display_kept_enabled'))
                for verb in ('enter','return'):
                    c=checks[method+'_'+verb+'_'+app];assert c['selection_sample'] in samples
                    assert c['selection_sample']['structure_telemetry'][4]==index
                    assert c['state']['active']==(app if verb=='enter' else 'menu')
        assert parent['before']['owners']['boot_id']==parent['after']['owners']['boot_id']
        assert parent['before']['owners']['hp0']==parent['after']['owners']['hp0']
        assert parent['after']['unit']['ActiveState']=='active' and not parent['after']['ready']['test_input']
        assert (run/'menu_dock_fixed.rgb332').read_bytes()[548*800:598*800]==(support/'pointer_reference.rgb332').read_bytes()[548*800:598*800]
        assert len(samples)==proof['CRC_packets']==42
        result.update(pass_=True,CRC_packets=len(samples),safe_switches=len(switches),round_trips=12,
            historical_inputs_match_current=current_matches,latest_direct_gestures_board_verified=False,
            current_pointer_files={name:sha(PACKAGE/name) for name in ('menu.bin','pointer_controls.py','pointer_source.py','pointer_devices.py')},
            original_receipt_sha256=sha(ROOT/'reports/input_robustness_20261102_a/received_a/result.json'),
            scope='Historical robustness-menu packet/switch recheck against archived exact inputs; changed direct-entry gestures are not covered by this old board test')
    except Exception as exc:result['error']=repr(exc);raise
    finally:a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('POINTER_RETURN_RECHECK',len(samples),len(switches))

if __name__=='__main__':main()
