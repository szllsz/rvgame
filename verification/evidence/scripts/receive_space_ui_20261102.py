"""Recompute current UI board metrics, packet FNV and consumed PCM CRC."""
import argparse
import json
from pathlib import Path
from scarlet_resources_20261031 import ROOT,BASE,sha
from receive_touhou_g12_metrics_20261031 import checksum
from run_scarlet_music_board_20261031 import crc_prefix
from scarlet_ui_reference_20261101 import UiCrc


def read(p):return json.loads(p.read_text(encoding='utf-8'))


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tag',required=True);ap.add_argument('--output-tag');a=ap.parse_args()
    board=BASE/('native_session_smoke_board_native_space_ui_'+a.tag)
    support=BASE/('space_ui_support_'+a.tag);run=board/'received'
    out=BASE/('space_ui_received_'+(a.output_tag or a.tag));out.mkdir(exist_ok=False)
    result=dict(pass_=False,board_tested=True,manual_tested=False,independent_final_review=False)
    try:
        parent=read(board/'result.json');observed=read(run/'result.json');ready=read(run/'ready.json')
        assert parent['pass_'] and parent['original_restored'] and observed['pass_'] and parent['board_result']==observed
        assert not parent.get('error') and not observed.get('error') and observed['finished']
        manifest=read(support/'package/manifest.json');native=read(support/'package/candidate_manifest.json')
        assert sha(support/'package/manifest.json')==parent['manifest_sha256']==read(support/'result.json')['manifest_sha256']
        for name,digest in manifest['files'].items():
            assert sha(support/'package'/name)==digest,name
            # The tested controller checks every uploaded byte before launch.
            # Its downloader intentionally retains diagnostics, not duplicate ROMs.
            if (run/name).is_file():assert sha(run/name)==digest,name
        assert sha(run/'manifest.json')==sha(support/'package/manifest.json')
        assert sha(run/'candidate_manifest.json')==sha(support/'package/candidate_manifest.json')
        assert ready==observed['ready'] and ready['geometry']==[800,600] and ready['test_input'] and ready['compact_exit']
        clean=read(run/'cleanup.json');assert clean==observed['cleanup']==parent['cleanup']
        assert clean['pass'] and clean['pid']==ready['pid'] and not clean.get('error')
        assert all(clean[k] for k in ('program_freed','display_freed','hp_restored','trace_writer_flushed'))
        assert not (run/'owner_error.json').exists()
        assert parent['before']['owners']['boot_id']==parent['after']['owners']['boot_id']
        assert parent['before']['owners']['hp0']==parent['after']['owners']['hp0']
        assert parent['after']['unit']['ActiveState']=='active' and not parent['after']['ready']['test_input']
        samples=[json.loads(line) for line in (run/'samples.jsonl').read_text().splitlines()]
        for row in samples:
            assert checksum(row)==row['mailbox_checksum']
            assert row['underflows']==0 and not row['ddr_status']&1 and not row['display_status']&1
            if row.get('retirement_only'):
                assert row['mailbox_final'] and row['retirement_verified'] and not row['retirement']['fault']
            else:
                assert row['coherent'] and row['structure_coherent']
                if row.get('touhou_game'):assert not row['structure_telemetry'][12] and not row['telemetry'][55]
        metrics=[]
        for measure in observed['touhou_performance']:
            x,y=measure['start_sample'],measure['end_sample'];assert x in samples and y in samples
            aa,bb=x['telemetry'],y['telemetry'];density=measure['density']
            assert aa[60:63]==bb[60:63]==[0,3,density]
            seconds=((bb[32]|bb[33]<<32)-(aa[32]|aa[33]<<32))/105000000
            frames=bb[34]-aa[34];scans=bb[35]-aa[35];nonunit=bb[36]-aa[36];missed=bb[37]-aa[37];logic=bb[39]-aa[39]
            strict=seconds>=60 and frames==scans and nonunit==missed==logic==0 and 59.9<=frames/seconds<=60.1
            actual=dict(seconds=seconds,completed_frames=frames,scans=scans,completed_FPS=frames/seconds,
                        nonunit_scans=nonunit,missed_scans=missed,missed_logic_ticks=logic,strict_every_scan=strict)
            assert actual==measure['timed_interval'];metrics.append(dict(density=density,**actual))
        assert len(metrics)==2 and metrics[0]['density']==1 and metrics[0]['strict_every_scan'] and metrics[1]['density']==2
        controls=next(x for x in observed['checks'] if x['name']=='pause_volume_mute_density_background_B_resume')
        for key in ('paused','volume','muted','scene','resumed'):assert controls[key] in samples
        assert controls['volume']['telemetry'][31]!=controls['paused']['telemetry'][31]
        assert (controls['muted']['telemetry'][31]^controls['volume']['telemetry'][31])&256
        modes=next(x for x in observed['checks'] if x['name']=='software_hardware_modes')
        assert modes['software'] in samples and modes['software']['telemetry'][60]==1
        assert modes['hardware'] in samples and modes['hardware']['telemetry'][60]==0
        sw_seconds=modes['software']['mailbox_received_monotonic']-modes['software']['monotonic']
        assert 20<sw_seconds<120,'Must exercise a real complete packet exceeding the old false-stall bound'
        music_rows=[json.loads(line) for line in (run/'music_samples.jsonl').read_text().splitlines()]
        previous={};data={};mixers={};sources={};effects={}
        for kind,spec in native['ui_effects'].items():
            p=support/'package'/spec['file'];assert sha(p)==spec['sha256'];effects[kind]=p.read_bytes()
        for row in music_rows:
            app=row['application'];segment=row['segment'];allowed=[native['native_music'][app]]
            if app=='touhou':allowed.append(native['touhou_title_music'])
            matches=[s for s in allowed if s['file']==row['music_file']];assert len(matches)==1
            spec=matches[0];key=spec['file'];assert sources.setdefault(segment,key)==key and row['music_sha256']==spec['sha256']
            if key not in data:
                p=support/'package'/key;assert sha(p)==spec['sha256'];data[key]=p.read_bytes()
            assert row['digital_crc_verified'] and previous.get(segment,0)<=row['consumed_samples']<=row['submitted_samples']
            previous[segment]=row['consumed_samples']
            assert row['underflows']==row['serial_faults']==row['serial_quiet_faults']==0
            if row.get('ui_mixed'):
                if segment not in mixers:mixers[segment]=UiCrc(data[key],spec['loop_start'],spec['loop_end'],effects)
                expected=mixers[segment].accept_delta(row['consumed_samples'],row['ui_effects'],row['ui_effects_base'],row['ui_effects_total'],row['submitted_samples'])
            else:expected=crc_prefix(data[key],spec['loop_start'],spec['loop_end'],row['consumed_samples'])
            assert row['crc32']==expected
        finals=[read(p) for p in run.glob('music_segment_*_final.json')]
        assert finals and all(r in music_rows and r['final'] and r['paused'] for r in finals)
        switches=[]
        for p in sorted(run.glob('switch_*.json')):
            row=read(p);assert row['pass_'] and row['pid']==ready['pid'] and row['frame_base']==ready['frame_base']
            assert row['held_final_frames'] and row['hp1_drained'] and row['old_program_freed'] and not row['overlay_reloaded']
            switches.append(dict(from_app=row['from'],to_app=row['to'],seconds=row['total_seconds']))
        assert any(x['name']=='B_demo_home_menu' for x in observed['checks'])
        evidence=[Path(__file__),board/'result.json',board/'preparation.json',support/'result.json',support/'package/manifest.json',
                  *[p for p in run.iterdir() if p.suffix in ('.json','.jsonl')]]
        result.update(pass_=True,scope='Affected UI, slow SW telemetry, controls, default/adjacent60s and safe restoration; no independent human review',
                      program_sha256=manifest['files']['touhou.bin'],paired_bit_sha256=manifest['files']['top.bit'],
                      PS_sha256=manifest['files']['pynq_brotato_session.py'],mailbox_sha256=manifest['files']['bt_mailbox.py'],
                      metrics=metrics,switches=switches,coherent_CRC_packets=len(samples),PCM_CRC_snapshots=len(music_rows),
                      PCM_consumed_samples=sum(r['consumed_samples'] for r in finals),slow_SW_complete_packet_seconds=sw_seconds,
                      zero_faults=True,original_restored=True,boot_id=parent['after']['owners']['boot_id'],
                      evidence={p.relative_to(ROOT).as_posix():sha(p) for p in evidence})
    except Exception as error:result['error']=repr(error);raise
    finally:(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('SPACE_UI_RECEIVED_PASS',len(samples),'packets',len(music_rows),'PCM CRCs',sw_seconds,'seconds slow packet')


if __name__=='__main__':main()
