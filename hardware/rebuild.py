"""Detached native800 source/IP reconstruction and optional full implementation.

No board connection, old project, cached IP products or old DCP is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import zipfile


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--bundle',type=Path,default=Path(__file__).resolve().parent)
    ap.add_argument('--destination',type=Path,required=True);ap.add_argument('--vivado',type=Path,required=True)
    ap.add_argument('--stop-after-platform',action='store_true');a=ap.parse_args()
    bundle=a.bundle.resolve();out=a.destination.resolve();vivado=a.vivado.resolve()
    manifest=json.loads((bundle/'recipe.json').read_text())
    for name,digest in manifest['files'].items():assert sha(bundle/name)==digest,name
    assert not out.exists() and vivado.is_file()
    out.mkdir(parents=True);shutil.copytree(bundle/'inputs',out/'inputs')
    shutil.copyfile(bundle/'export.tcl',out/'export.tcl')
    result=dict(pass_=False,board_tested=False,source_only=True,original_project_used=False,stages=[],recipe_sha256=sha(bundle/'recipe.json'))
    def save():(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    try:
        stages=[('recreate',out/'inputs/build/recreate.tcl',[out/'vivado']),
                ('platform',out/'inputs/build/finish_platform.tcl',[out])]
        if not a.stop_after_platform:stages.extend([('implementation',out/'inputs/build/implement.tcl',[out]),('export',out/'export.tcl',[out,out/'products'])])
        for stage,entry,args in stages:
            wrapper=out/(stage+'_entry.tcl')
            wrapper.write_text('set taskStandardChannels [chan names]\nputs stdout "TOOL_STDOUT_INITIALIZED"\nputs stderr "TOOL_STDERR_INITIALIZED"\nflush stdout\nflush stderr\nsource {'+entry.as_posix()+'}\n',encoding='utf-8')
            command=[str(vivado),'-mode','batch','-source',str(wrapper),'-log',str(out/(stage+'.log')),'-journal',str(out/(stage+'.jou')),'-tclargs',*map(str,args)]
            start=time.time();save()
            with (out/(stage+'_console.log')).open('wb') as log:
                run=subprocess.run(command,cwd=out,stdout=log,stderr=subprocess.STDOUT)
            result['stages'].append(dict(stage=stage,exit_code=run.returncode,seconds=time.time()-start,command=command));save()
            assert run.returncode==0,stage+' failed; retain logs and original stable release'
            print('DETACHED_HARDWARE_STAGE_PASS',stage,flush=True)
        project=out/'vivado/system640_clean.xpr'
        assert project.exists()
        # Original workspace paths must not occur in the regenerated project.
        text=project.read_text(encoding='utf-8')
        assert manifest['original_source_root'].replace('\\','/') not in text
        if not a.stop_after_platform:
            with zipfile.ZipFile(out/'products/system640.xsa') as archive:
                names=[n for n in archive.namelist() if n.endswith('.hwh')];assert len(names)==1
                (out/'products/top.hwh').write_bytes(archive.read(names[0]))
            result['products']={p.name:sha(p) for p in (out/'products').iterdir() if p.suffix in ('.bit','.hwh','.xsa')}
        result.update(pass_=True,scope='project/BD/IP reconstruction only' if a.stop_after_platform else 'source-only synthesis/route/physical script gates and bit/XSA export; not new board qualification')
    except Exception as error:result['error']=repr(error);raise
    finally:save()


if __name__=='__main__':main()
