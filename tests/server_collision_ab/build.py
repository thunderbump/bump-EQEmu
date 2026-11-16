"""Matched incremental full-target builds; host transport, native work guest-only."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess as sp

ROOT = Path.home()/'.local/state/eqemu-map-translation/server-collision-npc-completion-20261115'
PRIOR = ROOT.parent/'zone-map-reload-20261111'
DOCKER = ['docker','--host','unix:///run/user/1000/eqemu-server-collision-npc-completion-20261115.sock']
IMAGE = 'sha256:dc2003a3fb395b63706f6dd975cb29e94c18fccf04784b4c47dab00d913f1894'
assert not sp.check_output(['git','status','--porcelain']), 'commit source first'
assert 'name=rootless' in json.loads(sp.check_output(DOCKER+['info','--format','{{json .SecurityOptions}}']))
assert json.loads(sp.check_output(DOCKER+['image','inspect',IMAGE]))[0]['Id'] == IMAGE
out = ROOT/'build'
out.mkdir()
for name in ('source','results','cargo','deps'):
    sp.run(['cp','-a','--reflink=auto',str(PRIOR/name),str(ROOT/name)],check=True)
delta=[]
for name in sp.check_output(['git','ls-files','-z']).decode().split('\0'):
    if not name or not Path(name).is_file(): continue
    dst=ROOT/'source'/name
    if not dst.exists() or dst.read_bytes()!=Path(name).read_bytes():
        dst.parent.mkdir(parents=True,exist_ok=True)
        dst.write_bytes(Path(name).read_bytes())
        delta.append(name)
head=sp.check_output(['git','rev-parse','HEAD']).decode().strip()
(out/'source-delta.json').write_text(json.dumps(delta,indent=2))
(out/'production.patch').write_bytes(sp.check_output(['git','diff','e876fcad518567ceb7fe4164badaeae096700c53',head,'--','zone','common','cmake','CMakeLists.txt','rust']))
cmd=DOCKER+['create','--network','none','--user','0','--memory','4g','--memory-swap','4g','--cpus','1','--pids-limit','256']
for src,dst,ro in [('source','/work',True),('results','/results',False),('cargo','/cargo',False),('deps','/deps',True)]:
    cmd+=['--mount',f'type=bind,src={ROOT/src},dst={dst}'+(',readonly' if ro else '')]
cmd+=['--entrypoint','python3',IMAGE,'/work/tests/map_reload/worker.py']
(out/'inputs.json').write_text(json.dumps({'source':head,'command':cmd,'image':IMAGE},indent=2))
cid=sp.check_output(cmd).decode().strip()
try:
    with (out/'worker.log').open('wb') as log:
        result=sp.run(DOCKER+['start','-a',cid],stdout=log,stderr=sp.STDOUT,timeout=30000)
    (out/'exit.json').write_text(json.dumps({'exit':result.returncode}))
finally:
    (out/'container.json').write_bytes(sp.check_output(DOCKER+['inspect',cid]))
    sp.run(DOCKER+['rm','-f',cid],check=True)
    (out/'cleanup.json').write_text(json.dumps({'removed':cid}))
raise SystemExit(result.returncode)
