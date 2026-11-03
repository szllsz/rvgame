"""Portable offline recheck of the final two-game evidence and exact payloads.

Run from the packaged verification directory; no project workspace required.
"""
import argparse
import hashlib
import json
from pathlib import Path
from audit_helpers import checksum,crc_prefix
from scarlet_ui_reference_20261101 import UiCrc

HERE=Path(__file__).resolve().parent;B=HERE.parent;DATA=HERE/'data'
def read(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def payload(package,name):
    p=package/name
    if p.is_file():return p
    historical=DATA/'qualified_runtime'/name
    return historical if historical.is_file() else B/'runtime/package'/name


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);args=ap.parse_args();assert not args.output.exists()
    results=[]
    for label in ('star','egg'):
        package=DATA/label/'inputs';board=DATA/label/'board';run=board/'received';receipt=read(DATA/label/'summary.json')
        parent=read(board/'result.json');observed=read(run/'result.json');ready=read(run/'ready.json');manifest=read(package/'manifest.json');native=read(package/'candidate_manifest.json')
        assert parent['pass_'] and parent['original_restored'] and observed==parent['board_result'] and observed['pass_'] and observed['finished']
        assert sha(package/'manifest.json')==parent['manifest_sha256']==sha(run/'manifest.json')
        archive=[]
        for name,digest in manifest['files'].items():
            p=payload(package,name);assert sha(p)==digest,name
            if p.parent!=B/'runtime/package':archive.append(name)
        assert receipt['program_sha256']==sha(B/'runtime/package'/('touhou.bin' if label=='star' else 'game.bin'))
        assert manifest['files']['native_session.py']==sha(B/'runtime/package/native_session.py')
        if label=='egg':assert manifest['files']['touhou.bin']==sha(B/'runtime/package/touhou.bin')
        clean=read(run/'cleanup.json');assert clean==parent['cleanup']==observed['cleanup']
        assert clean['pass'] and not clean.get('error') and all(clean[k] for k in ('program_freed','display_freed','hp_restored','trace_writer_flushed'))
        samples=[json.loads(l) for l in (run/'samples.jsonl').read_text().splitlines()]
        for row in samples:
            assert checksum(row)==row['mailbox_checksum'] and row['underflows']==0 and not row['ddr_status']&1 and not row['display_status']&1
            if row.get('retirement_only'):assert row['retirement_verified'] and not row['retirement']['fault']
            else:assert row['coherent'] and row['structure_coherent']
        audio=[json.loads(l) for l in (run/'music_samples.jsonl').read_text().splitlines()]
        effects={k:payload(package,v['file']).read_bytes() for k,v in native['ui_effects'].items()};data={};mixers={};previous={}
        for row in audio:
            specs=[native['native_music'][row['application']]]
            if row['application']=='touhou':specs.append(native['touhou_title_music'])
            spec=next(s for s in specs if s['file']==row['music_file']);name=spec['file'];segment=row['segment']
            if name not in data:
                p=payload(package,name);assert sha(p)==spec['sha256'];data[name]=p.read_bytes()
            assert row['music_sha256']==spec['sha256'] and row['digital_crc_verified']
            assert row['underflows']==row['serial_faults']==row['serial_quiet_faults']==0
            assert previous.get(segment,0)<=row['consumed_samples']<=row['submitted_samples'];previous[segment]=row['consumed_samples']
            if row.get('ui_mixed'):
                if segment not in mixers:mixers[segment]=UiCrc(data[name],spec['loop_start'],spec['loop_end'],effects)
                expected=mixers[segment].accept_delta(row['consumed_samples'],row['ui_effects'],row['ui_effects_base'],row['ui_effects_total'],row['submitted_samples'])
            else:expected=crc_prefix(data[name],spec['loop_start'],spec['loop_end'],row['consumed_samples'])
            assert expected==row['crc32']
        finals=[read(p) for p in run.glob('music_segment_*_final.json')];assert finals and all(r in audio and r['paused'] for r in finals)
        switches=[read(p) for p in run.glob('switch_*.json')]
        for s in switches:assert s['pass_'] and s['pid']==ready['pid'] and s['held_final_frames'] and s['hp1_drained'] and s['old_program_freed'] and not s['overlay_reloaded']
        assert len(switches)==(4 if label=='star' else 7)
        for im in observed['images']:
            p=run/(im['name']+'.rgb332');assert p.stat().st_size==480000 and sha(p)==im['sha256']
        if label=='star':
            for measured,bound in zip(observed['touhou_performance'],receipt['metrics']):
                x,y=measured['start_sample'],measured['end_sample'];assert x in samples and y in samples
                a,b=x['telemetry'],y['telemetry'];assert a[60:63]==b[60:63]==[0,3,bound['density']]
                seconds=((b[32]|b[33]<<32)-(a[32]|a[33]<<32))/105000000;frames=b[34]-a[34];scans=b[35]-a[35]
                assert seconds>=60 and seconds==bound['seconds'] and frames/seconds==bound['completed_FPS']
                assert frames==bound['completed_frames'] and scans==bound['scans'] and b[37]-a[37]==bound['missed_scans']
            assert receipt['metrics'][0]['strict_every_scan'] and receipt['meets_three_targets']
        else:
            aux=[json.loads(l) for l in (run/'showcase_samples.jsonl').read_text().splitlines()]
            for row in aux:assert checksum(row)==row['mailbox_checksum'] and row['underflows']==0 and row['structure_telemetry'][12:15]==[20261028,1,0]
            for measured,bound in zip(observed['egg_performance'],receipt['metrics']):
                assert measured['held_wall_seconds']>=35 and measured['wave']==bound['wave'] and len(measured['closed_epochs'])>=2
                for point in measured['closed_epochs']:
                    row=point['sample'];assert row in aux;w=row['telemetry'][19:27]
                    seconds=(w[3]|w[4]<<32)/105000000;assert 7.8<=seconds<=8.2 and point['FPS']==w[6]/seconds
                    assert point['frames']==w[6] and point['scans']==w[5] and point['nonunit']==w[7]
                assert sum(p['FPS'] for p in measured['closed_epochs'])/len(measured['closed_epochs'])==bound['FPS']
            assert receipt['meets_three_targets']
        results.append(dict(application=label,pass_=True,CRC_packets=len(samples),PCM_CRC_snapshots=len(audio),safe_switches=len(switches),metrics=receipt['metrics'],archived_test_inputs=archive))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(dict(pass_=True,host_only=True,new_board_test=False,manual_tested=False,runs=results,
        scope='Independent offline arithmetic, packet/PCM CRC, payload binding and safe-retirement recheck; each Egg epoch excludes its first2s, unlike Star continuous60s tests'),indent=2))
    print('FINAL_VISUAL_OFFLINE_RECHECK_PASS')


if __name__=='__main__':main()
