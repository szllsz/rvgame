"""Replay eight frozen RV32 targets, audit local includes, compare exact bins.

Standard-library only. No imports from the original workspace and no board use.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def response(path,values):
    path.write_text('\n'.join('"'+v.replace('\\','\\\\').replace('"','\\"')+'"' for v in values)+'\n',encoding='utf-8')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--bundle',type=Path,default=Path(__file__).resolve().parent)
    ap.add_argument('--destination',type=Path,required=True);ap.add_argument('--compiler',type=Path,required=True)
    ap.add_argument('--applications',nargs='+',help='Optional target subset; omitted rebuilds all eight')
    a=ap.parse_args();bundle=a.bundle.resolve();out=a.destination.resolve();compiler=a.compiler.resolve()
    receipt=json.loads((bundle/'result.json').read_text());assert receipt['pass_']
    assert sha(bundle/'recipes.json')==receipt['recipe_sha256']
    for name,digest in receipt['source_files'].items():assert sha(bundle/'source'/name)==digest,name
    recipes=json.loads((bundle/'recipes.json').read_text());assert set(recipes)==set(receipt['applications'])
    selected=set(a.applications) if a.applications else set(recipes)
    assert selected and selected<=set(recipes),('Unknown application',selected-set(recipes))
    out.mkdir(parents=True,exist_ok=False);shutil.copytree(bundle/'source',out/'source')
    result=dict(pass_=False,board_tested=False,applications={},selected_applications=sorted(selected),scope='Detached source rebuild on the same compiler host; no original workspace include permitted')
    try:
        assert compiler.is_file();suffix='.exe' if os.name=='nt' else ''
        objcopy=compiler.with_name(compiler.name[:-len('gcc'+suffix)]+'objcopy'+suffix)
        version=subprocess.check_output([str(compiler),'--version']).decode().splitlines()[0]
        for app,recipe in recipes.items():
            if app not in selected:continue
            work=out/app;work.mkdir();dependencies=set()
            for index,values in enumerate(recipe['commands']):
                # GCC's dependency pass follows actual conditional include paths.
                dep=[];skip=False
                for value in values:
                    if skip:skip=False;continue
                    if value in ('-o','-T'):skip=True;continue
                    if value.startswith('-Wl,') or value in ('-lgcc','-nostdlib','-c') or value.endswith('.o'):continue
                    dep.append(value)
                response(work/'dependencies.rsp',['-MM','-MT','PACKED',*dep])
                run=subprocess.run([str(compiler),'@dependencies.rsp'],cwd=work,capture_output=True,timeout=180)
                (work/('dependencies_'+str(index)+'.log')).write_bytes(run.stdout+run.stderr)
                assert run.returncode==0,(app,run.stderr.decode(errors='replace'))
                text=run.stdout.decode().replace('\\\r\n',' ').replace('\\\n',' ')
                for row in text.splitlines():
                    assert row.startswith('PACKED:'),row
                    for token in re.findall(r'(?:\\[ \t#]|[^\s])+',row.partition(':')[2]):
                        p=(work/token.replace('\\ ',' ').replace('\\#','#')).resolve()
                        assert p.is_relative_to(out/'source'),'Include outside frozen sources: '+str(p)
                        dependencies.add(p.relative_to(out/'source').as_posix())
                response(work/'compile.rsp',values)
                run=subprocess.run([str(compiler),'@compile.rsp'],cwd=work,capture_output=True,timeout=180)
                (work/('compile_'+str(index)+'.log')).write_bytes(run.stdout+run.stderr)
                assert run.returncode==0,(app,run.stderr.decode(errors='replace')[-2500:])
            subprocess.run([str(objcopy),'-O','binary',app+'.elf',app+'.bin'],cwd=work,check=True,capture_output=True)
            digest=sha(work/(app+'.bin'));assert digest==recipe['expected_binary_sha256']==receipt['applications'][app]['binary_sha256'],app
            result['applications'][app]=dict(pass_=True,sha256=digest,bytes=(work/(app+'.bin')).stat().st_size,
                                           dependencies=sorted(dependencies),compiler_version=version,compiler_sha256=sha(compiler))
            print('DETACHED_REBUILD_IDENTICAL',app,flush=True)
        result.update(pass_=True,recipe_sha256=sha(bundle/'recipes.json'),source_receipt_sha256=sha(bundle/'result.json'))
    except Exception as error:result['error']=repr(error);raise
    finally:(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
