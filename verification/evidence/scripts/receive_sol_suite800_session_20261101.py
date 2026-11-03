"""Independent native800 suite switching, CRC/audio and CPU report reception."""
import argparse
import json
from pathlib import Path
import re
import subprocess
from receive_touhou_g12_metrics_20261031 import checksum
from run_scarlet_music_board_20261031 import crc_prefix
from scarlet_resources_20261031 import ROOT, BASE, sha
from scarlet_ui_reference_20261101 import UiCrc


def read(path):return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--tag',required=True);parser.add_argument('--board',type=Path,required=True)
    parser.add_argument('--scope',choices=['functional','gamecurve','feedback'],default='functional')
    parser.add_argument('--title-assets',type=Path)
    parser.add_argument('--pixels',type=Path);parser.add_argument('--cpu-pixels',type=Path);args=parser.parse_args()
    board=args.board.resolve();parent=read(board/'result.json');run=board/'received';observed=read(run/'result.json')
    assert parent['pass_'] and parent['original_restored'] and observed['pass_'] and not observed.get('error')
    support=ROOT/read(board/'preparation.json')['parent_support'];manifest=read(support/'package/manifest.json')
    assert sha(support/'package/manifest.json')==parent['manifest_sha256']
    for name,digest in manifest['files'].items():assert sha(support/'package'/name)==digest,name
    candidate=read(run/'candidate_manifest.json');assert candidate['native_suite800'] and candidate['native_geometry']==[800,600]
    assert sha(run/'candidate_manifest.json')==manifest['files']['candidate_manifest.json']
    assert parent['before']['owners']['boot_id']==parent['after']['owners']['boot_id']
    assert parent['released_owner_snapshot']==parent['restore_admission']==parent['retired_N']['after']
    protection=parent['before']['latest_protected_files']
    assert protection>0 and parent['before']['protected_files']==140
    assert parent['after']['protected_files']==140 and parent['after']['latest_protected_files']==protection
    cleanup=read(run/'cleanup.json');assert cleanup['pass'] and not cleanup.get('error')
    assert all(cleanup[k] for k in ('program_freed','display_freed','hp_restored','trace_writer_flushed'))
    assert not (run/'owner_error.json').exists()
    ready=read(run/'ready.json');assert ready['geometry']==[800,600] and ready['session_start']=='menu' and ready['test_input']
    samples=[json.loads(line) for line in (run/'samples.jsonl').read_text().splitlines()]
    assert samples
    for row in samples:
        assert row['coherent'] and row['structure_coherent'] and checksum(row)==row['mailbox_checksum']
        assert not row['underflows'] and not row['ddr_status']&1 and not row['display_status']&1
    switches=[read(p) for p in sorted(run.glob('switch_*.json'))];assert switches
    exits=dict(coremark=0x362,validation=0x362,gfx=0x37f,bitmap=0x38f,alpha=0x39f)
    for item in switches:
        assert item['pass_'] and item['frame_base']==ready['frame_base'] and item['pid']==ready['pid']
        assert not item['overlay_reloaded'] and all(item[k] for k in ('display_buffer_retained','immutable_image_cached',
            'held_final_frames','hp1_drained','old_program_freed','display_kept_enabled'))
        if item['from'] in exits:
            proof=item['plain_retirement'];assert proof['safe_to_hold'] and proof['kind']=='plain_exit'
            assert proof['exit_page']==exits[item['from']] and not proof['frame_status']&2
        else:
            final=item['final'];assert final in samples and final['mailbox_final'] and final['session_segment']==item['segment']
    audio=[json.loads(line) for line in (run/'music_samples.jsonl').read_text().splitlines()];assert audio
    previous={};pcm={};consumed={};segment_sources={};ui_mixers={};effects={}
    for kind,spec in candidate.get('ui_effects',{}).items():
        file=support/'package'/spec['file'];assert sha(file)==spec['sha256'] and manifest['files'][spec['file']]==spec['sha256']
        assert spec['sample_rate']==32000;effects[kind]=file.read_bytes()
    for row in audio:
        app=row['application'];segment=row['segment'];allowed=[candidate['native_music'][app]]
        if app=='touhou' and candidate.get('touhou_frontend'):allowed.append(candidate['touhou_title_music'])
        choices=[spec for spec in allowed if spec['file']==row.get('music_file',candidate['native_music'][app]['file'])];assert len(choices)==1
        spec=choices[0];key=spec['file'];assert row.get('music_sha256',spec['sha256'])==spec['sha256']
        assert segment_sources.setdefault(segment,key)==key
        if key not in pcm:
            file=support/'package'/key;assert sha(file)==spec['sha256'];pcm[key]=file.read_bytes()
        assert row['digital_crc_verified'] and previous.get(segment,0)<=row['consumed_samples']<=row['submitted_samples']
        assert row['underflows']==row['serial_faults']==row['serial_quiet_faults']==0
        if row.get('ui_mixed'):
            assert app=='touhou' and set(effects)=={'select','confirm','cancel'}
            if segment not in ui_mixers:ui_mixers[segment]=UiCrc(pcm[key],spec['loop_start'],spec['loop_end'],effects)
            expected=ui_mixers[segment].accept_delta(row['consumed_samples'],row['ui_effects'],row['ui_effects_base'],row['ui_effects_total'],row['submitted_samples'])
        else:expected=crc_prefix(pcm[key],spec['loop_start'],spec['loop_end'],row['consumed_samples'])
        assert row['crc32']==expected
        previous[segment]=row['consumed_samples']
        if row.get('final'):assert row['paused'];consumed[segment]=row['consumed_samples']
    for file in run.glob('music_segment_*_final.json'):assert read(file) in audio
    cpu=[];curve=[];images=[];defaults=[];scarlet=[]
    if args.scope=='functional':
        assert observed['functional_smoke'] and len(switches)==(18 if observed.get('final_defaults_all_strict60') else 12)
        names={item['name'] for item in observed['checks']}
        for app in ('coremark','gfx','bitmap','alpha','game','touhou'):
            assert 'entered_'+app in names and 'returned_from_'+app in names
        for app in ('gfx','bitmap','alpha'):assert app+'_actual_HW_SW' in names
        for item in observed['checks']:
            if item['name']!='CPU_actual_4000_performance_validation':continue
            text=item['sample']['cpu_text'];chunks=text.split('RVGAME CPU SUITE validation\n');assert len(chunks)==2
            assert text.count('Correct operation validated.')==2 and 'ERROR!' not in text and 'Errors detected' not in text
            for phase,chunk in enumerate(chunks):
                ticks=int(re.search(r'Total ticks\s*:\s*(\d+)',chunk)[1]);assert 1050000000<=ticks<2**32
                assert int(re.search(r'Iterations\s*:\s*(\d+)',chunk)[1])==4000
                values=[int(re.search(key+r'\s*:\s*0x([0-9a-fA-F]+)',chunk)[1],16) for key in ('seedcrc','crclist','crcmatrix','crcstate','crcfinal')]
                assert values==([0xe9f5,0xe714,0x1fd7,0x8e3a,0x65c5] if phase==0 else [0x18f2,0xe3c1,0x747,0x8d84,0x5249])
                cpu.append(dict(phase=phase,ticks=ticks,seconds=ticks/105000000,CRC=values))
        for item in observed['checks']:
            if item['name'].endswith('_actual_HW_SW') and 'fps_history' in item['software']:
                w=item['software']['fps_history'];assert w[0]==0x46505332 and w[2]==3
                for mode in range(2):assert w[3+mode]==w[5+mode]*105000000*100//w[7+mode]
            if '_native_reference_' in item['name']:
                assert args.pixels and read(args.pixels)['pass_']
                assert sha(args.pixels)==read(support/'result.json')['pixel_host_sha256']
                row=item['sample'];app=row['session_app'];f=row['footer'];assert f[4]==0 and f[10]==1
                actual=(run/row['image_file']).read_bytes();assert len(actual)==480000
                expected=subprocess.run([str(args.pixels.resolve().parent/(app+'_scalar.exe')),str(f[2]),str(f[3]),'0','0'],capture_output=True,check=True).stdout
                assert len(expected)==480000
                differences=sum(a!=b for a,b in zip(actual[:-64],expected[:-64]));assert not differences,(item['name'],differences)
                images.append(dict(name=item['name'],pixels_checked=479936,metadata_bytes_checked_separately=64,differences=0,
                                   sha256=sha(run/row['image_file'])))
        if cpu:
            assert args.cpu_pixels and read(args.cpu_pixels)['pass_']
            for item in observed['checks']:
                if item['name'] not in ('CPU_actual_4000_performance_validation','CPU_actual_details'):continue
                row=item['sample'];assert row.get('image_file')
                actual=(run/row['image_file']).read_bytes();assert len(actual)==480000
                scenario='3' if item['name']=='CPU_actual_details' else '2'
                expected=subprocess.run([str(args.cpu_pixels.resolve().parent/'scalar.exe'),scenario,*[str(p['ticks']) for p in cpu]],capture_output=True,check=True).stdout
                assert len(expected)==480000
                differences=sum(a!=b for a,b in zip(actual[:800*592],expected[:800*592]));assert not differences,(item['name'],differences)
                images.append(dict(name=item['name'],pixels_checked=800*592,diagnostic_bytes_checked_via_CRC_reports=6400,differences=0,
                                   sha256=sha(run/row['image_file'])))
        if observed.get('final_defaults_all_strict60'):
            assert len(observed['runs'])==3 and observed['performance_qualified']
            for item in observed['runs']:
                w=item['sample']['capacity_words'];n=w[2];seconds=(w[3]+(w[4]<<32))/105000000
                assert w[34]==candidate['native_capacity_defaults'][item['sample']['session_app']] and w[35]==0
                assert seconds>=60 and w[13]==w[5]==n-1 and not any(w[i] for i in (12,14,17))
                assert 59.9<=w[13]/seconds<=60.1
                assert sum(w[64:128])==sum(w[128:192])==w[44]==n and sum(w[192:256])==n-1 and sum(w[45:64])==n
                defaults.append(dict(name=item['name'],count=w[34],seconds=seconds,frames=w[13],scans=w[5],FPS=w[13]/seconds,strict60=True))
        for item in observed.get('touhou_performance',[]):
            before=item['start_sample'];last=item['end_sample'];a=before['telemetry'];b=last['telemetry']
            assert before in samples and last in samples and before['session_app']==last['session_app']=='touhou'
            assert before['session_segment']==last['session_segment'] and a[60:63]==b[60:63]==[0,3,item['density']]
            seconds=((b[32]|b[33]<<32)-(a[32]|a[33]<<32))/105000000;assert seconds>=60
            frames=b[34]-a[34];scans=b[35]-a[35];nonunit=b[36]-a[36];missed=b[37]-a[37];logic=b[39]-a[39]
            stats=dict(seconds=seconds,completed_frames=frames,scans=scans,completed_FPS=frames/seconds,
                       nonunit_scans=nonunit,missed_scans=missed,missed_logic_ticks=logic,
                       strict_every_scan=frames==scans and nonunit==missed==logic==0 and 59.9<=frames/seconds<=60.1)
            assert stats==item['timed_interval'];scarlet.append(dict(density=item['density'],**stats))
        if scarlet:assert len(scarlet)==2 and scarlet[0]['strict_every_scan'] and [p['density'] for p in scarlet]==[candidate['scarlet_density']['default'],candidate['scarlet_density']['default']+1]
    elif args.scope=='feedback':
        assert observed['feedback_smoke'] and candidate['touhou_frontend']
        showcase_only=bool(candidate.get('touhou_showcase_only'))
        assert observed.get('showcase_only') if showcase_only else observed['frontend_directory_only']
        assert args.title_assets and sha(args.title_assets/'result.json')==candidate['title_assets_sha256']
        title=read(args.title_assets/'result.json');assert title['pass_']
        names={item['name'] for item in observed['checks']}
        required={'touhou_title','touhou_pause_B_returns_to_game','touhou_demo_B_returns_home'}
        if showcase_only:required.add('touhou_showcase_entered')
        else:
            required.update(('showcase_choice','catalog_confirm_stays_directory'));required.update('catalog_'+str(i) for i in range(6))
        required.update('returned_from_'+app for app in ('touhou','game','alpha','bitmap','gfx'))
        assert required<=names
        for item in observed['checks']:
            name=item['name']
            index=(0 if name in ('touhou_title','touhou_demo_B_returns_home') else None) if showcase_only else 0 if name=='touhou_title' else 1 if name=='showcase_choice' else int(name.split('_')[1])+2 if re.fullmatch(r'catalog_[0-5]',name) else None
            if index is None:continue
            sample=item['sample'];assert sample in samples and sample['coherent'] and sample.get('image_file')
            actual=(run/sample['image_file']).read_bytes();expected=(args.title_assets/(str(index)+'.rgb332')).read_bytes()
            assert len(actual)==len(expected)==480000 and actual==expected,name
            images.append(dict(name=name,pixels_checked=480000,differences=0,sha256=sha(run/sample['image_file'])))
        assert len(images)==(2 if showcase_only else 8)
        pause_images=[]
        for item in observed['checks']:
            if not re.fullmatch(r'touhou_pause_modal_[0-2]',item['name']):continue
            sample=item['sample'];assert sample in samples and sample.get('image_file')
            word=sample['telemetry'][58];assert word&0xffff0000==0x46530000
            choice=word&3;volume=(word>>2)&15;muted=(word>>6)&1;density=(word>>7)&15;scene=(word>>11)&3
            assert choice==int(item['name'][-1]) and max(volume,density,scene)<9
            expected=bytearray((args.title_assets/('sc_pause_panel_'+str(choice)+'.rgb332')).read_bytes())
            def overlay(raw,w,h,x,y):
                assert len(raw)==w*h
                for yy in range(h):
                    for xx in range(w):
                        color=raw[yy*w+xx]
                        if color:expected[(y+yy)*520+x+xx]=color
            for i,value in enumerate((volume,density,scene)):
                overlay((args.title_assets/('sc_pause_digit_'+str(value)+'.rgb332')).read_bytes(),36,44,423,90+i*60)
            overlay((args.title_assets/('sc_pause_mute_'+str(muted)+'.rgb332')).read_bytes(),108,26,368,31)
            full=(run/sample['image_file']).read_bytes();assert len(full)==480000
            actual=b''.join(full[(120+y)*800+140:(120+y)*800+660] for y in range(360))
            assert actual==expected,item['name']
            pause_images.append(dict(name=item['name'],pixels_checked=520*360,differences=0,sha256=sha(run/sample['image_file'])))
        if effects:assert len(pause_images)==3
        banners=0
        for item in switches:
            if item['to']!='menu':continue
            assert item['return_banner_file'] and item['transition_banner_updates']>0
            expected=bytearray((support/'package/transition_menu_2.rgb332').read_bytes())
            for row in range(52,60):expected[row*800+32:row*800+768]=bytes([63])*736
            assert (run/item['return_banner_file']).read_bytes()==expected
            banners+=1
        assert banners==5
        assert len(observed['runs'])==1
        item=observed['runs'][0];w=item['sample']['capacity_words'];n=w[2];seconds=(w[3]+(w[4]<<32))/105000000
        assert item['sample']['session_app']=='alpha' and w[34]==candidate['native_capacity_defaults']['alpha'] and w[35]==0
        assert seconds>=60 and 59.9<=w[13]/seconds<=60.1 and w[13]==w[5]==n-1 and not any(w[i] for i in (12,14,17))
        defaults.append(dict(name=item['name'],count=w[34],seconds=seconds,frames=w[13],scans=w[5],FPS=w[13]/seconds,strict60=True))
        assert len(observed['touhou_performance'])==1
        item=observed['touhou_performance'][0];a=item['start_sample']['telemetry'];b=item['end_sample']['telemetry']
        seconds=((b[32]|b[33]<<32)-(a[32]|a[33]<<32))/105000000;frames=b[34]-a[34];scans=b[35]-a[35]
        assert seconds>=60 and frames==scans and all(b[i]==a[i] for i in (36,37,39)) and 59.9<=frames/seconds<=60.1
        assert a[60:63]==b[60:63]==[0,3,candidate['scarlet_density']['default']]
        scarlet.append(dict(density=item['density'],seconds=seconds,completed_frames=frames,scans=scans,completed_FPS=frames/seconds,strict_every_scan=True))
        assert any(row['music_file']==candidate['touhou_title_music']['file'] for row in audio if row['application']=='touhou')
        assert any(row['music_file']==candidate['native_music']['touhou']['file'] for row in audio if row['application']=='touhou')
        if effects:
            assert {event['kind'] for mixer in ui_mixers.values() for event in mixer.events}==set(effects)
            assert ready['test_input']
    else:
        assert observed['observation_seconds']>=60 and len(observed['closed_epochs'])>=6
        aux=[json.loads(line) for line in (run/'showcase_samples.jsonl').read_text().splitlines()]
        for key,point in observed['closed_epochs'].items():
            assert any(row['telemetry'][19:27]==point['meter'] for row in aux)
            w=point['meter'];seconds=(w[3]+(w[4]<<32))/105000000;fps=w[6]/seconds
            strict=w[5]==w[6]==w[2]-1 and w[7]==0 and 59.9<=fps<=60.1
            assert abs(fps-point['FPS'])<1e-10 and strict==point['stage_strict60']
            curve.append(dict(epoch=int(key),population=w[1],seconds=seconds,FPS=fps,stage_strict60=strict,missed=w[5]-w[6]))
    result=dict(pass_=True,board_tested=True,functional_scope=args.scope,geometry=[800,600],switches=len(switches),
                coherent_CRC_packets=len(samples),PCM_CRC_snapshots=len(audio),PCM_consumed_samples=sum(consumed.values()),
                CPU_reports=cpu,CPU_metrics_unchanged=True,pixel_reception=images,final_defaults=defaults,native_touhou_windows=scarlet,closed_game_epochs=curve,original_restored=True,
                pause_pixel_reception=pause_images if args.scope=='feedback' else [],UI_SFX_segments={str(k):v.events for k,v in ui_mixers.items()},
                same_boot=True,protected_files=[protection,140],manual_tested=False,adopted=False,
                evidence={p.relative_to(ROOT).as_posix():sha(p) for p in [board/'result.json',run/'result.json',run/'samples.jsonl',
                    run/'music_samples.jsonl',support/'package/manifest.json',Path(__file__)]})
    out=BASE/('sol_suite800_session_received_'+args.tag);out.mkdir(exist_ok=False)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('NATIVE800_SUITE_SESSION_RECEIVED',len(switches),len(audio),len(cpu),curve)


if __name__=='__main__':main()
