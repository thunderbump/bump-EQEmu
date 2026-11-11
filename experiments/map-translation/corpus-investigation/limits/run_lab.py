"""Host-only transport for serial ordinary lab C++ execution, never Rust."""
import argparse
import hashlib
import json
import subprocess
import tarfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
IMAGE = 'sha256:0aeb54f14590bf1631a27f60b3e7b7eed6dde549f08ba1f5738282e59d4cea1d'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--socket', required=True)
p.add_argument('--inspection', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
assert not subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT).strip()
inspection = json.loads((a.inspection / 'inspector/structure.json').read_text())
fixture_state = json.loads((a.inspection / 'fixtures/final.json').read_text())['container'][0]['State']
assert fixture_state['ExitCode'] == 0 and not fixture_state['OOMKilled']
cases = json.loads((HERE / 'cases.json').read_text())['cases']
assert {r['mesh_sha256'] for r in inspection['cases']} == {c['geometry']['sha256'] for c in cases}
assert all(r['nonfinite_components'] == r['components_outside_core_range'] == 0 for r in inspection['cases'])
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
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
archive = a.output / 'source.tar'
with archive.open('wb') as f:
    subprocess.run(['git', 'archive', commit], cwd=ROOT, stdout=f, check=True)
source = a.output / 'source'
source.mkdir()
with tarfile.open(archive) as t:
    t.extractall(source, filter='data')
save(a.output / 'source.json', {'commit': commit, 'image': IMAGE, 'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                              'tools': {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in HERE.glob('*.py')}})
for case in [None, *cases]:
    name = 'build' if case is None else Path(case['input']['path']).stem
    dest = a.output / name
    dest.mkdir()
    args = ['create', '--pull', 'never', '--network', 'none', '--memory', '2g', '--memory-swap', '2g',
            '--cpus', '2', '--pids-limit', '128', '--mount', f'type=bind,src={dest},dst=/results',
            '--mount', f'type=bind,src={source},dst=/work,readonly']
    if case:
        src = Path.home() / '.local/state/eqemu-map-translation/raycaster-corpus-20261110/final/assets/base' / name / 'geometry.mesh'
        save(dest / 'case.json', case)
        args += ['--mount', f'type=bind,src={src},dst=/geometry.mesh,readonly',
                 '--mount', f'type=bind,src={dest}/case.json,dst=/case.json,readonly',
                 '--mount', f'type=bind,src={a.output}/build/lab-reference,dst=/lab-reference,readonly']
    cid = call(args + ['--entrypoint', 'python3', IMAGE,
                      '/work/experiments/map-translation/corpus-investigation/limits/lab_guest.py',
                      'query' if case else 'build'], text=True).strip()
    try:
        save(dest / 'container.json', json.loads(call(['inspect', cid])))
        start = time.monotonic()
        with (dest / 'worker.log').open('w') as log:
            try:
                code = subprocess.run(docker + ['start', '-a', cid], stdout=log, stderr=subprocess.STDOUT,
                                      timeout=420 if case is None else 150).returncode
            except subprocess.TimeoutExpired:
                call(['kill', cid])
                raise
        final = json.loads(call(['inspect', cid]))
        save(dest / 'final.json', {'transport_exit': code, 'seconds': time.monotonic()-start, 'container': final})
        assert code == 0 and final[0]['State']['ExitCode'] == 0 and not final[0]['State']['OOMKilled']
    finally:
        call(['rm', '-f', cid])
        save(dest / 'cleanup.json', {'removed_container': cid})
