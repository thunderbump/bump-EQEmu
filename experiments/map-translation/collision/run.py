"""Guest-only bounded collision checks and provenance collection."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time

ROOT = Path('/work')
HERE = ROOT / 'experiments/map-translation/collision'
OUT = Path('/results')
BINARY = '/build/collision-eval'
IDENTITIES = {'qeynos2': '040343c54e3b02bfd1637ce8fca8839dd676e17c41fb24c042ccfa0bd649435c',
              'poknowledge': '794b618d86852b47bd593424129aaf34eee326e5e3fec7290aeeb0e32bc41d20'}

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def run(name, args, expected=0, diagnostic=None):
    start = time.monotonic()
    p = subprocess.run(['/usr/bin/time', '-v', '-o', str(OUT / (name+'.time'))] + args, capture_output=True, text=True)
    (OUT / (name+'.stdout')).write_text(p.stdout)
    (OUT / (name+'.stderr')).write_text(p.stderr)
    record = {'name': name, 'command': args, 'exit': p.returncode, 'seconds': time.monotonic()-start}
    results.append(record)
    (OUT/'runs.json').write_text(json.dumps(results, indent=2))
    if p.returncode != expected or (diagnostic and diagnostic not in p.stderr):
        raise RuntimeError(f'{name}: unexpected result {p.returncode}, expected {expected}: {p.stderr}')
    return p.stdout

results = []
run('analytic', [BINARY, '--controls'])
manifest = []
for name, identity in IDENTITIES.items():
    original = Path('/inputs')/(name+'.map')
    assert sha(original) == identity
    a=Path('/references')/(name+'.0.mesh')
    b=Path('/rust-meshes')/(name+'.0.mesh')
    for p in (original,a,b):
        manifest.append({'path':str(p),'bytes':p.stat().st_size,'sha256':sha(p),
                         'payload_sha256':hashlib.sha256(p.read_bytes()[84:]).hexdigest() if p.suffix=='.mesh' else None})
    report=json.loads(run(name,[BINARY,str(a),str(b),identity]))
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
