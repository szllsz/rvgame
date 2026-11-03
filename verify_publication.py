"""Offline verification of public files, payloads and build input manifests."""
import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parent


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8'))


def main():
    manifest=read(ROOT/'publication_manifest.json')
    for name,entry in manifest['files'].items():
        path=(ROOT/name).resolve()
        assert path.is_relative_to(ROOT) and path.is_file(),name
        assert path.stat().st_size==entry['bytes'] and sha(path)==entry['sha256'],name
        assert path.stat().st_size<100*1024*1024,name
        assert re.fullmatch(r'[a-z0-9_./-]+',name) or path.name in ('README.md','SKILL.md','LICENSE'),name
        if path.suffix=='.py':ast.parse(path.read_text(encoding='utf-8-sig'),filename=name)
    for folder,filename,key in [('software','result.json','source_files'),('hardware','recipe.json','files')]:
        record=read(ROOT/folder/filename)
        base=ROOT/folder/('source' if key=='source_files' else '')
        for name,digest in record[key].items():assert sha(base/name)==digest,(folder,name)
    sw=read(ROOT/'software/result.json');recipes=read(ROOT/'software/recipes.json')
    runtime_names={p.relative_to(ROOT/'runtime').as_posix() for p in (ROOT/'runtime').rglob('*') if p.is_file()}
    assert set(read(ROOT/'runtime/release_manifest.json')['files'])<=runtime_names,'Runtime paths must match filename case on Linux'
    assert len(recipes)==8 and sha(ROOT/'software/recipes.json')==sw['recipe_sha256']
    for app,recipe in recipes.items():assert sha(ROOT/'runtime/package'/(app+'.bin'))==recipe['expected_binary_sha256']
    for case in read(ROOT/'sim/fixtures.json')['cases']:
        for name,digest in case['files'].items():assert sha(ROOT/'sim/cases'/case['name']/name)==digest,(case['name'],name)
    subprocess.run([sys.executable,'-B',str(ROOT/'runtime/run.py')],check=True)
    subprocess.run([sys.executable,'-B',str(ROOT/'runtime/usb_touch/install.py')],check=True)
    print('PUBLICATION_VERIFIED',len(manifest['files']),'files; eight targets; no board operations')


if __name__=='__main__':main()
