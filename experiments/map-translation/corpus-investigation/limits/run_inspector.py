"""Host transport only. Execute fixtures before pinned-mesh inspection in workers."""
import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
IMAGE = 'sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--socket', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
assert not subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT).strip()
a.output.mkdir(parents=True, exist_ok=False)
docker = ['docker', '--host', a.socket]
commands = []

def call(args, **kw):
    commands.append(args)
    (a.output / 'commands.json').write_text(json.dumps(commands, indent=2))
    return subprocess.check_output(docker + args, timeout=180, **kw)

def save(path, value):
    path.write_text(json.dumps(value, indent=2))

assert 'name=rootless' in json.loads(call(['info', '--format', '{{json .SecurityOptions}}'], text=True))
source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
save(a.output / 'source.json', {'commit': source, 'image': IMAGE, 'tools': {
    f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in HERE.glob('*.py')}})
for stage in ('fixtures', 'inspector'):
    dest = a.output / stage
    dest.mkdir()
    args = ['create', '--pull', 'never', '--network', 'none', '--memory', '2g',
            '--memory-swap', '2g', '--cpus', '2', '--pids-limit', '128',
            '--mount', f'type=bind,src={dest},dst=/results']
    if stage == 'inspector':
        for case in json.loads((HERE / 'cases.json').read_text())['cases']:
            name = Path(case['input']['path']).stem
            src = Path.home() / '.local/state/eqemu-map-translation/raycaster-corpus-20261110/final/assets/base' / name / 'geometry.mesh'
            args += ['--mount', f'type=bind,src={src},dst=/inputs/{name}.mesh,readonly']
    guest = ('python3 -m unittest discover -s /limits -p test_inspect.py -v' if stage == 'fixtures'
             else 'python3 /limits/inspect_mesh.py /limits/cases.json /inputs /results/structure.json')
    script = '''set -eu
[ "$(cat /sys/fs/cgroup/memory.max)" = 2147483648 ]
[ "$(cat /sys/fs/cgroup/memory.swap.max)" = 0 ]
for n in memory.max memory.swap.max memory.peak memory.events; do cat /sys/fs/cgroup/$n > /results/before.$n; done
set +e
/usr/bin/timeout --signal=KILL 120 /usr/bin/time -v -o /results/time.txt ''' + guest + '''
code=$?
for n in memory.max memory.swap.max memory.peak memory.events; do cat /sys/fs/cgroup/$n > /results/after.$n; done
exit $code
'''
    cid = call(args + ['--entrypoint', '/bin/bash', IMAGE, '-lc', script], text=True).strip()
    try:
        save(dest / 'container.json', json.loads(call(['inspect', cid])))
        call(['cp', str(HERE), cid + ':/limits'])
        start = time.monotonic()
        with (dest / 'worker.log').open('w') as log:
            try:
                code = subprocess.run(docker + ['start', '-a', cid], stdout=log,
                                      stderr=subprocess.STDOUT, timeout=150).returncode
            except subprocess.TimeoutExpired:
                call(['kill', cid])
                raise
        final = json.loads(call(['inspect', cid]))
        save(dest / 'final.json', {'transport_exit': code, 'seconds': time.monotonic() - start,
                                   'container': final})
        assert code == 0 and final[0]['State']['ExitCode'] == 0 and not final[0]['State']['OOMKilled']
    finally:
        call(['rm', '-f', cid])
        save(dest / 'cleanup.json', {'removed_container': cid})
