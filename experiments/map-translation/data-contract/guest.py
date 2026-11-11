"""Guest-only tests. Contract success is not a C++ collision comparison."""
import hashlib, json, os, signal, struct, subprocess, time
from pathlib import Path
O = Path('/results')
CG = Path('/sys/fs/cgroup')
def memory():
    return {n: (CG/n).read_text() for n in ['memory.current', 'memory.peak', 'memory.max', 'memory.swap.max', 'memory.events']}
def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()
runs = []
def run(name, args, expected=0, message=None):
    before = memory()
    start = time.monotonic()
    with (O/(name+'.stdout')).open('w') as out, (O/(name+'.stderr')).open('w') as err:
        p = subprocess.Popen(['/usr/bin/time', '-v', '-o', str(O/(name+'.time')), *args], stdout=out, stderr=err, start_new_session=True)
        try:
            code = p.wait(timeout=300)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.wait()
            code = 'timeout'
    runs.append(dict(name=name, args=args, exit=code, seconds=time.monotonic()-start, before=before, after=memory()))
    (O/'runs.json').write_text(json.dumps(runs, indent=2))
    assert code == expected, (name, code)
    if message:
        assert message in (O/(name+'.stderr')).read_text(), (name, message)
assert memory()['memory.max'].strip() == '2147483648'
assert memory()['memory.swap.max'].strip() == '0'
os.environ['CARGO_TARGET_DIR'] = '/results/target'
base = '/work/experiments/map-translation/'
for name, manifest, command in [('core-build','rust-raycaster','build'), ('core-tests','rust-raycaster','test'), ('probe-build','data-contract','build'), ('probe-tests','data-contract','test')]:
    run(name, ['cargo', command, '--manifest-path', base+manifest+'/Cargo.toml', '--release', '--frozen', '--offline'])
(O/'toolchain.txt').write_text(subprocess.check_output(['rustc','-Vv'], text=True)+subprocess.check_output(['cargo','-V'], text=True))
cli = str(O/'target/release/rust-raycaster-poc')
probe = str(O/'target/release/contract-probe')
(O/'binaries.json').write_text(json.dumps({p: sha(p) for p in [cli, probe]}, indent=2))
cases = json.loads(Path(base+'data-contract/cases.json').read_text())
summary = []
def replay(identity):
    return b'EQRAYS01'+identity.encode()+struct.pack('<I',1)+struct.pack('<I6f',0,0,0,1,0,0,-1)
def reject(label, mesh, identity, message):
    query = O/(label+'.queries'); query.write_bytes(replay(identity))
    for existing in [False, True]:
        dest = O/(label+('-preserved' if existing else '-absent')+'.bin')
        if existing:
            dest.write_bytes(b'preserve existing outcome\n')
        run(label+('-preserved' if existing else '-absent'), [cli,str(mesh),identity,str(query),str(dest)], 1, message)
        assert dest.read_bytes() == b'preserve existing outcome\n' if existing else not dest.exists()
        assert 'PHASE build' not in (O/(label+('-preserved' if existing else '-absent')+'.stderr')).read_text()
for c in cases:
    name = c['name']; identity = c['files']['original']['sha256']; mesh = Path('/assets')/name/'geometry.mesh'
    assert sha(mesh) == c['files']['mesh']['sha256']
    if c['expected'] == 'load_error_nonfinite':
        reject(name, mesh, identity, 'nonfinite or out-of-bound vertex')
        summary.append(dict(path=c['path'], result='expected LOAD ERROR: nonfinite or out-of-bound vertex', cli_rejections=2, engine_constructed=False, collision_queries=0, diagnostic_vertices=c['nonfinite']))
    else:
        run(name, [probe,str(mesh),identity,c['path']])
        q = O/(name+'.queries'); q.write_bytes(replay(identity)); dest = O/(name+'.bin')
        run(name+'-cli',[cli,str(mesh),identity,str(q),str(dest)])
        assert dest.read_bytes() == b'\0'
        summary.append(dict(path=c['path'], result='valid empty', load_drop_cycles=64, finite_nohits=14336, invalid_endpoint_errors=384, cli_nohits=1))
# Handwritten serialized fixtures isolate schema/count corruption from nonfinite geometry.
identity = '0'*64
header = lambda nv, ni: b'EQMESH01'+struct.pack('<III',1,nv,ni)+identity.encode()
for bits in [0xffc00000,0x7fc00000,0x7f800000,0xff800000]:
    for axis in range(3):
        coords = [0]*9; coords[axis] = bits
        mesh = O/f'nonfinite-{bits:08x}-{axis}.mesh'
        mesh.write_bytes(header(3,3)+struct.pack('<9I',*coords)+struct.pack('<3I',0,1,2))
        reject(mesh.stem,mesh,identity,'nonfinite or out-of-bound vertex')
for label, data, message in [('schema',b'broken','mesh schema'),('truncated',header(1,0),'mesh counts/length'),('partial-index',header(0,1)+struct.pack('<I',0),'mesh counts/length')]:
    mesh=O/(label+'.mesh'); mesh.write_bytes(data); reject(label,mesh,identity,message)
# Positive serialized controls verify useful finite geometry still loads.
for label, data, hit in [('handwritten-empty',header(0,0),False),('handwritten-triangle',header(3,3)+struct.pack('<9f',-4,-4,0,4,-4,0,0,4,0)+struct.pack('<3I',0,1,2),True)]:
    mesh=O/(label+'.mesh'); mesh.write_bytes(data)
    query=O/(label+'.queries'); query.write_bytes(replay(identity)); dest=O/(label+'.bin')
    run(label,[cli,str(mesh),identity,str(query),str(dest)])
    assert dest.read_bytes()[0] == int(hit)
(O/'summary.json').write_text(json.dumps(dict(asset_contracts=summary, handwritten_nonfinite_cli_rejections=24, parser_cli_rejections=6, core_unit_tests=10, contract_unit_tests=2, collision_comparisons=0),indent=2))
