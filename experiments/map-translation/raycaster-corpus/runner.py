"""Opt-in host transport. Native execution occurs only in isolated rootless containers."""
import argparse, collections, hashlib, json, os, shutil, struct, subprocess, sys, tarfile, time
from pathlib import Path
IMAGE='sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d'
ROOT=Path(__file__).resolve().parents[3]
STATE=Path.home()/'.local/state/eqemu-map-translation/raycaster-corpus-20261110'
def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()
def write(p,x):
    temp=p.with_suffix('.tmp'); temp.write_text(json.dumps(x,indent=2)); temp.replace(p)
def census(root):
    result=[]
    for p in sorted([*(root/'base').glob('*.map'),*(root/'legacy/base').glob('*.map')]):
        assert p.is_file() and not p.is_symlink()
        with p.open('rb') as f: header=f.read(4)
        result.append({'path':str(p.relative_to(root)),'bytes':p.stat().st_size,'sha256':sha(p),'version':header.hex()})
    return result
def order(records):
    first=['base/griegsend.map','base/qeynos2.map','base/poknowledge.map']
    return sorted(records,key=lambda r:(first.index(r['path']) if r['path'] in first else 3,r['path']))
def cleanup(directory):
    removed=[]
    for name in ('geometry.mesh','queries.bin','reference.trace','candidate.bin','candidate.trace'):
        p=directory/name
        if p.exists(): removed.append({'name':name,'bytes':p.stat().st_size,'sha256':sha(p)}); p.unlink()
    return removed
BUILD='''set -eu
mkdir -p /results
cmake -S /work/experiments/map-translation -B /capture -DGLM_INCLUDE_DIR=/candidate/build/vcpkg_installed/x64-linux/include -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /capture -j2
cmake -S /work/experiments/map-translation/collision -B /collision -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /collision -j2
cd /work/experiments/map-translation/rust-raycaster
export CARGO_TARGET_DIR=/rust-build
cargo build --release --frozen --offline
cargo test --release --frozen --offline > /results/tests.txt
python3 /work/experiments/map-translation/raycaster-corpus/controls.py
/collision/collision-eval --controls > /results/cpp-controls.txt
MAP_CAPTURE_BINARY=/capture/map-capture python3 ../test_capture.py > /results/capture-tests.txt 2>&1
/capture/map-capture --runtime-info > /results/fp.json
rustc -Vv > /results/rustc.txt
c++ --version > /results/compiler.txt
cargo tree --frozen --offline > /results/dependencies.txt
cp /capture/compile_commands.json /results/capture-compile.json
cp /collision/compile_commands.json /results/collision-compile.json
sha256sum /capture/map-capture /collision/collision-eval /rust-build/release/rust-raycaster-poc > /results/binaries.sha256
ldd /capture/map-capture > /results/decoder-ldd.txt
ldd /collision/collision-eval > /results/reference-ldd.txt
ldd /rust-build/release/rust-raycaster-poc > /results/candidate-ldd.txt
'''
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--socket',required=True); p.add_argument('--collection',type=Path,required=True); p.add_argument('--deployed',type=Path,required=True)
    p.add_argument('--run',default='qualification'); p.add_argument('--only',help='exact manifest path for regeneration'); p.add_argument('--retain',action='store_true'); p.add_argument('--resume',action='store_true')
    a=p.parse_args(); assert a.run.isidentifier(), 'simple run name required'
    assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip(), 'clean committed source required'
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    d=['docker','--host',a.socket]
    def call(args,**kw): return subprocess.check_output(d+args,timeout=180,**kw)
    assert 'name=rootless' in json.loads(call(['info','--format','{{json .SecurityOptions}}'],text=True))
    out=STATE/a.run; out.mkdir(parents=True,exist_ok=a.resume)
    records=census(a.collection); deployed=census(a.deployed)
    assert records and any(x['path']=='base/griegsend.map' for x in records), 'missing corpus'
    prior=Path.home()/'.local/state/eqemu-map-translation/investigation-2026-11-10/asset-census.json'
    prior_data=json.loads(prior.read_text())['collections'][0]['maps']
    old={r['path']:r['sha256'] for r in prior_data}; current={r['path']:r['sha256'] for r in records}
    write(out/'census-comparison.json',{'prior_sha256':sha(prior),'prior_paths':sorted(old),'current_paths':sorted(current),'changed_or_added':[p for p in current if current[p]!=old.get(p)],'removed':sorted(old.keys()-current.keys())})
    assert records==deployed, 'deployed and source inventories differ'
    identity={'source':commit,'image':IMAGE,'inputs':records,'collection':str(a.collection.resolve()),'deployed':str(a.deployed.resolve()),'only':a.only,'retain':a.retain,'memory':2147483648,'stage_timeout':120,'container_timeout':420,'recipe':'driver.cpp 0x854f2026'}
    if a.resume: assert json.loads((out/'identity.json').read_text())==identity, 'resume identity mismatch'
    else: write(out/'identity.json',identity); write(out/'command.json',sys.argv)
    entries=order(records)
    if a.only:
        entries=[x for x in entries if x['path']==a.only]; assert len(entries)==1
    tag='raycaster-corpus-build:'+commit[:12]
    def container(script,dest,setup,timeout):
        cid=call(['create','--pull','never','--network','none','--memory','2g','--memory-swap','2g','--cpus','2','--pids-limit','128','--entrypoint','/bin/bash',IMAGE if dest.name=='build' else tag,'-lc',script],text=True).strip()
        write(dest/'container.json',json.loads(call(['inspect',cid])))
        try:
            setup(cid)
            with (dest/'worker.log').open('w') as log:
                try: transport=subprocess.run(d+['start','-a',cid],stdout=log,stderr=subprocess.STDOUT,timeout=timeout).returncode
                except subprocess.TimeoutExpired: call(['kill',cid]); transport='timeout'
            final=json.loads(call(['inspect',cid])); write(dest/'final.json',{'transport':transport,'container':final})
            subprocess.run(d+['cp',cid+':/results/.',str(dest)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=180)
            return cid,final[0]['State']['ExitCode']
        except BaseException:
            subprocess.run(d+['rm','-f',cid],stdout=subprocess.DEVNULL,timeout=180); raise
    build=out/'build'
    if not (build/'complete.json').exists():
        build.mkdir(exist_ok=True)
        archive=out/'source.tar'
        with archive.open('wb') as f: subprocess.run(['git','archive',commit],cwd=ROOT,stdout=f,check=True)
        source=out/'source'; source.mkdir(exist_ok=True)
        with tarfile.open(archive) as t: t.extractall(source,filter='data')
        def setup(cid): call(['cp',str(source)+'/.',cid+':/work'])
        cid,code=container(BUILD,build,setup,600)
        try:
            assert code==0, 'worker build failed; see build logs'
            call(['commit',cid,tag]); write(build/'complete.json',{'commit':commit,'image':call(['image','inspect',tag,'--format','{{.Id}}'],text=True).strip()})
        finally: call(['rm','-f',cid])
        shutil.rmtree(source); archive.unlink()
    assert call(['image','inspect',tag,'--format','{{.Id}}'],text=True).strip()==json.loads((build/'complete.json').read_text())['image'], 'build image identity mismatch'
    outcomes=[]
    for record in entries:
        dest=out/'assets'/record['path'].removesuffix('.map'); dest.mkdir(parents=True,exist_ok=True)
        if a.resume and (dest/'outcome.json').exists(): outcomes.append(json.loads((dest/'outcome.json').read_text())); continue
        # Incomplete attempts are retained; never reuse partial replay outputs.
        if (dest/'container.json').exists():
            previous=dest.with_name(dest.name+'.interrupted-'+str(time.time_ns())); dest.rename(previous); dest.mkdir()
        result={'input':record,'outcome':'tooling-failure'}; start=time.monotonic()
        try:
            assert sha(a.collection/record['path'])==record['sha256'], 'input changed after freeze'
            historical=Path.home()/'.local/state/eqemu-map-translation/raycaster-memory-20261110/ordinary-01/results'/(Path(record['path']).stem+'.reference.trace')
            def setup(cid):
                call(['cp',str(a.collection/record['path']),cid+':/input.map'])
                if record['path'] in ('base/qeynos2.map','base/poknowledge.map'): call(['cp',str(historical),cid+':/historical.trace'])
            cid,code=container('mkdir -p /results; python3 /work/experiments/map-translation/raycaster-corpus/guest.py '+record['sha256'],dest,setup,420)
            try:
                if (dest/'report.json').exists(): result.update(json.loads((dest/'report.json').read_text()))
                result['container_exit']=code
                if code: result['outcome']='container-failure'
            finally: call(['rm','-f',cid]); write(dest/'cleanup.json',{'container_removed':cid})
            if result['outcome']=='pass' and not a.retain: result['removed']=cleanup(dest)
            # Failure trace packets remain; cap retained full expanded meshes at 2 GiB.
            mesh=dest/'geometry.mesh'
            retained=sum(x.stat().st_size for x in (out/'assets').rglob('geometry.mesh'))
            if mesh.exists() and retained>2*1024**3:
                result['mesh_regenerate']={'bytes':mesh.stat().st_size,'sha256':sha(mesh),'reason':'2GiB failure mesh cap'}; mesh.unlink()
        except Exception as e: result['tooling_error']=str(e); result['outcome']='tooling-failure'
        result['seconds']=time.monotonic()-start; result['retained_bytes']=sum(x.stat().st_size for x in dest.rglob('*') if x.is_file())
        write(dest/'outcome.json',result); outcomes.append(result)
        write(out/'summary.json',{'identity_sha256':sha(out/'identity.json'),'declared':len(entries),'processed':len(outcomes),'outcomes':dict(collections.Counter(x['outcome'] for x in outcomes)),'assets':outcomes})
        print(record['path'],result['outcome'],round(result['seconds'],2),flush=True)
    assert len(outcomes)==len(entries) and len({x['input']['path'] for x in outcomes})==len(entries)
if __name__=='__main__': main()
