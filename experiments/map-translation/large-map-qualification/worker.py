"""Host transport only. Each stage gets a fresh 2 GiB/no-swap offline rootless container."""
import argparse,subprocess,json,hashlib,tarfile,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--socket',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--stage',choices=['build','maps','regressions'],required=True);p.add_argument('--build',type=Path);a=p.parse_args()
ROOT=Path(__file__).resolve().parents[3];HERE=Path(__file__).resolve().parent
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip()
D=['docker','--host',a.socket];IMAGE='sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d'
assert 'name=rootless' in json.loads(subprocess.check_output(D+['info','--format','{{json .SecurityOptions}}']))
a.output.mkdir(parents=True,exist_ok=False);archive=a.output/'source.tar'
with archive.open('wb') as f:subprocess.run(['git','archive','HEAD'],cwd=ROOT,stdout=f,check=True)
source=a.output/'source';source.mkdir()
with tarfile.open(archive) as t:t.extractall(source,filter='data')
(a.output/'source.json').write_text(json.dumps({'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'image':IMAGE},indent=2))
cases=json.loads((HERE/'cases.json').read_text())['cases'] if a.stage=='maps' else [None]
commands=[]
if a.stage != 'build':
 assert a.build
 origin=json.loads((a.build.parent/'source.json').read_text())
 current=json.loads((a.output/'source.json').read_text())
 assert origin['commit']==current['commit'] and origin['image']==IMAGE, 'build source/image mismatch'
 final=json.loads((a.build/'build.final.json').read_text())[0]['State'];assert final['ExitCode']==0 and not final['OOMKilled']
 for label,path in [('rust','target/release/rust-raycaster-poc'),('cpp','reference')]:
  with (a.build/path).open('rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==json.loads((a.build/(label+'.identity.json')).read_text())['sha256']
for c,phase in [(c,phase) for c in cases for phase in (['controls','reference','rust'] if a.stage=='maps' else [a.stage])]:
 name=Path(c['input']['path']).stem if c else 'build';dest=a.output/name;dest.mkdir(exist_ok=True)
 args=['create','--pull','never','--network','none','--memory','2g','--memory-swap','2g','--cpus','2','--pids-limit','128','--mount',f'type=bind,src={source},dst=/work,readonly','--mount',f'type=bind,src={dest},dst=/results']
 if c:
  assert a.build
  mesh=Path.home()/'.local/state/eqemu-map-translation/raycaster-corpus-20261110/final/assets/base'/name/'geometry.mesh'
  original=Path('/home/bump/Projects/bump-eqemu/bump-akk-stack/server/maps')/c['input']['path']
  (dest/'case.json').write_text(json.dumps(c))
  for src,target in [(mesh,'/geometry.mesh'),(original,'/original.map'),(dest/'case.json','/case.json'),(a.build.resolve(),'/build')]:args+=['--mount',f'type=bind,src={src},dst={target},readonly']
 if a.stage=='regressions':
  for src,target in [(a.build.resolve(),'/build'),(Path.home()/'.local/state/eqemu-map-translation/raycaster-memory-20261110/ordinary-01/results','/baseline'),(Path.home()/'.local/state/eqemu-map-translation/rust-poc-20261110/qualified/results/rust','/rust-meshes'),(Path.home()/'.local/state/eqemu-map-translation/capture-tests-20261110/handoff-ordinary/results/capture','/references')]:args+=['--mount',f'type=bind,src={src},dst={target},readonly']
 args+=['--entrypoint','python3',IMAGE,'/work/experiments/map-translation/large-map-qualification/guest.py',phase]
 commands.append(D+args);(a.output/'commands.json').write_text(json.dumps(commands,indent=2))
 cid=subprocess.check_output(D+args,text=True).strip()
 try:
  (dest/(phase+'.container.json')).write_bytes(subprocess.check_output(D+['inspect',cid]))
  with (dest/(phase+'.worker.log')).open('w') as log:
   try:code=subprocess.run(D+['start','-a',cid],stdout=log,stderr=subprocess.STDOUT,timeout=1200).returncode
   except subprocess.TimeoutExpired:subprocess.run(D+['kill',cid],check=True);raise
  final=json.loads(subprocess.check_output(D+['inspect',cid]));(dest/(phase+'.final.json')).write_text(json.dumps(final,indent=2));assert code==0 and final[0]['State']['ExitCode']==0 and not final[0]['State']['OOMKilled'],dest
 finally:
  subprocess.run(D+['rm','-f',cid],check=True);(dest/(phase+'.cleanup.json')).write_text(json.dumps({'removed':cid}))
