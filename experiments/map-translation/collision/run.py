"""Guest-only bounded collision checks and provenance collection."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import threading
from compare import compare, controls

ROOT = Path('/work')
HERE = ROOT / 'experiments/map-translation/collision'
OUT = Path('/results')
BINARY = '/build/collision-eval'
IDENTITIES = {'qeynos2': '040343c54e3b02bfd1637ce8fca8839dd676e17c41fb24c042ccfa0bd649435c',
              'poknowledge': '794b618d86852b47bd593424129aaf34eee326e5e3fec7290aeeb0e32bc41d20'}

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def run(name, args, expected=0, diagnostic=None, collect=False):
    def memory():
        return {n: (Path('/sys/fs/cgroup')/n).read_text().strip() for n in
                ['memory.current','memory.peak','memory.events','memory.max','memory.swap.max']}
    before = memory()
    start = time.monotonic()
    samples = []
    done = threading.Event()
    def sample():
        while not done.wait(.05):
            samples.append({'seconds':time.monotonic()-start, **memory()})
    monitor = threading.Thread(target=sample)
    monitor.start()
    p = subprocess.run(['/usr/bin/time', '-v', '-o', str(OUT / (name+'.time'))] + args, capture_output=True, text=True)
    done.set(); monitor.join()
    (OUT / (name+'.memory.json')).write_text(json.dumps(samples))
    (OUT / (name+'.stdout')).write_text(p.stdout)
    (OUT / (name+'.stderr')).write_text(p.stderr)
    record = {'name': name, 'command': args, 'exit': p.returncode, 'seconds': time.monotonic()-start, 'expected_exit': expected, 'cgroup_before':before, 'cgroup_after':memory()}
    results.append(record)
    (OUT/'runs.json').write_text(json.dumps(results, indent=2))
    if p.returncode != expected or (diagnostic and diagnostic not in p.stderr):
        if collect:
            failures.append(name)
        else:
            raise RuntimeError(f'{name}: unexpected result {p.returncode}, expected {expected}: {p.stderr}')
    return p.stdout

results = []
failures = []
if os.environ.get('DIAGNOSTICS') == 'ON':
    run('sanitizer-pass', ['/build/collision-sanitizer-controls', 'pass'])
    for mode, message in [('use-after-free','heap-use-after-free'), ('out-of-bounds','heap-buffer-overflow'), ('undefined','signed integer overflow'), ('leak','detected memory leaks')]:
        run('sanitizer-'+mode, ['/build/collision-sanitizer-controls', mode], 1, message)
        text=(OUT/('sanitizer-'+mode+'.stderr')).read_text()
        assert 'sanitizer_controls.cpp:' in text, 'missing symbolized control file/line'
run('analytic', [BINARY, '--controls'], collect=True)
controls(OUT)
manifest = []
for name, identity in IDENTITIES.items():
    original = Path('/inputs')/(name+'.map')
    assert sha(original) == identity
    a=Path('/references')/(name+'.0.mesh')
    b=Path('/rust-meshes')/(name+'.0.mesh')
    for p in (original,a,b):
        manifest.append({'path':str(p),'bytes':p.stat().st_size,'sha256':sha(p),
                         'payload_sha256':hashlib.sha256(p.read_bytes()[84:]).hexdigest() if p.suffix=='.mesh' else None})
    manifest.append({'path':'/baseline/'+name+'.trace','sha256':sha(Path('/baseline')/(name+'.trace'))})
    reference=OUT/(name+'.reference.trace')
    candidate=OUT/(name+'.candidate.trace')
    run(name+'-reference',[BINARY,'--record',str(a),identity,'unused',str(reference)],collect=True)
    run(name+'-candidate',[BINARY,'--replay',str(b),identity,str(reference),str(candidate)],collect=True)
    try:
        report=compare(reference,candidate)
        baseline=compare(Path('/baseline')/(name+'.trace'),reference,legacy=True)
        assert baseline == report
    except (AssertionError, OSError, ValueError, IndexError) as error:
        failures.append(name+'-comparison')
        report={'failure':str(error)}
    (OUT/(name+'.json')).write_text(json.dumps(report,indent=2))
    # Exercise the native reader and structural comparator, not just a Python parser.
    raw=a.read_bytes()
    nv,ni=struct.unpack_from('<II',raw,12)
    changes = {'schema':(8,struct.pack('<I',2),'schema'),
               'identity':(20,b'f'*64,'identity mismatch'),
               'index':(84+nv*12,struct.pack('<I',nv),'index bounds'),
               'count':(12,struct.pack('<I',0),'counts'),
               'nonfinite':(84,struct.pack('<I',0x7fc00000),'nonfinite vertex')}
    for label,(offset,value,message) in changes.items():
        mutated=raw[:offset]+value+raw[offset+len(value):]
        path=OUT/'control.mesh'; path.write_bytes(mutated)
        run(name+'-reject-'+label,[BINARY,str(a),str(path),identity],2,message)
        path.unlink()
    path=OUT/'control.mesh'; path.write_bytes(raw[:-1])
    run(name+'-reject-truncated',[BINARY,str(a),str(path),identity],2,'length'); path.unlink()
    path=OUT/'control.mesh'; changed=bytearray(raw)
    i0,i1=struct.unpack_from('<II',changed,84+nv*12)
    struct.pack_into('<II',changed,84+nv*12,i1,i0)
    path.write_bytes(changed)
    run(name+'-reject-order',[BINARY,str(a),str(path),identity],2,'strict structure/index/order'); path.unlink()
(OUT/'artifacts.json').write_text(json.dumps(manifest,indent=2))

(OUT/'summary.json').write_text(json.dumps({'passed':not failures, 'failed_processes':failures},indent=2))
if failures:
    raise SystemExit('failed processes: '+', '.join(failures))
