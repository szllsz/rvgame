"""Portable artifact-checker regression; no project or board dependency."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parent


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    spec=importlib.util.spec_from_file_location('artifact_checker',ROOT/'verify_artifacts.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    actual=b'\x10\x20\x30\x40';(out/'actual.bin').write_bytes(actual)
    (out/'golden.bin').write_bytes(bytes([16,32,48,64]));(out/'wrong.bin').write_bytes(bytes([16,32,255,64]))
    good={'files':{'actual.bin':hashlib.sha256(actual).hexdigest()},'comparisons':[{'actual':'actual.bin','reference':'golden.bin'}]}
    cases=[('match',good,True),('wrong_hash',{'files':{'actual.bin':'0'*64}},False),
           ('wrong_byte',{'comparisons':[{'actual':'wrong.bin','reference':'golden.bin'}]},False),
           ('empty_range',{'comparisons':[{'actual':'actual.bin','reference':'golden.bin','length':0}]},False),
           ('out_of_bounds',{'comparisons':[{'actual':'actual.bin','reference':'golden.bin','length':5}]},False),
           ('escape_root',{'files':{'../unrelated':'0'*64}},False),('empty_manifest',{},False),
           ('valid_slice',{'comparisons':[{'actual':'actual.bin','reference':'golden.bin','actual_offset':1,'reference_offset':1,'length':2}]},True)]
    results={}
    for name,manifest,expected in cases:
        result=module.verify(out,manifest);assert result['pass']==expected,(name,result);results[name]=result
    assert 'offset 2' in results['wrong_byte']['errors'][0]['error']
    result=dict(pass_=True,host_only=True,board_tested=False,results=results,checker_sha256=hashlib.sha256((ROOT/'verify_artifacts.py').read_bytes()).hexdigest())
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('COMPLETION_SKILL_PASS',len(cases),'behavior cases')


if __name__=='__main__':main()
