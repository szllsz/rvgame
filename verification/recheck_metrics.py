"""Recompute published metrics and check exact runtime bindings, offline."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from statistics import mean

HERE=Path(__file__).resolve().parent
BUNDLE=HERE.parent
EVIDENCE=HERE/'evidence'
BASE=EVIDENCE/'reports/rvgame_longrun_20261031'


def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    index=read(HERE/'evidence_index.json')
    for name,spec in index['files'].items():assert sha(BUNDLE/name)==spec['sha256'],name
    bindings=[]
    def bind(manifest,names):
        files=read(manifest)['files']
        for name in names:
            actual=sha(BUNDLE/'runtime/package'/name)
            current_matches=files[name]==actual
            if not current_matches:
                allowed={('input_robustness_support_a','menu.bin'),('input_robustness_support_a','pointer_controls.py'),
                         ('egg_camera_support_c','game.bin'),('space_ui_support_polish_c','touhou.bin')}
                assert (manifest.parent.parent.name,name) in allowed,(manifest,name)
                assert sha(manifest.parent/name)==files[name],('Missing exact historical input',name)
            bindings.append(dict(name=name,sha256=files[name],current_sha256=actual,current_matches=current_matches,
                                 manifest=manifest.relative_to(EVIDENCE).as_posix()))
    old=BASE/'native_session_smoke_support_sol_suite800_final_status_y/package/manifest.json'
    recent=BASE/'native_session_smoke_support_sol_suite800_showcase_final_g/package/manifest.json'
    current=BASE/'native_session_smoke_board_native_current_review_a/received/manifest.json'
    bind(old,['coremark.bin','validation.bin','gfx.bin','bitmap.bin','top.bit','top.hwh'])
    bind(recent,['alpha.bin','top.bit','top.hwh'])
    bind(current,['coremark.bin','validation.bin','top.bit','top.hwh'])
    bind(BASE/'egg_camera_support_c/package/manifest.json',['game.bin','top.bit','top.hwh'])
    bind(BASE/'input_robustness_support_a/package/manifest.json',
         ['menu.bin','input_backend.py','pynq_gamepad.py','replay_input.py','pointer_controls.py','pointer_devices.py','pointer_source.py'])
    bind(BASE/'space_ui_support_polish_c/package/manifest.json',
         ['touhou.bin','top.bit','top.hwh','pynq_brotato_session.py','bt_mailbox.py'])
    old_alpha_matches=read(old)['files']['alpha.bin']==sha(BUNDLE/'runtime/package/alpha.bin')
    assert not old_alpha_matches, 'The documented historical Alpha mismatch changed; revisit inheritance'
    observed=read(BASE/'native_session_smoke_board_native_current_review_a/received/result.json')
    raw=next(c['sample']['cpu_text'] for c in observed['checks'] if c['name']=='CPU_actual_4000_performance_validation')
    blocks=raw.split('RVGAME CPU SUITE ')[1:];assert len(blocks)==2
    expected_crc=[[0xe9f5,0xe714,0x1fd7,0x8e3a,0x65c5],[0x18f2,0xe3c1,0x0747,0x8d84,0x5249]]
    cpu=[]
    for i,block in enumerate(blocks):
        ticks=int(re.search(r'Total ticks\s*:\s*(\d+)',block)[1])
        iterations=int(re.search(r'Iterations\s*:\s*(\d+)',block)[1])
        crc=[int(v,16) for v in re.findall(r'(?:seedcrc|crclist|crcmatrix|crcstate|crcfinal)\s*:\s*0x([0-9a-f]+)',block)]
        assert crc==expected_crc[i] and iterations==4000
        seconds=ticks/105000000;assert seconds>10
        cpu.append(dict(phase=i,iterations=iterations,ticks=ticks,clock_hz=105000000,
                        seconds=seconds,iterations_per_second=iterations/seconds,
                        per_MHz=iterations/seconds/105,CRC=crc))
    prior=read(EVIDENCE/'reports/final_review_20261101_a/automatic_review_summary.json')
    assert cpu==prior['CPU']
    graphics=[]
    for tag,apps in [('native_sol_suite800_final_status_y',['gfx','bitmap']),('native_sol_suite800_showcase_final_g',['alpha'])]:
        r=read(BASE/('native_session_smoke_board_'+tag)/'received/result.json')
        for item in r['runs']:
            app=item['sample']['session_app']
            if app not in apps:continue
            w=item['sample']['capacity_words'];n=w[2];seconds=(w[3]+(w[4]<<32))/105000000
            assert seconds>=60 and w[13]==w[5]==n-1 and not any(w[i] for i in (12,14,17))
            assert w[35]==0 and 59.9<=w[13]/seconds<=60.1
            m=item['measurement'];assert m['seconds']==seconds and m['frames']==w[13] and m['scans']==w[5]
            graphics.append(dict(application=app,count=w[34],seconds=seconds,frames=w[13],scans=w[5],FPS=w[13]/seconds,strict60=True,source=tag))
    assert [x['count'] for x in graphics]==[512,96,32]
    egg=[]
    camera=read(BASE/'native_session_smoke_board_native_egg_camera_c/received/result.json')
    for point in camera['closed_epochs'].values():
        w=point['meter'];seconds=(w[3]+(w[4]<<32))/105000000;fps=w[6]/seconds
        assert point['seconds']==seconds and point['FPS']==fps and point['missed']==w[5]-w[6]
        egg.append(point)
    assert len(egg)==6 and {p['population'] for p in egg}=={64,85,100}
    camera_audit=read(EVIDENCE/'reports/egg_camera_follow_20261102_a/received_c/result.json')
    assert camera_audit['pass_'] and camera_audit['program_sha256']==read(BASE/'egg_camera_support_c/package/manifest.json')['files']['game.bin']
    for point in egg:
        bound=next(p for p in camera_audit['closed_epochs'] if p['epoch']==point['epoch'])
        assert bound['population']==point['population'] and bound['FPS']==point['FPS'] and bound['seconds']==point['seconds']
    star=read(BASE/'space_ui_received_polish_c_verified_b/result.json')
    result=dict(pass_=True,host_only=True,board_tested=False,scope='Historical metric recomputation with archived exact game/menu inputs. Unchanged CPU/graphics remain bound to current bytes; current games use separate 20261103 evidence.',
                CPU=cpu,graphics=graphics,egg_bros_curve=egg,
                egg_bros_arithmetic_mean_FPS={str(p):mean(v['FPS'] for v in egg if v['population']==p) for p in (64,85,100)},
                egg_window_scope=prior['egg_window_scope'],star_barrage=star['metrics'],
                current_UI_recheck_command='python verification/recheck_ui.py --output <new-directory>',
                payload_bindings=bindings,old_alpha_receipt_applies=False,
                qualified_bit_sha256=sha(BUNDLE/'runtime/package/top.bit'),
                cold_rebuilt_bit_board_tested=False,manual_tested=False,power_measured=False)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('METRICS_RECHECK_PASS',len(bindings),'payload bindings',len(index['files']),'evidence files')


if __name__=='__main__':main()
