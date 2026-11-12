"""Guest-only build and bridge checks. Host transport must impose 2 GiB/no swap."""
import subprocess,os,json,time,signal,hashlib
from pathlib import Path
out=Path('/results'); runs=[]
def mem(): return {n:(Path('/sys/fs/cgroup')/n).read_text() for n in ['memory.current','memory.peak','memory.max','memory.swap.max','memory.events']}
def run(name,args,ok=True):
 t=time.monotonic(); before=mem()
 with (out/(name+'.stdout')).open('w') as so,(out/(name+'.stderr')).open('w') as se:
  p=subprocess.Popen(['/usr/bin/time','-v','-o',str(out/(name+'.time')),*args],stdout=so,stderr=se,start_new_session=True)
  try: code=p.wait(timeout=600)
  except subprocess.TimeoutExpired: os.killpg(p.pid,signal.SIGKILL);p.wait();code='timeout'
 runs.append(dict(name=name,args=args,exit=code,seconds=time.monotonic()-t,before=before,after=mem()))
 (out/'runs.json').write_text(json.dumps(runs,indent=2))
 if ok: assert code==0,(name,code)
 return code
assert mem()['memory.max'].strip()=='2147483648' and mem()['memory.swap.max'].strip()=='0'
os.environ['CARGO_TARGET_DIR']='/results/target'
run('toolchain',['rustc','-Vv']);run('compiler',['c++','--version'])
run('lock',['cargo','metadata','--manifest-path','/work/rust/raycaster/Cargo.toml','--frozen','--offline','--format-version','1','--no-deps'])
(out/'Cargo.lock').write_bytes(Path('/work/rust/raycaster/Cargo.lock').read_bytes())
run('fmt',['cargo','fmt','--manifest-path','/work/rust/raycaster/Cargo.toml','--check'])
run('configure',['cmake','-S','/work/tests/map_bridge','-B','/results/build','-DEQEMU_RUST_MAP_RAYCASTER=ON','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
run('build',['cmake','--build','/results/build','-j2'])
run('bridge',['/results/build/bridge_test'])
run('wrapper',['/results/build/wrapper_test'])
run('wrapper-mmf',['/results/build/wrapper_mmf_test'])
run('core',['cargo','test','--manifest-path','/work/rust/raycaster/Cargo.toml','--release','--frozen','--offline'])
for name in ['bridge_test','bridge_replay']:
 p=out/'build'/name
 (out/(name+'.sha256')).write_text(hashlib.sha256(p.read_bytes()).hexdigest())
run('default-configure',['cmake','-S','/work','-B','/results/default','-DEQEMU_BUILD_TESTS=ON','-DCMAKE_TOOLCHAIN_FILE='],False)
run('rust-configure',['cmake','-S','/work','-B','/results/rust','-DEQEMU_RUST_MAP_RAYCASTER=ON','-DEQEMU_BUILD_TESTS=ON','-DCMAKE_TOOLCHAIN_FILE='],False)
exec(Path('/work/tests/map_bridge/replay.py').read_text())

# Three qualified large meshes, one engine/map/process at a time. Test-only identity checks.
assets=Path('/work/experiments/map-translation/large-map-qualification/assets.inc').read_text().splitlines()[1:4]
for line in assets:
 name,_,identity,nv,ni,_,digest=line.split('|')
 mesh=Path('/evidence/raycaster-corpus-20261110/final/assets')/name.removesuffix('.map')/'geometry.mesh'
 assert hashlib.sha256(mesh.read_bytes()).hexdigest()==digest
 q=out/(Path(name).stem+'.queries')
 q.write_bytes(b'EQRAYS01'+identity.encode()+struct.pack('<I',1)+struct.pack('<I6f',0,0,0,1,0,0,-1))
 run(Path(name).stem,['/results/build/bridge_replay',str(mesh),identity,str(q),str(out/(Path(name).stem+'.bin'))])
run('default-focused-configure',['cmake','-S','/work/tests/map_bridge','-B','/results/native','-DEQEMU_RUST_MAP_RAYCASTER=OFF','-DCMAKE_BUILD_TYPE=Release'])
run('default-focused-build',['cmake','--build','/results/native','--target','default_map_compile','-j2'])

# Separate sanitizer archive/build identity. Controls qualify diagnostics first.
os.environ['CARGO_TARGET_DIR']='/results/sanitizer-target'
os.environ['CC']='/usr/bin/gcc'
os.environ['CXX']='/usr/bin/g++'
os.environ['ASAN_OPTIONS']='halt_on_error=1:abort_on_error=0:detect_leaks=1:symbolize=1:exitcode=1'
os.environ['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1:exitcode=1'
os.environ['ASAN_SYMBOLIZER_PATH']='/usr/bin/addr2line'
run('san-configure',['cmake','-S','/work/tests/map_bridge','-B','/results/sanitizer','-DEQEMU_RUST_MAP_RAYCASTER=ON','-DEQEMU_SANITIZERS=ON','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=/usr/bin/g++'])
run('san-build',['cmake','--build','/results/sanitizer','-j2'])
run('control-pass',['/results/sanitizer/sanitizer_controls','pass'])
qualified=True
for name,diagnostic,frame in [('use-after-free','heap-use-after-free','SanitizerUseAfterFree'),('out-of-bounds','heap-buffer-overflow','SanitizerOutOfBounds'),('undefined','runtime error','SanitizerUndefined'),('leak','LeakSanitizer','SanitizerLeak')]:
 code=run('control-'+name,['/results/sanitizer/sanitizer_controls',name],False)
 message=(out/('control-'+name+'.stderr')).read_text()
 qualified = qualified and code!=0 and diagnostic in message and frame in message and 'sanitizer_controls.cpp:' in message
(out/'sanitizer-qualification.json').write_text(json.dumps({'qualified':qualified,'workload_executed':qualified}))
if qualified:
 run('san-bridge',['/results/sanitizer/bridge_test'])
 run('san-wrapper',['/results/sanitizer/wrapper_test'])
else:
 raise SystemExit('Sanitizer controls did not qualify; workload skipped')
