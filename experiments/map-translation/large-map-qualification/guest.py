"""Guest-only serial qualification; no tolerance changes or automatic retries."""
import sys, json, subprocess, time, hashlib, struct, math, os, signal
from pathlib import Path
sys.path.insert(0,'/work/experiments/map-translation/collision')
from compare import compare, controls, row
O=Path('/results'); CG=Path('/sys/fs/cgroup')
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def mem():return {n:(CG/n).read_text() for n in ['memory.current','memory.max','memory.swap.max','memory.peak','memory.events']}
runs=[]
def run(name,args,expected=0,message=None):
 before=mem();start=time.monotonic()
 with (O/(name+'.stdout')).open('w') as out,(O/(name+'.stderr')).open('w') as err:
  p=subprocess.Popen(['/usr/bin/time','-v','-o',str(O/(name+'.time')),*args],stdout=out,stderr=err,start_new_session=True)
  try:code=p.wait(timeout=300)
  except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait();code='timeout'
 runs.append(dict(name=name,args=args,exit=code,seconds=time.monotonic()-start,before=before,after=mem()))
 (O/(sys.argv[1]+'.runs.json')).write_text(json.dumps(runs,indent=2));assert code==expected,(name,code)
 if message:assert message in (O/(name+'.stderr')).read_text(),(name,message)
assert mem()['memory.max'].strip()=='2147483648' and mem()['memory.swap.max'].strip()=='0'
if sys.argv[1]=='build':
 os.environ['CARGO_TARGET_DIR']='/results/target'
 os.chdir('/work/experiments/map-translation/rust-raycaster')
 run('fmt',['cargo','fmt','--check'])
 run('build',['cargo','build','--release','--frozen','--offline','-vv'])
 run('tests',['cargo','test','--release','--frozen','--offline'])
 run('cpp-build',['c++','-O3','-DNDEBUG','-std=c++17','-I/work/experiments/map-translation/collision/shim','-I/work','-I/work/zone','/work/experiments/map-translation/collision/driver.cpp','/work/zone/raycast_mesh.cpp','/work/common/memory/ksm.cpp','-o','/results/reference'])
 run('old-configure',['cmake','-S','/work/experiments/map-translation/rust-raycaster','-B','/results/old-reference','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
 run('old-build',['cmake','--build','/results/old-reference','-j2'])
 run('cpp-controls',['/results/reference','--controls'])
 (O/'toolchain.txt').write_text(subprocess.check_output(['rustc','-Vv'],text=True)+subprocess.check_output(['c++','--version'],text=True))
 for name,cmd in [('rust','/results/target/release/rust-raycaster-poc'),('cpp','/results/reference'),('regression-cpp','/results/old-reference/raycaster-reference')]:
  (O/(name+'.identity.json')).write_text(json.dumps({'sha256':sha(cmd),'ldd':subprocess.check_output(['ldd',cmd],text=True),'FP':'O3/default CPU, no fast math, default process FP environment'},indent=2))
elif sys.argv[1]=='regressions':
 import runpy
 os.symlink('/build/target','/rust-build');os.symlink('/build/old-reference','/reference-build')
 os.chdir('/work/experiments/map-translation/rust-raycaster')
 runpy.run_path('qualify.py',run_name='__main__')
else:
 c=json.loads(Path('/case.json').read_text());id=c['input']['sha256'];name=Path(c['input']['path']).stem
 assert sha('/geometry.mesh')==c['geometry']['sha256'] and sha('/original.map')==id
 controls(O)
 from controls import check
 if sys.argv[1]=='controls':
  check(c,run,O);sys.exit(0)
 opts=['--known-asset',c['input']['path'],'/original.map']
 if sys.argv[1]=='reference':
  run('reference',['/build/reference','--record','/geometry.mesh',id,'unused','/results/reference.trace',*opts])
  sys.exit(0)
 lines=(O/'reference.trace').read_text().splitlines();assert len(lines)==5504
 q=bytearray(b'EQRAYS01'+id.encode()+struct.pack('<I',len(lines)))
 for n,line in enumerate(lines):ident,_=row(line);q+=struct.pack('<I',n)+b''.join(ident[1])
 (O/'queries.bin').write_bytes(q)
 run('rust',['/build/target/release/rust-raycaster-poc','/geometry.mesh',id,'/results/queries.bin','/results/rust.bin',*opts])
 b=(O/'rust.bin').read_bytes();p=0;out=[]
 for line in lines:
  f=line.split()[:7];hit=b[p];p+=1;assert hit in [0,1];f.append(str(hit))
  if hit:
   values=struct.unpack_from('<7fI',b,p);p+=32;assert all(math.isfinite(x) for x in values[:7]);f.extend(float(x).hex() for x in values[:7])
  out.append(' '.join(f)+'\n')
 assert p==len(b);(O/'rust.trace').write_text(''.join(out))
 # Independent strict comparator checks category, endpoint bits/order, classifier and all seven valid fields.
 try:exact=compare(O/'reference.trace',O/'rust.trace')
 except AssertionError as e:exact={'failure':str(e)}
 diag={'classification_changes':0,'field_bit_changes':0,'max_abs':0.,'max_relative':0.,'max_ulp':0,'signed_zero_changes':0,'nonfinite':0,'first_failures':[]}
 def ordered(b):
  u=int.from_bytes(b,'little');return 0x80000000-(u&0x7fffffff) if u&0x80000000 else 0x80000000+u
 for n,(a,b) in enumerate(zip(lines,out,strict=True)):
  ai,ah=row(a);bi,bh=row(b);assert ai==bi
  if ah[0][0]!=bh[0][0]:diag['classification_changes']+=1
  if ah[0][0] and bh[0][0]:
   for k,(x,y) in enumerate(zip(ah[0][1],bh[0][1])):
    if x==y:continue
    av=struct.unpack('<f',x)[0];bv=struct.unpack('<f',y)[0];delta=abs(av-bv)
    diag['field_bit_changes']+=1;diag['max_abs']=max(diag['max_abs'],delta);diag['max_relative']=max(diag['max_relative'],delta/abs(av) if av else 0);diag['max_ulp']=max(diag['max_ulp'],abs(ordered(x)-ordered(y)));diag['signed_zero_changes']+=av==bv==0
    if len(diag['first_failures'])<20:diag['first_failures'].append([n,k,x.hex(),y.hex()])
 (O/'summary.json').write_text(json.dumps({'exact':exact,'numeric':diag,'mesh_sha256':sha('/geometry.mesh'),'reference_sha256':sha(O/'reference.trace'),'queries_sha256':sha(O/'queries.bin'),'candidate_sha256':sha(O/'rust.trace')},indent=2))
 assert 'failure' not in exact and exact['hits']>100 and exact['misses']>100
