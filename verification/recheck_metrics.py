"""Recompute CPU/graphics metrics from raw board records and implementation reports."""
import argparse
import hashlib
import json
from pathlib import Path
import re

HERE=Path(__file__).resolve().parent
BUNDLE=HERE.parent
DATA=HERE/'data'
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();assert not args.output.exists(),'Use a new output file'
    clock=read(BUNDLE/'hardware/recipe.json')['clock_hz'];assert clock==105000000
    bindings=[]
    def bind(path,names):
        manifest=read(path)['files']
        for name in names:
            actual=sha(BUNDLE/'runtime/package'/name)
            assert actual==manifest[name],(path.name,name)
            bindings.append(dict(file=name,sha256=actual,measurement_manifest=path.relative_to(HERE).as_posix()))
    bind(DATA/'cpu/manifest.json',['coremark.bin','validation.bin','top.bit','top.hwh'])
    observed=read(DATA/'cpu/result.json')
    raw=next(c['sample']['cpu_text'] for c in observed['checks'] if c['name']=='CPU_actual_4000_performance_validation')
    blocks=raw.split('RVGAME CPU SUITE ')[1:];assert len(blocks)==2
    expected=[[0xe9f5,0xe714,0x1fd7,0x8e3a,0x65c5],[0x18f2,0xe3c1,0x0747,0x8d84,0x5249]]
    cpu=[]
    for i,block in enumerate(blocks):
        ticks=int(re.search(r'Total ticks\s*:\s*(\d+)',block)[1])
        iterations=int(re.search(r'Iterations\s*:\s*(\d+)',block)[1])
        crc=[int(v,16) for v in re.findall(r'(?:seedcrc|crclist|crcmatrix|crcstate|crcfinal)\s*:\s*0x([0-9a-f]+)',block)]
        assert crc==expected[i] and iterations==4000
        seconds=ticks/clock;assert seconds>10
        cpu.append(dict(phase=i,iterations=iterations,ticks=ticks,clock_hz=clock,seconds=seconds,coremark=iterations/seconds,per_mhz=iterations/seconds/(clock/1e6),crc=crc))
    graphics=[]
    for name,applications in [('tile_bitmap',('gfx','bitmap')),('alpha',('alpha',))]:
        bind(DATA/'graphics'/(name+'_manifest.json'),[*(a+'.bin' for a in applications),'top.bit','top.hwh'])
        result=read(DATA/'graphics'/(name+'.json'))
        for run in result['runs']:
            app=run['sample']['session_app']
            if app not in applications:continue
            w=run['sample']['capacity_words'];seconds=(w[3]+(w[4]<<32))/clock
            assert seconds>=60 and w[13]==w[5]==w[2]-1 and not any(w[i] for i in (12,14,17))
            assert w[35]==0 and 59.9<=w[13]/seconds<=60.1
            assert run['measurement']['seconds']==seconds and run['measurement']['frames']==w[13] and run['measurement']['scans']==w[5]
            graphics.append(dict(application=app,objects=w[34],seconds=seconds,frames=w[13],scans=w[5],fps=w[13]/seconds,strict_every_scan=True))
    assert [r['objects'] for r in graphics]==[512,96,32]
    report=HERE/'hardware/qualified/acceptance/utilization_hier.rpt'
    rows={}
    for line in report.read_text(encoding='utf-8').splitlines():
        cells=[c.strip() for c in line.split('|')[1:-1]]
        if len(cells)>=10 and cells[0] in ('top','u_cpu'):
            rows[cells[0]]=[int(v) for v in cells[2:]]
    assert rows['top'][0]==23447 and rows['u_cpu'][:3]==[6387,5998,389]
    efficiency=cpu[0]['coremark']/rows['u_cpu'][0]
    result=dict(pass_=True,host_only=True,board_tested_this_run=False,cpu=cpu,graphics=graphics,
        resources=dict(full_soc=rows['top'],cpu_core=rows['u_cpu'],columns=['total_lut','logic_lut','lutram','srl','ff','ramb36','ramb18','dsp']),
        coremark_per_cpu_lut=efficiency,resource_report_sha256=sha(report),payload_bindings=bindings,
        scope='Recomputed from original measured ticks, CRCs, scan counters and post-route hierarchy; CPU-only LUT denominator')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('CPU_GRAPHICS_METRICS_PASS',len(bindings),'bindings; CoreMark/LUT',round(efficiency,5))

if __name__=='__main__':main()
