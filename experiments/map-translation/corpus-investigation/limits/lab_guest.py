"""Guest-only serial lab query stages. Never invokes Rust or promotes output."""
import collections
import hashlib
import json
import math
import os
import signal
import struct
import subprocess
import sys
import time
from pathlib import Path

O = Path('/results')
CG = Path('/sys/fs/cgroup')
def memory():
    return {n: (CG / n).read_text().strip() for n in ('memory.max', 'memory.swap.max', 'memory.peak', 'memory.events')}
report = {'scope': 'lab C++ only; unchanged production engine, copied envelope adapter; no Rust qualification', 'admission': memory(), 'stages': []}
assert report['admission']['memory.max'] == '2147483648' and report['admission']['memory.swap.max'] == '0'
def save():
    (O / 'lab.json').write_text(json.dumps(report, indent=2))
def run(name, args):
    start = time.monotonic()
    before = memory()
    with (O / (name + '.stdout')).open('w') as out, (O / (name + '.stderr')).open('w') as err:
        proc = subprocess.Popen(['/usr/bin/time', '-v', '-o', str(O / (name + '.time')), *args], stdout=out, stderr=err, start_new_session=True)
        try:
            code = proc.wait(timeout=120)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
            code = 'timeout'
    report['stages'].append(dict(name=name, exit=code, seconds=time.monotonic()-start, before=before, after=memory()))
    save()
    assert code == 0, name

if sys.argv[1] == 'build':
    run('generate', ['python3', '/work/experiments/map-translation/corpus-investigation/limits/prepare_lab.py'])
    run('compile', ['c++', '-O3', '-DNDEBUG', '-std=c++17', '-I/work/experiments/map-translation/collision/shim', '-I/work', '-I/work/zone', '/results/lab_driver.cpp', '/work/common/memory/ksm.cpp', '-o', '/results/lab-reference'])
    run('controls', ['/results/lab-reference', '--controls'])
    report['binary_sha256'] = hashlib.sha256((O / 'lab-reference').read_bytes()).hexdigest()
    report['compiler'] = subprocess.check_output(['c++', '--version'], text=True)
else:
    case = json.loads(Path('/case.json').read_text())
    with Path('/geometry.mesh').open('rb') as f:
        before_hash = hashlib.file_digest(f, 'sha256').hexdigest()
    assert before_hash == case['geometry']['sha256']
    run('reference', ['/lab-reference', '--record', '/geometry.mesh', case['input']['sha256'], 'unused', '/results/reference.trace'])
    counts = collections.Counter()
    hits = 0
    endpoint_max_abs = 0.
    for line in (O / 'reference.trace').read_text().splitlines():
        fields = line.split()
        assert len(fields) == (15 if fields[7] == '1' else 8)
        coords = [float.fromhex(x) for x in fields[1:7]]
        assert all(math.isfinite(x) and abs(x) <= 1e8 for x in coords)
        endpoint_max_abs = max(endpoint_max_abs, *(abs(x) for x in coords))
        hits += fields[7] == '1'
        counts[fields[0]] += 1
    queries = sum(counts.values())
    assert hits > 0 and queries > hits and queries <= 5504
    for line in (O / 'reference.stderr').read_text().splitlines():
        if line.startswith('LAB_STATS '):
            report['tree'] = json.loads(line[len('LAB_STATS '):])
    assert 'tree' in report
    with Path('/geometry.mesh').open('rb') as f:
        assert hashlib.file_digest(f, 'sha256').hexdigest() == before_hash
    report.update(mesh_sha256=before_hash, input=case['input'], queries=queries, hits=hits,
                  misses=queries-hits, categories=counts, endpoint_max_abs=endpoint_max_abs,
                  trace_sha256=hashlib.sha256((O / 'reference.trace').read_bytes()).hexdigest())
save()
