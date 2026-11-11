"""Host-only authenticated external-baseline plus harness-overlay transport."""
import hashlib,json,subprocess,sys,tarfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
STATE=Path.home()/'.local/state/eqemu-map-translation/additional-pipeline-20261111'
BASE='60346d90b79b9c2b1a86590125b955219ecd1a41'
IMAGE='sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d'
SOCKET='unix:///run/user/1000/eqemu-additional-pipeline-20261111.sock'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,x):p.write_text(json.dumps(x,indent=2)+'\n')
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()
def main():
    assert not git('status','--porcelain'), 'clean committed harness required'
    head=git('rev-parse','HEAD'); out=STATE/'execution';out.mkdir(exist_ok=False)
    source=out/'source';source.mkdir()
    identities=[]
    for name,commit,paths in [('module',BASE,[]),('harness',head,['experiments/map-translation/additional-pipeline'])]:
        archive=out/(name+'.tar')
        with archive.open('wb') as f:subprocess.run(['git','archive',commit,*paths],cwd=ROOT,stdout=f,check=True)
        with archive.open('rb') as f:assert subprocess.check_output(['git','get-tar-commit-id'],stdin=f,text=True).strip()==commit
        identities.append({'name':name,'commit':commit,'tree':git('rev-parse',commit+'^{tree}'),'subtree':git('rev-parse',commit+':experiments/map-translation/additional-pipeline') if paths else None,'archive_sha256':sha(archive),'archive_bytes':archive.stat().st_size})
        with tarfile.open(archive) as t:t.extractall(source,filter='data')
    save(out/'source.json',identities);save(out/'command.json',sys.argv)
    d=['docker','--host',SOCKET]
    def call(args):return subprocess.check_output(d+args,text=True,timeout=180).strip()
    assert 'name=rootless' in json.loads(call(['info','--format','{{json .SecurityOptions}}']))
    def container(script,dest,image,setup,limit):
        dest.mkdir(); cid=call(['create','--pull','never','--network','none','--memory','2g','--memory-swap','2g','--cpus','2','--pids-limit','128','--entrypoint','/bin/bash',image,'-lc',script])
        save(dest/'container.json',json.loads(call(['inspect',cid])))
        try:
            setup(cid)
            with (dest/'worker.log').open('w') as f:
                try:code=subprocess.run(d+['start','-a',cid],stdout=f,stderr=subprocess.STDOUT,timeout=limit).returncode
                except subprocess.TimeoutExpired:call(['kill',cid]);code='timeout'
            save(dest/'exit.json',{'transport':code,'inspect':json.loads(call(['inspect',cid]))})
            call(['cp',cid+':/results/.',str(dest)])
            return cid,code
        except BaseException:call(['rm','-f',cid]);raise
    build='''set -eu
mkdir /results
cmake -S /work/experiments/map-translation -B /capture -DGLM_INCLUDE_DIR=/candidate/build/vcpkg_installed/x64-linux/include -DCMAKE_BUILD_TYPE=Release
cmake --build /capture -j2
cmake -S /work/experiments/map-translation/collision -B /collision -DCMAKE_BUILD_TYPE=Release
cmake --build /collision -j2
cd /work/experiments/map-translation/rust
CARGO_TARGET_DIR=/decoder-build cargo build --release --frozen --offline
CARGO_TARGET_DIR=/decoder-build cargo test --release --frozen --offline > /results/decoder-tests.txt
cd ../rust-raycaster
CARGO_TARGET_DIR=/rust-build cargo build --release --frozen --offline
CARGO_TARGET_DIR=/rust-build cargo test --release --frozen --offline > /results/raycaster-tests.txt
python3 /work/experiments/map-translation/raycaster-corpus/controls.py
MAP_CAPTURE_BINARY=/capture/map-capture python3 ../test_capture.py > /results/capture-tests.txt 2>&1
MAP_CAPTURE_BINARY=/decoder-build/release/map-poc python3 ../rust/test_poc.py > /results/decoder-fixtures.txt 2>&1
rustc -Vv > /results/rustc.txt
c++ --version > /results/compiler.txt
sha256sum /capture/map-capture /collision/collision-eval /decoder-build/release/map-poc /rust-build/release/rust-raycaster-poc > /results/binaries.sha256
cat /sys/fs/cgroup/memory.{peak,events,max,swap.max} > /results/build-memory.txt
'''
    cid,code=container(build,out/'build',IMAGE,lambda c:call(['cp',str(source)+'/.',c+':/work']),600)
    tag='additional-pipeline-build:'+head[:12]
    try:
        assert code==0,'build failed; retained logs'
        call(['commit',cid,tag]);save(out/'build-image.json',json.loads(call(['image','inspect',tag])))
    finally:call(['rm','-f',cid]);save(out/'build'/'cleanup.json',{'removed':cid})
    outcomes=[]
    for r in json.loads((HERE/'manifest.json').read_text())['selected']:
        inp=STATE/'inputs'/Path(r['path']).name;assert sha(inp)==r['sha256']
        dest=out/Path(r['path']).stem
        cid,code=container('mkdir /results; python3 /work/experiments/map-translation/additional-pipeline/guest.py '+r['sha256'],dest,tag,lambda c:call(['cp',str(inp),c+':/input.map']),600)
        try:
            report=json.loads((dest/'report.json').read_text());report['container_transport']=code;report['input']=r;outcomes.append(report)
        finally:call(['rm','-f',cid]);save(dest/'cleanup.json',{'removed':cid})
        save(out/'summary.json',outcomes)
        print(r['path'],report['outcome'],flush=True)
if __name__=='__main__':main()
