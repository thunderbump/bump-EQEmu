"""Guest-only immutable replay adapter and independent exact/numeric reporting."""
import hashlib,json,math,struct,subprocess,sys,time
from pathlib import Path
sys.path.insert(0,'/work/experiments/map-translation/collision')
from compare import compare,controls,row
OUT=Path('/results'); BIN='/rust-build/release/rust-raycaster-poc'
IDS={'qeynos2':'040343c54e3b02bfd1637ce8fca8839dd676e17c41fb24c042ccfa0bd649435c','poknowledge':'794b618d86852b47bd593424129aaf34eee326e5e3fec7290aeeb0e32bc41d20'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def mem():return {n:(Path('/sys/fs/cgroup')/n).read_text() for n in ['memory.current','memory.peak','memory.events','memory.max','memory.swap.max']}
runs=[]
def run(name,args,expected=0,message=None):
 before=mem();t=time.monotonic()
 p=subprocess.run(['/usr/bin/time','-v','-o',str(OUT/(name+'.time')),*args],capture_output=True,text=True)
 (OUT/(name+'.stdout')).write_text(p.stdout);(OUT/(name+'.stderr')).write_text(p.stderr)
 runs.append({'name':name,'args':args,'exit':p.returncode,'seconds':time.monotonic()-t,'before':before,'after':mem()});(OUT/'runs.json').write_text(json.dumps(runs,indent=2))
 assert p.returncode==expected,(name,p.stderr)
 assert message is None or message in p.stderr,(name,message,p.stderr)
def ordered(b):
 u=struct.unpack('<I',b)[0];return (0x80000000-u) if u&0x80000000 else (0x80000000+u)
def report(ref,candidate):
 results={'queries':0,'hits':0,'misses':0,'classification_changes':0,'field_bit_changes':0,'max_abs':0.,'max_relative_reference_ge_1e-6':0.,'max_ulp':0,'signed_zero_changes':0,'examples':[]}
 for x,y in zip(ref.read_text().splitlines(),candidate.read_text().splitlines(),strict=True):
  xi,xh=row(x);yi,yh=row(y);assert xi==yi
  results['queries']+=1; results['hits' if yh[0][0] else 'misses']+=1
  if xh[0][0]!=yh[0][0]:results['classification_changes']+=1
  if xh[0][0] and yh[0][0]:
   for k,(a,b) in enumerate(zip(xh[0][1],yh[0][1])):
    if a==b:continue
    results['field_bit_changes']+=1;av=struct.unpack('<f',a)[0];bv=struct.unpack('<f',b)[0];delta=abs(av-bv)
    results['max_abs']=max(results['max_abs'],delta);results['max_ulp']=max(results['max_ulp'],abs(ordered(a)-ordered(b)))
    if abs(av)>=1e-6:results['max_relative_reference_ge_1e-6']=max(results['max_relative_reference_ge_1e-6'],delta/abs(av))
    if av==0 and bv==0:results['signed_zero_changes']+=1
    if len(results['examples'])<10:results['examples'].append({'row':results['queries']-1,'field':k,'reference':av,'candidate':bv})
 return results
controls(OUT)
manifest=[]; failures=[]
for name,identity in IDS.items():
 mesh=Path('/rust-meshes')/(name+'.0.mesh');reference=Path('/baseline')/(name+'.reference.trace')
 # Both original owned arrays are retained; reject accidental geometry drift before replay.
 assert mesh.read_bytes()==(Path('/references')/(name+'.0.mesh')).read_bytes()
 lines=reference.read_text().splitlines();queries=bytearray(b'EQRAYS01'+identity.encode()+struct.pack('<I',len(lines)))
 for n,line in enumerate(lines):
  ident,_=row(line);queries+=struct.pack('<I',n)+b''.join(ident[1])
 qp=OUT/(name+'.queries');qp.write_bytes(queries)
 for p in [mesh,reference,qp]:manifest.append({'path':str(p),'bytes':p.stat().st_size,'sha256':sha(p)})
 binary=OUT/(name+'.bin');run(name,[BIN,str(mesh),identity,str(qp),str(binary)])
 ref_binary=OUT/(name+'.reference.bin');run(name+'-reference',['/reference-build/raycaster-reference',str(mesh),identity,str(qp),str(ref_binary)])
 for label,path in [('candidate',binary),('reference',ref_binary)]:
  data=path.read_bytes();cursor=0;out=[];ordinals=[]
  for line in lines:
   fields=line.split()[:7];hit=data[cursor];cursor+=1;assert hit in (0,1);fields.append(str(hit))
   if hit:
    values=struct.unpack_from('<7fI',data,cursor);cursor+=32;assert all(math.isfinite(x) for x in values[:7]);fields.extend(float(x).hex() for x in values[:7]);ordinals.append(values[7])
   out.append(' '.join(fields)+'\n')
  assert cursor==len(data)
  candidate=OUT/(name+'.'+label+'.trace');candidate.write_text(''.join(out));(OUT/(name+'.'+label+'.ordinals.json')).write_text(json.dumps(ordinals))
  try:strict=compare(reference,candidate)
  except AssertionError as e:strict={'failure':str(e)};failures.append(name+' '+label+' strict')
  (OUT/(name+'.'+label+'.strict.json')).write_text(json.dumps(strict,indent=2));(OUT/(name+'.'+label+'.numeric.json')).write_text(json.dumps(report(reference,candidate),indent=2))
 # Reader/replay controls exercise the candidate, preserving the reference files.
 raw=mesh.read_bytes();nv=struct.unpack_from('<I',raw,12)[0]
 for label,offset,value,message in [('schema',8,struct.pack('<I',2),'schema'),('identity',20,b'f'*64,'identity'),('index',84+nv*12,struct.pack('<I',nv),'index'),('count',12,struct.pack('<I',4_000_001),'counts'),('nonfinite',84,struct.pack('<I',0x7fc00000),'vertex')]:
  path=OUT/'bad.mesh';path.write_bytes(raw[:offset]+value+raw[offset+len(value):]);dest=OUT/(name+'-bad-'+label+'.bin');run(name+'-reject-'+label,[BIN,str(path),identity,str(qp),str(dest)],1,message);path.unlink()
 path=OUT/'bad.mesh';path.write_bytes(raw[:-1]);run(name+'-reject-truncated',[BIN,str(path),identity,str(qp),str(OUT/(name+'-bad-truncated.bin'))],1,'length');path.unlink()
 for label,offset,value,message in [('order',76,struct.pack('<I',1),'order'),('identity',8,b'f'*64,'identity'),('count',72,struct.pack('<I',0),'count')]:
  path=OUT/'bad.queries';path.write_bytes(queries[:offset]+value+queries[offset+len(value):]);run(name+'-replay-'+label,[BIN,str(mesh),identity,str(path),str(OUT/(name+'-replay-'+label+'.bin'))],1,message);path.unlink()
(OUT/'artifacts.json').write_text(json.dumps(manifest,indent=2));(OUT/'summary.json').write_text(json.dumps({'failures':failures},indent=2))
if failures:raise SystemExit(1)
