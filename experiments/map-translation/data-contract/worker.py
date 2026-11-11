"""Host transport only; exclusive outputs, immutable source and offline rootless guest."""
import argparse, hashlib, json, subprocess, tarfile
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('--socket',required=True); p.add_argument('--output',type=Path,required=True); a=p.parse_args()
ROOT=Path(__file__).resolve().parents[3]
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip()
D=['docker','--host',a.socket]
assert 'name=rootless' in json.loads(subprocess.check_output(D+['info','--format','{{json .SecurityOptions}}']))
IMAGE='sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d'
a.output.mkdir(parents=True,exist_ok=False)
archive=a.output/'source.tar'
with archive.open('wb') as f: subprocess.run(['git','archive','HEAD'],cwd=ROOT,stdout=f,check=True)
source=a.output/'source'; source.mkdir()
with tarfile.open(archive) as t: t.extractall(source,filter='data')
def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()
cases=json.loads((source/'experiments/map-translation/data-contract/cases.json').read_text())
for c in cases:
    for f in c['files'].values():
        path=Path(f['path']); assert path.stat().st_size==f['bytes'] and sha(path)==f['sha256'],path
    for name,digest in c['export_provenance'].items():
        assert sha(Path(c['files']['mesh']['path']).parent/name)==digest
(a.output/'source.json').write_text(json.dumps(dict(commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),archive_sha256=sha(archive),image=IMAGE,verified_cases=cases),indent=2))
results=a.output/'results'; results.mkdir()
args=['create','--pull','never','--network','none','--memory','2g','--memory-swap','2g','--cpus','2','--pids-limit','128','--mount',f'type=bind,src={source},dst=/work,readonly','--mount',f'type=bind,src={results},dst=/results']
for c in cases:
    args+=['--mount',f'type=bind,src={Path(c["files"]["mesh"]["path"]).parent},dst=/assets/{c["name"]},readonly']
args+=['--entrypoint','python3',IMAGE,'/work/experiments/map-translation/data-contract/guest.py']
(a.output/'command.json').write_text(json.dumps(D+args,indent=2))
cid=subprocess.check_output(D+args,text=True).strip()
try:
    (a.output/'initial.json').write_bytes(subprocess.check_output(D+['inspect',cid]))
    with (a.output/'worker.log').open('w') as log:
        try: code=subprocess.run(D+['start','-a',cid],stdout=log,stderr=subprocess.STDOUT,timeout=1200).returncode
        except subprocess.TimeoutExpired:
            subprocess.run(D+['kill',cid],check=True); raise
    final=json.loads(subprocess.check_output(D+['inspect',cid])); (a.output/'final.json').write_text(json.dumps(final,indent=2))
    assert code==0 and final[0]['State']['ExitCode']==0 and not final[0]['State']['OOMKilled']
finally:
    subprocess.run(D+['rm','-f',cid],check=True)
    (a.output/'cleanup.json').write_text(json.dumps(dict(container_removed=cid)))
