"""Run only inside a disposable worker; outputs always go to a fresh directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time
import shlex
from mesh import compare, read

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('binary', type=Path)
p.add_argument('inputs', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
a.binary = a.binary.resolve()
a.output.mkdir(parents=True, exist_ok=False)
root = Path(__file__).resolve().parents[2]

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def command(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)

provenance = {
    'runtime_fp_and_versions': json.loads(command(str(a.binary), '--runtime-info')),
    'schema': 1, 'architecture': platform.machine(), 'platform': platform.platform(),
    'source_identity': json.loads((root / 'capture-source.json').read_text()),
    'binary_sha256': sha(a.binary),
    'compiler': command(shlex.split(json.loads((a.binary.parent / 'compile_commands.json').read_text())[0]['command'])[0], '--version'),
    'dependencies': command('ldd', str(a.binary)),
    'compile_commands': json.loads((a.binary.parent / 'compile_commands.json').read_text()),
    'environment': {k: os.environ.get(k) for k in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'LD_LIBRARY_PATH')},
    'fp': 'default process round-to-nearest; -fno-fast-math -ffp-contract=off; no FP environment changes',
}
# Hash resolved shared objects, GLM headers and build configuration separately from geometry.
provenance['dependency_sha256'] = {}
for line in provenance['dependencies'].splitlines():
    for token in line.split():
        if token.startswith('/') and Path(token).is_file():
            provenance['dependency_sha256'][token] = sha(token)
glm = Path(os.environ.get('GLM_ROOT', '/usr/include/glm'))
provenance['glm_headers'] = {str(f.relative_to(glm)): sha(f) for f in sorted(f for f in glm.rglob('*') if f.is_file())}
if not provenance['glm_headers']:
    raise RuntimeError('set GLM_ROOT to actual compiler GLM directory')
(a.output / 'provenance.json').write_text(json.dumps(provenance, indent=2))
manifest = json.loads(Path(__file__).with_name('corpus.json').read_text())
results = []
for item, predicted in zip(manifest['maps'], [(32276, 72855), (735199, 975000)]):
    source = a.inputs / item['path']
    if sha(source) != item['sha256'] or source.stat().st_size != item['bytes']:
        raise RuntimeError(f'input identity mismatch: {source}')
    captures = []
    for n in range(2):
        target = a.output / f'{source.stem}.{n}.mesh'
        usage = a.output / f'{source.stem}.{n}.time'
        start = time.monotonic()
        pid = os.fork()
        if pid == 0:
            os.execv(str(a.binary), [str(a.binary), str(source), str(target), item['sha256']])
        _, status, resources = os.wait4(pid, 0)
        usage.write_text(json.dumps({'wait_status': status, 'peak_rss_kib': resources.ru_maxrss,
                                     'user_seconds': resources.ru_utime, 'system_seconds': resources.ru_stime}))
        if status != 0:
            raise RuntimeError(f'capture failed: wait status {status}')
        captures.append(target)
        results.append({'map': item['path'], 'run': n, 'elapsed_seconds': time.monotonic() - start,
                        'bytes': target.stat().st_size, 'sha256': sha(target)})
    payload = compare(*captures)
    identity, vertices, indices = read(captures[0])
    assert (len(vertices) // 3, len(indices)) == predicted
    results[-1]['geometry_sha256'] = payload
(a.output / 'results.json').write_text(json.dumps(results, indent=2))
print(json.dumps(results, indent=2))
