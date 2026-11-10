"""Worker-only two-map qualification, immutable external C++ references."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from mesh import read, compare
from diagnose import diagnose
binary,inputs,refs,out=map(Path,sys.argv[1:]); out.mkdir(exist_ok=False)
manifest=json.loads(Path(__file__).resolve().parents[1].joinpath('corpus.json').read_text())
results=[]
for item,counts in zip(manifest['maps'],[(32276,72855),(735199,975000)]):
    source=inputs/item['path']; stem=source.stem
    assert len(source.read_bytes())==item['bytes'] and hashlib.sha256(source.read_bytes()).hexdigest()==item['sha256']
    for n in range(2):
        target=out/f'{stem}.{n}.mesh'
        usage=out/f'{stem}.{n}.time'
        start=time.monotonic()
        subprocess.run(['/usr/bin/time','-o',str(usage),'-f','{"peak_rss_kib":%M,"user_seconds":%U,"system_seconds":%S}',str(binary),str(source),str(target),item['sha256']],stderr=(out/f'{stem}.{n}.stages.json').open('w'),check=True)
        elapsed=time.monotonic()-start
        resources=json.loads(usage.read_text())
        identity,v,i=read(target); assert (len(v)//3,len(i))==counts and identity.decode()==item['sha256']
        results.append({'map':stem,'run':n,'elapsed_seconds':elapsed,**resources,'bytes':target.stat().st_size,
                        'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    compare(out/f'{stem}.0.mesh',out/f'{stem}.1.mesh')
    for n in range(2):
        target=out/f'{stem}.cpp.{n}.mesh'; usage=out/f'{stem}.cpp.{n}.time'
        start=time.monotonic()
        subprocess.run(['/usr/bin/time','-o',str(usage),'-f','{"peak_rss_kib":%M,"user_seconds":%U,"system_seconds":%S}','/build/map-capture',str(source),str(target),item['sha256']],check=True)
        elapsed=time.monotonic()-start
        compare(refs/f'{stem}.0.mesh',target)
        results.append({'map':stem,'implementation':'cpp','run':n,'elapsed_seconds':elapsed,**json.loads(usage.read_text())})
        target.unlink() # Disposable fresh timing outputs, never the supplied references.
    # Strict comparator first; failure retained, not turned into acceptance.
    strict=subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'mesh.py'),str(refs/f'{stem}.0.mesh'),str(out/f'{stem}.0.mesh')],capture_output=True,text=True)
    (out/f'{stem}.strict.json').write_text(json.dumps({'exit':strict.returncode,'stdout':strict.stdout,'stderr':strict.stderr},indent=2))
    report=diagnose(refs/f'{stem}.0.mesh',out/f'{stem}.0.mesh')
    (out/f'{stem}.numeric.json').write_text(json.dumps(report,indent=2,allow_nan=False))
(out/'results.json').write_text(json.dumps(results,indent=2))
