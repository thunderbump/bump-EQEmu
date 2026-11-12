"""Host transport only. Run from clean committed checkout; all native work is guest-only.

Uses task-owned daemon, output, and fresh copies of the retained qualified inputs.
Run: python3 tests/map_reload/validate.py build|OFF|ON
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess as sp
import sys

root = Path.home()/'.local/state/eqemu-map-translation/zone-map-reload-20261111'
prior = root.parent/'rust-map-server-validation-20261111'
docker = ['docker','--host','unix:///run/user/1000/eqemu-zone-map-reload-20261111.sock']
assert 'name=rootless' in json.loads(sp.check_output(docker+['info','--format','{{json .SecurityOptions}}']))
assert not sp.check_output(['git','status','--porcelain'])
head = sp.check_output(['git','rev-parse','HEAD']).decode().strip()
phase = sys.argv[1]
assert phase in ('build','OFF','ON')
mounts = []
if phase == 'build':
    for name in ('source','results','cargo','deps'):
        shutil.copytree(prior/name,root/name,copy_function=shutil.copy2)
    # Preserve source mtimes for identical inputs so Ninja can reuse the actual
    # qualified objects. Every changed/added tracked file is overlaid explicitly.
    delta = []
    for name in sp.check_output(['git','ls-files','-z']).decode().split('\0'):
        if not name or not Path(name).is_file(): continue
        dest=root/'source'/name
        content=Path(name).read_bytes()
        if not dest.exists() or dest.read_bytes()!=content:
            delta.append(name);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(content)
    (root/'source-delta.json').write_text(json.dumps(delta,indent=2))
    (root/'production-diff.patch').write_bytes(sp.check_output(['git','diff','239480beac57c78770a209b8fb4f3a2b79fd68b3','HEAD','--','zone','common','cmake','CMakeLists.txt','rust']))
    mounts=[(root/'source','/work',True),(root/'results','/results',False),
            (root/'deps','/deps',True),(root/'cargo','/cargo',False)]
    image='sha256:dc2003a3fb395b63706f6dd975cb29e94c18fccf04784b4c47dab00d913f1894'
    script='/work/tests/map_reload/worker.py'; cap='4g'; out=root/'build'
else:
    out=root/(phase+'-runtime');out.mkdir();(out/'runtime').mkdir();(out/'results').mkdir()
    mounts=[(out/'runtime','/runtime',False),(out/'results','/runtime-results',False),
            (root/'results'/phase/'bin','/binaries',True),
            (Path.home()/'.local/state/eqemu-vm-proof/runtime-qeynos2-inputs-20261025/payload','/fixture',True),
            (prior/'fixture-manifest-v2.json','/fixture-manifest.json',True),
            (root.parent/'two-maps-v1/inputs','/originals',True),
            (root/'source/tests/map_server_validation/runtime.py','/runtime.py',True)]
    image='sha256:93429edbf01c1631611af6c88220405136072afd78dc65b6ebf47415b54c38e1'
    script='/runtime.py';cap='2g'
out.mkdir(exist_ok=True)
cmd=docker+['create','--user','0','--network','none','--memory',cap,'--memory-swap',cap,'--cpus','1','--pids-limit','256']
for src,dst,ro in mounts: cmd+=['--mount',f'type=bind,src={src},dst={dst}'+(',readonly' if ro else '')]
cmd+=['--entrypoint','python3',image,script]
(out/'input.json').write_text(json.dumps({'head':head,'command':cmd,'scope':'incremental full default targets' if phase=='build' else 'fresh disposable public fixture'},indent=2))
cid=sp.check_output(cmd).decode().strip()
try:
    with (out/'worker.log').open('w') as log:
        p=sp.run(docker+['start','-a',cid],stdout=log,stderr=sp.STDOUT,timeout=30000)
    (out/'exit.json').write_text(json.dumps({'exit':p.returncode}))
finally:
    (out/'container.json').write_bytes(sp.check_output(docker+['inspect',cid]))
    sp.run(docker+['rm','-f',cid],check=True)
    (out/'cleanup.json').write_text(json.dumps({'container_removed':cid}))
raise SystemExit(p.returncode)
