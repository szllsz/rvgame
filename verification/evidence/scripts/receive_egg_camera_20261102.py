"""Audit camera board frames, movement, packet/PCM CRC and showcase epochs."""
import json
from pathlib import Path
from scarlet_resources_20261031 import ROOT,BASE,sha
from receive_touhou_g12_metrics_20261031 import checksum
from run_scarlet_music_board_20261031 import crc_prefix


def read(p):return json.loads(p.read_text(encoding='utf-8'))


def main():
    out=ROOT/'reports/egg_camera_follow_20261102_a/received_c';out.mkdir(exist_ok=False)
    result=dict(pass_=False,manual_tested=False,independent_final_review=False,adopted=False)
    evidence=[];runs={};summaries=[]
    try:
        fix=out.parent;preparation=read(fix/'preparation.json');host=read(fix/'host_c/result.json')
        assert preparation['pass_'] and host['pass_']
        assert sha(fix/'game/game.bin')==preparation['binary_sha256']
        evidence.extend((fix/'preparation.json',fix/'host_c/result.json',fix/'game/game.bin'))
        for tag in ('c',):
            board=BASE/('native_session_smoke_board_native_egg_camera_'+tag);run=board/'received'
            support=BASE/('egg_camera_support_'+tag);package=support/'package'
            parent=read(board/'result.json');observed=read(run/'result.json');ready=read(run/'ready.json')
            assert parent['board_result']==observed and parent['original_restored']
            if tag=='c':assert parent['pass_'] and observed['pass_'] and observed['finished'] and not observed.get('error')
            manifest=read(package/'manifest.json');native=read(package/'candidate_manifest.json')
            assert sha(package/'manifest.json')==parent['manifest_sha256']==read(support/'result.json')['manifest_sha256']
            assert manifest['files']['game.bin']==preparation['binary_sha256']
            for name,digest in manifest['files'].items():
                assert sha(package/name)==digest,name
                if (run/name).is_file():assert sha(run/name)==digest,name
            assert sha(run/'manifest.json')==sha(package/'manifest.json')
            assert ready['geometry']==[800,600] and ready['test_input'] and ready['compact_exit']
            clean=read(run/'cleanup.json');assert clean==observed['cleanup']==parent['cleanup']
            assert clean['pass'] and clean['pid']==ready['pid'] and not clean.get('error')
            assert all(clean[k] for k in ('program_freed','display_freed','hp_restored','trace_writer_flushed'))
            assert not (run/'owner_error.json').exists()
            assert parent['before']['owners']['boot_id']==parent['after']['owners']['boot_id']
            assert parent['before']['owners']['hp0']==parent['after']['owners']['hp0']
            assert parent['after']['unit']['ActiveState']=='active' and not parent['after']['ready']['test_input']
            samples=[json.loads(s) for s in (run/'samples.jsonl').read_text().splitlines()]
            for row in samples:
                assert checksum(row)==row['mailbox_checksum']
                assert row['underflows']==0 and not row['ddr_status']&1 and not row['display_status']&1
                if row.get('retirement_only'):
                    assert row['mailbox_final'] and row['retirement_verified'] and not row['retirement']['fault']
                else:assert row['coherent'] and row['structure_coherent']
            switches=[read(p) for p in sorted(run.glob('switch_*.json'))]
            for row in switches:
                assert row['pass_'] and row['pid']==ready['pid'] and row['frame_base']==ready['frame_base']
                assert row['held_final_frames'] and row['hp1_drained'] and row['old_program_freed'] and not row['overlay_reloaded']
                assert row['final'] in samples and row['final']['mailbox_final']
            audio=[json.loads(s) for s in (run/'music_samples.jsonl').read_text().splitlines()]
            previous={};pcm={};sources={}
            for row in audio:
                app=row['application'];segment=row['segment'];assert app=='game' and not row.get('ui_mixed')
                spec=native['native_music'][app];name=spec['file']
                assert row['music_file']==name and row['music_sha256']==spec['sha256']
                assert sources.setdefault(segment,name)==name
                if name not in pcm:
                    assert sha(package/name)==spec['sha256'];pcm[name]=(package/name).read_bytes()
                assert row['digital_crc_verified'] and previous.get(segment,0)<=row['consumed_samples']<=row['submitted_samples']
                assert row['underflows']==row['serial_faults']==row['serial_quiet_faults']==0
                assert row['crc32']==crc_prefix(pcm[name],spec['loop_start'],spec['loop_end'],row['consumed_samples'])
                previous[segment]=row['consumed_samples']
            finals=[read(p) for p in run.glob('music_segment_*_final.json')]
            assert audio and finals and all(r in audio and r['final'] and r['paused'] for r in finals)
            runs[tag]=(run,observed,samples)
            summaries.append(dict(tag=tag,original_run_pass=parent['pass_'],restored=True,CRC_packets=len(samples),
                                  short_retirement_packets=sum(bool(r.get('retirement_only')) for r in samples),
                                  PCM_CRC_snapshots=len(audio),PCM_consumed_samples=sum(r['consumed_samples'] for r in finals),switches=len(switches)))
            evidence.extend([board/'result.json',support/'result.json',package/'manifest.json',*[p for p in run.iterdir() if p.suffix in ('.json','.jsonl')]])
        pixels=[]
        for role,spec in enumerate(preparation['roles']):
            tag='c';run,observed,samples=runs[tag]
            capture=next(p for p in observed['role_images'] if p['role']==role)
            check=next(p for p in observed['checks'] if p['name']=='normal_role_%d_visible'%role)
            assert check['sample'] in samples and check['sample']['telemetry'][56]==role
            image=run/capture['image_file'];actual=image.read_bytes();assert len(actual)==480000 and sha(image)==capture['sha256']
            original=BASE/'egg_camera_support_c/package'/('role_reference_%d.rgb332'%role);expected=original.read_bytes();assert sha(original)==spec['sha256']
            left=400+spec['x'];top=305+spec['y'];assert capture['actor_top_left']==[left,top]
            coords=[(y,x) for y in range(spec['height']//2) for x in range(spec['width']) if expected[y*spec['width']+x]]
            assert len(coords)>100
            assert all(actual[(top+y)*800+left+x]==expected[y*spec['width']+x] for y,x in coords),role
            pixels.append(dict(role=role,source_run=tag,visible_head_exact_pixels=len(coords),full_frame_sha256=sha(image)))
            evidence.extend((original,image))
        run,observed,samples=runs['c'];aux=[json.loads(s) for s in (run/'showcase_samples.jsonl').read_text().splitlines()]
        assert observed['observation_seconds']>=60 and len(observed['closed_epochs'])==6
        for row in aux:
            assert checksum(row)==row['mailbox_checksum']
            assert row['underflows']==0 and not row['ddr_status']&1 and not row['display_status']&1
        curve=[]
        for key,point in observed['closed_epochs'].items():
            w=point['meter'];assert any(row['telemetry'][19:27]==w for row in aux)
            seconds=(w[3]+(w[4]<<32))/105000000;fps=w[6]/seconds
            strict=w[5]==w[6]==w[2]-1 and w[7]==0 and 59.9<=fps<=60.1
            assert abs(fps-point['FPS'])<1e-10 and strict==point['stage_strict60'] and w[5]-w[6]==point['missed']
            curve.append(dict(epoch=int(key),population=w[1],seconds=seconds,FPS=fps,stage_strict60=strict,missed=w[5]-w[6]))
        assert len(observed['camera_checks'])==1
        move=observed['camera_checks'][0];assert move['role']==0
        spec=preparation['roles'][0]
        raw=(BASE/'egg_camera_support_c/package/role_reference_0.rgb332').read_bytes()
        for key,anchor_xy in [('follow',[400,305]),('left_edge',[56,305])]:
            capture=move[key];path=run/capture['image_file'];actual=path.read_bytes()
            assert len(actual)==480000 and sha(path)==capture['sha256']
            left=anchor_xy[0]+spec['x'];top=anchor_xy[1]+spec['y']
            assert capture['actor_top_left']==[left,top]
            for y in range(spec['height']//2):
                for x in range(spec['width']):
                    color=raw[y*spec['width']+x]
                    if color:assert actual[(top+y)*800+left+x]==color,(key,x,y)
            evidence.append(path)
        center=next(p for p in observed['role_images'] if p['role']==0)
        assert center['sha256']!=move['follow']['sha256']
        assert next(c for c in observed['checks'] if c['name']=='camera_pause_page')['page']==0x352
        assert next(c for c in observed['checks'] if c['name']=='camera_resume_page')['page']==0x351
        result['camera_checks']=observed['camera_checks']
        result.update(pass_=True,runs=summaries,pixels=pixels,closed_epochs=curve,
                      program_sha256=preparation['binary_sha256'],scope='Camera: five roles and movement pixels, host full frames, CRC/PCM, six approximately 8-second closed showcase epochs; not six 60-second windows',
                      evidence={p.relative_to(ROOT).as_posix():sha(p) for p in [Path(__file__),*evidence]})
    except Exception as error:result['error']=repr(error);raise
    finally:(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('EGG_CAMERA_RECEIVED',json.dumps(dict(runs=summaries,pixels=pixels,closed_epochs=curve)))


if __name__=='__main__':main()
