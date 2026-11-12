"""Task-specific host transport; native execution stays in the rootless worker.

Run from a clean repository root: launch_runtime.py OFF|ON UNIQUE_LABEL.
Labels beginning inserver select the complete existing suite.
"""
import subprocess,json,sys,shutil,hashlib
from pathlib import Path
mode,suffix=sys.argv[1:]
assert mode in ('OFF','ON')
assert suffix and all(c.isalnum() or c in '-_' for c in suffix)
r=Path.home()/'.local/state/eqemu-map-translation/rust-map-server-validation-20261111'
out=r/(mode+'-runtime-'+suffix); out.mkdir();(out/'runtime').mkdir();(out/'results').mkdir()
script=out/'runtime.py';shutil.copyfile('tests/map_server_validation/runtime.py',script)
d=['docker','--host','unix:///run/user/1000/eqemu-map-server-validation-20261111.sock']
assert 'name=rootless' in json.loads(subprocess.check_output(d+['info','--format','{{json .SecurityOptions}}']))
cmd=d+['create','--user','0','--network','none','--memory','2g','--memory-swap','2g','--cpus','1','--pids-limit','256']
for src,dst,ro in [(out/'runtime','/runtime',False),(out/'results','/runtime-results',False),(r/'results'/mode/'bin','/binaries',True),(Path.home()/'.local/state/eqemu-vm-proof/runtime-qeynos2-inputs-20261025/payload','/fixture',True),(r/'fixture-manifest-v2.json','/fixture-manifest.json',True),(r.parent/'two-maps-v1/inputs','/originals',True),(script,'/runtime.py',True)]:
 cmd+=['--mount',f'type=bind,src={src},dst={dst}'+(',readonly' if ro else '')]
if suffix.startswith('inserver'): cmd+=['--env','EQEMU_MAP_INSERVER_CHECK=1']
cmd+=['--entrypoint','python3',(r/'runtime-image.txt').read_text().strip(),'/runtime.py']
(out/'command.json').write_text(json.dumps(cmd,indent=2))
(out/'source.json').write_text(json.dumps({'harness_commit':subprocess.check_output(['git','rev-parse','HEAD']).decode().strip(),'harness_sha256':hashlib.sha256(script.read_bytes()).hexdigest(),'binary_source':'239480beac57c78770a209b8fb4f3a2b79fd68b3'},indent=2))
cid=subprocess.check_output(cmd).decode().strip()
try:
 with (out/'worker.log').open('w') as log: p=subprocess.run(d+['start','-a',cid],stdout=log,stderr=subprocess.STDOUT,timeout=1800)
 (out/'exit.json').write_text(json.dumps({'exit':p.returncode}))
finally:
 inspected=subprocess.run(d+['inspect',cid],capture_output=True)
 (out/'container.json').write_bytes(inspected.stdout)
 (out/'inspect.stderr').write_bytes(inspected.stderr)
 subprocess.run(d+['rm','-f',cid],check=True)
 (out/'cleanup.json').write_text(json.dumps({'removed':cid}))

raise SystemExit(p.returncode)
