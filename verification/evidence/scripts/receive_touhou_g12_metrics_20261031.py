"""Receive all-frame histograms and object/stack ledgers from frozen G12 banks."""
import argparse
import copy
import json
from pathlib import Path
from build_work_demo_20261029 import ROOT,sha

BASE=ROOT/'reports/rvgame_longrun_20261031'


def checksum(sample):
    value=2166136261
    for word in sample['structure_telemetry']+sample['telemetry']:value=((value^word)*16777619)&0xffffffff
    return value


def receive(run,expected_static_stack=66452,scarlet=False,expected_game_bytes=45376):
    banks=run['banks'];assert len(banks)==8
    assert {s['structure_telemetry'][7] for s in banks}==set(range(8))
    assert len({(s['structure_telemetry'][1],s['structure_telemetry'][3],s['structure_telemetry'][5]) for s in banks})==1
    for s in banks:
        m=s['structure_telemetry'];assert len(m)==16 and len(s['telemetry'])==64 and checksum(s)==s['mailbox_checksum']
        assert m[0]==0x54474231 and m[15]==m[1]^0xffffffff and not m[12] and not(m[13]&8)
    data={s['structure_telemetry'][7]:s['telemetry'] for s in banks}
    cap=sum((data[i] for i in range(4)),[]);hist=data[4];custom=data[5];abi=data[7]
    n=cap[2];assert cap[:2]==[0x43415031,1] and n>0
    assert sum(cap[64:128])==sum(cap[128:192])==sum(hist)==custom[0]==n
    assert sum(cap[192:256])==n-1
    assert custom[22]==custom[23]==0
    if scarlet:assert custom[21]==custom[49] and custom[53]==0
    else:assert custom[21]==0
    assert custom[15]+(custom[16]<<32)-custom[17]-(custom[18]<<32)==custom[19]
    assert custom[20]<=custom[17]+(custom[18]<<32) and custom[45]-custom[46]==custom[44] and custom[47]<=custom[46]
    assert abi[0]==0x54414231 and abi[1]==0x49500100 and abi[2]==0x41505531
    assert abi[8:11]==[expected_game_bytes,128,40] and abi[11]==expected_static_stack and abi[14]==0 and abi[13]==abi[11]-abi[12]>=256
    assert abi[15]==128|(128<<16)
    final=run['final'];m=final['structure_telemetry'];d=final['telemetry'];assert m[0]==0x54475031 and m[15]==m[1]^0xffffffff
    assert checksum(final)==final['mailbox_checksum'] and not d[16]
    if scarlet:assert d[22]==d[53] and d[55]==0 and d[56]==3 and d[52] in (2026,2027,2028)
    else:assert not d[22]
    cycles=cap[3]|(cap[4]<<32);whole_cycles=d[32]|(d[33]<<32)
    fps=cap[13]*105000000/cycles if cycles else None
    def percentiles(values):
        size=sum(values);answer={}
        for percent in [95,99]:
            total=0;index=None
            for i,count in enumerate(values):
                total+=count
                if size and total>=(size*percent+99)//100:index=i;break
            answer['p'+str(percent)+'_upper_ms']=(index+1)*32768/105000 if index is not None and index<63 else None
        return answer
    return dict(name=run['name'],sample_scope='last metering window; comparison resets every600 frames',
        frames=n,seconds=cycles/105000000,completed_FPS=fps,
        window_strict_every_scan=bool(fps is not None and 59.9<=fps<=60.1 and cap[14]==cap[17]==cap[12]==0 and cap[5]==cap[13]==n-1),
        whole_application_seconds=whole_cycles/105000000,whole_application_FPS=(d[34]-1)*105000000/whole_cycles if whole_cycles else None,
        whole_application_nonunit_scans=d[36],whole_application_missed_scans=d[37],whole_application_max_scan_gap=d[38],
        whole_application_strict_every_scan=d[36]==d[37]==d[39]==0 and d[34]-1==d[35] and d[40]<=1750000,
        whole_application_work_max_ms=d[40]/105000,
        logic=dict(mean_ms=(custom[3]|(custom[4]<<32))/n/105000,min_ms=custom[6]/105000,max_ms=custom[5]/105000,**percentiles(hist)),
        draw=dict(mean_ms=(cap[6]|(cap[7]<<32))/n/105000,max_ms=cap[10]/105000,**percentiles(cap[64:128])),
        work=dict(mean_ms=(cap[8]|(cap[9]<<32))/n/105000,max_ms=cap[11]/105000,**percentiles(cap[128:192])),
        interval=dict(max_ms=cap[18]/105000,**percentiles(cap[192:256])),
        minimum_full_one_second_FPS=None if custom[24]==0xffffffff else custom[24]/100,
        enemies=dict(min=custom[7],max_observed=custom[8],mean=(custom[9]|(custom[10]<<32))/n),
        shots=dict(min=custom[11],max_observed=custom[12],mean=(custom[13]|(custom[14]<<32))/n),
        render=dict(attempted=custom[15]|(custom[16]<<32),submitted=custom[17]|(custom[18]<<32),fully_clipped=custom[19],alpha_submitted=custom[20],
                    last_attempted=custom[45],last_submitted=custom[46],last_fully_clipped=custom[44],last_alpha=custom[47]),
        stack=dict(static_available=abi[11],canary_upper_bound_peak=abi[12],remaining=abi[13],guard_bytes=256,
                   exact_deepest_SP=False,excluded_canary_tail_bytes=256),
        normal_or_comparison='comparison' if custom[36] else 'normal',software=custom[34],groups=custom[35],
        matched_UI=dict(current=data[6][:32],rendered=data[6][32:]),
        carried_world_peak=d[43],world_peak_may_include_ROM_snapshot_history=bool(custom[36]),
        expected_source_pool_rejects=custom[49] if scarlet else 0)


def fixture():
    cap=[0]*256;cap[:6]=[0x43415031,1,600,599*1750000,0,599]
    cap[6]=600*900000;cap[8]=600*1100000;cap[10:14]=[900000,1100000,0,599];cap[18]=1750000
    cap[64+27]=cap[128+33]=600;cap[192+53]=599
    hist=[0]*64;hist[6]=600;custom=[0]*64;custom[0]=600
    for i,v in {3:120000000,5:200000,6:200000,7:100,8:100,9:60000,11:5,12:5,13:3000,15:120000,17:108000,19:12000,20:6000,24:6000,32:599,34:0,35:1,36:1,44:20,45:200,46:180,47:10}.items():custom[i]=v
    abi=[0]*64;abi[:3]=[0x54414231,0x49500100,0x41505531];abi[8:16]=[45376,128,40,66452,384,66068,0,128|(128<<16)]
    view=[0]*64;view[:5]=[1,0,2,1,600];view[32:37]=[1,0,2,1,599]
    payloads=[cap[i*64:i*64+64] for i in range(4)]+[hist,custom,view,abi];banks=[]
    for bank,payload in enumerate(payloads):
        m=[0]*16;m[0]=0x54474231;m[1]=600;m[3]=1234;m[5]=5;m[7]=bank;m[11]=384;m[15]=600^0xffffffff
        s=dict(structure_telemetry=m,telemetry=payload);s['mailbox_checksum']=checksum(s);banks.append(s)
    m=[0]*16;m[0]=0x54475031;m[1]=600;m[11]=384;m[15]=600^0xffffffff;d=[0]*64
    d[32]=599*1750000;d[34]=600;d[35]=599;d[38]=1;d[40]=1100000;d[43]=110
    final=dict(structure_telemetry=m,telemetry=d);final['mailbox_checksum']=checksum(final)
    return dict(name='synthetic_unit_input',banks=banks,final=final)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--tag',required=True);ap.add_argument('--self-test',action='store_true');ap.add_argument('--board',type=Path);a=ap.parse_args()
    assert a.self_test != bool(a.board)
    out=BASE/('touhou_g12_metrics_'+a.tag);out.mkdir(exist_ok=False);r=dict(pass_=False,board_tested=not a.self_test,timing_values_are_synthetic_unit_inputs=a.self_test)
    try:
        if a.self_test:
            run=fixture();parsed=receive(run);assert parsed['window_strict_every_scan'] and parsed['whole_application_strict_every_scan']
            for key,value in [('logic',200000),('draw',900000),('work',1100000)]:
                upper=parsed[key]['p99_upper_ms'];assert value/105000<=upper<(value+32768)/105000
            bad=copy.deepcopy(run);bad['banks'][0]['telemetry'][14]=1;bad['banks'][0]['mailbox_checksum']=checksum(bad['banks'][0]);assert not receive(bad)['window_strict_every_scan']
            bad=copy.deepcopy(run);bad['banks'][4]['telemetry'][6]-=1;bad['banks'][4]['mailbox_checksum']=checksum(bad['banks'][4])
            try:receive(bad);raise RuntimeError('Missing histogram sample accepted')
            except AssertionError:pass
            r.update(pass_=True,strict_mean_near60_bad_control_rejected=True,histogram_loss_rejected=True,percentile_bounds_checked=True,
                     note='Synthetic unit inputs only, no FPS or board evidence')
        else:
            folder=a.board.resolve();br=json.loads((folder/'result.json').read_text());assert br['original_restored']
            points=[receive(run) for run in br['board_result']['runs'] if run.get('diagnostic_frozen')];assert points
            r.update(pass_=True,points=points,board_controller_pass=br['board_result']['pass_'],board_receipt_sha256=sha(folder/'result.json'))
        r['script_sha256']=sha(Path(__file__));print('TOUHOU_G12_METRICS_RECEIVED',r['pass_'],'board=',not a.self_test)
    except Exception as e:r['error']=repr(e);raise
    finally:(out/'result.json').write_text(json.dumps(r,indent=2)+'\n')


if __name__=='__main__':main()
