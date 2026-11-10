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
        start=time.monotonic(); pid=os.fork()
        if pid==0:
            log=os.open(str(out/f'{stem}.{n}.stages.json'),os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
            os.dup2(log,2)
            os.execv(str(binary),[str(binary),str(source),str(target),item['sha256']])
        _,status,r=os.wait4(pid,0)
        assert status==0, status
        identity,v,i=read(target); assert (len(v)//3,len(i))==counts and identity.decode()==item['sha256']
        results.append({'map':stem,'run':n,'elapsed_seconds':time.monotonic()-start,'peak_rss_kib':r.ru_maxrss,
                        'user_seconds':r.ru_utime,'system_seconds':r.ru_stime,'bytes':target.stat().st_size,
                        'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
    compare(out/f'{stem}.0.mesh',out/f'{stem}.1.mesh')
    # Strict comparator first; failure retained, not turned into acceptance.
    strict=subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'mesh.py'),str(refs/f'{stem}.0.mesh'),str(out/f'{stem}.0.mesh')],capture_output=True,text=True)
    (out/f'{stem}.strict.json').write_text(json.dumps({'exit':strict.returncode,'stdout':strict.stdout,'stderr':strict.stderr},indent=2))
    report=diagnose(refs/f'{stem}.0.mesh',out/f'{stem}.0.mesh')
    (out/f'{stem}.numeric.json').write_text(json.dumps(report,indent=2,allow_nan=False))
(out/'results.json').write_text(json.dumps(results,indent=2))
