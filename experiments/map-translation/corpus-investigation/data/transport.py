"""Host transport only; all parsing/build/execution is in the owned bounded worker."""
import json, subprocess, tarfile, sys
from pathlib import Path
R=Path.home()/'.local/state/eqemu-map-translation/corpus-data-gaps-20261110'
assert len(sys.argv)==2 and sys.argv[1].isidentifier(), 'usage: transport.py NEW_RUN_ID'
R=R/sys.argv[1];R.mkdir(exist_ok=False)
D=['docker','--host','unix:///run/user/1000/eqemu-corpus-data.sock']
IMAGE='sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d'
def call(a):return subprocess.check_output(D+a,text=True,timeout=180).strip()
assert 'name=rootless' in json.loads(call(['info','--format','{{json .SecurityOptions}}']))
script='''set -eu
mkdir -p /results /inputs
cp /staged/* /inputs/
cmake -S /work/experiments/map-translation -B /capture -DGLM_INCLUDE_DIR=/candidate/build/vcpkg_installed/x64-linux/include -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /capture -j2
cp /capture/compile_commands.json /results/
sha256sum /capture/map-capture /work/zone/map_geometry.cpp > /results/source-binary.sha256
c++ --version > /results/compiler.txt
for name in breedinggrounds chapterhouse firiona_lit warslikswood_lit; do
 hash=$(sha256sum /inputs/$name.map | cut -d' ' -f1)
 /capture/map-capture /inputs/$name.map /results/$name.mesh "$hash"
 cmp /results/$name.mesh /inputs/$name.mesh
done
python3 /analysis/analyze.py
python3 /analysis/check_excerpts.py
sha256sum /analysis/*.py > /results/analysis-source.sha256
cat /sys/fs/cgroup/memory.peak > /results/memory.peak
'''
cid=call(['create','--pull','never','--network','none','--memory','2g','--memory-swap','2g','--cpus','2','--pids-limit','128','--entrypoint','/bin/bash',IMAGE,'-lc',script])
(R/'container-initial.json').write_text(call(['inspect',cid]))
try:
    archive=R/'source.tar'
    with archive.open('wb') as f:subprocess.run(['git','archive','2d6cc88a5ea891f79b72fc0e9530b35d7b2caec8'],stdout=f,check=True)
    src=R/'source';src.mkdir(exist_ok=True)
    with tarfile.open(archive) as t:t.extractall(src,filter='data')
    call(['cp',str(src)+'/.',cid+':/work'])
    call(['cp',str(Path(__file__).resolve().parent),cid+':/analysis'])
    staged=R/'staged';staged.mkdir(exist_ok=True)
    import shutil,hashlib
    inv=[]
    for name in ('breedinggrounds','chapterhouse','firiona_lit','warslikswood_lit','firiona','warslikswood'):
        p=Path('/home/bump/Projects/bump-eqemu/bump-akk-stack/server/maps/base')/(name+'.map')
        if not p.exists():continue
        b=p.read_bytes();h=hashlib.sha256(b).hexdigest()
        q=Path.home()/'.local/share/eqemu-upstream/state/server/maps/base'/p.name
        assert q.read_bytes()==b
        inv.append(dict(path='base/'+p.name,bytes=len(b),sha256=h))
        shutil.copyfile(p,staged/p.name)
        mesh=Path.home()/'.local/state/eqemu-map-translation/raycaster-corpus-20261110/final/assets/base'/name/'geometry.mesh'
        if mesh.exists():shutil.copyfile(mesh,staged/(name+'.mesh'))
    (R/'inputs.json').write_text(json.dumps(inv,indent=2))
    call(['cp',str(staged)+'/.',cid+':/staged'])
    with (R/'worker.log').open('w') as f:subprocess.run(D+['start','-a',cid],stdout=f,stderr=subprocess.STDOUT,timeout=600,check=True)
finally:
    try:
        if call(['inspect',cid,'--format','{{.State.Running}}'])=='true':call(['kill',cid])
        (R/'container-final.json').write_text(call(['inspect',cid]))
        call(['cp',cid+':/results/.',str(R)])
    finally:
        call(['rm','-f',cid])
