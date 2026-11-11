"""Guest-only stages; unchanged production decoder and query recipe."""
import collections, hashlib, json, math, os, signal, struct, subprocess, sys, time, zlib
from pathlib import Path
sys.path.insert(0, '/work/experiments/map-translation/collision')
from compare import compare, controls, row
O = Path('/results')
def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
def save(name, obj): (O/name).write_text(json.dumps(obj, indent=2))
def memory():
    return {n: (Path('/sys/fs/cgroup')/n).read_text() for n in ('memory.current','memory.peak','memory.events','memory.max','memory.swap.max')}
def run(stage, args, limit):
    report['stage'] = stage; save('report.json', report)
    before = memory(); start = time.monotonic()
    with (O/(stage+'.stdout')).open('w') as out, (O/(stage+'.stderr')).open('w') as err:
        proc = subprocess.Popen(['/usr/bin/time','-v','-o',str(O/(stage+'.time')),*args], stdout=out, stderr=err, start_new_session=True)
        try: code = proc.wait(timeout=limit)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL); proc.wait(); code = 'timeout'
    report['stages'].append(dict(stage=stage, exit=code, seconds=time.monotonic()-start, before=before, after=memory()))
    save('report.json', report)
    if code != 0: raise RuntimeError(f'{stage}: {code}')
def ordered(b):
    u = struct.unpack('<I', b)[0]
    return 0x80000000-(u&0x7fffffff) if u&0x80000000 else 0x80000000+u
report = {'stage':'admission','outcome':'incomplete','stages':[]}
try:
    admission = memory(); save('admission.json', admission)
    assert int(admission['memory.max']) == 2*1024**3 and int(admission['memory.swap.max']) == 0
    identity = sys.argv[1]
    # Census only. Header declarations are not branch execution coverage.
    source=Path('/input.map').read_bytes()
    if source[:4]==bytes.fromhex('00000002'):
        decoded=zlib.decompress(source[12:]); header=struct.unpack_from('<9If',decoded)
        report['header_declarations']={'models':header[4],'placements':header[5],'groups':header[6],'tiles':header[7],'proof_of_branch_execution':False}
        del decoded
    del source
    assert sha(Path('/input.map')) == identity, 'input integrity'
    run('cpp-decode', ['/capture/map-capture','/input.map',str(O/'geometry.mesh'),identity],120)
    report['stage']='sanity'; save('report.json',report)
    mesh=O/'geometry.mesh'; raw=mesh.read_bytes()
    assert raw[:8]==b'EQMESH01' and struct.unpack_from('<I',raw,8)[0]==1 and raw[20:84]==identity.encode(), 'schema/identity'
    nv,ni=struct.unpack_from('<II',raw,12)
    assert len(raw)==84+12*nv+4*ni and ni%3==0, 'counts/length'
    assert all(math.isfinite(x[0]) for x in struct.iter_unpack('<f',raw[84:84+12*nv])), 'nonfinite geometry'
    assert all(x[0]<nv for x in struct.iter_unpack('<I',raw[84+12*nv:])), 'index bounds'
    report['geometry']={'vertices':nv,'indices':ni,'bytes':len(raw),'sha256':sha(mesh)}
    del raw
    if not nv or not ni: raise RuntimeError('empty geometry')
    run('cpp-query',['/collision/collision-eval','--record',str(mesh),identity,'unused',str(O/'reference.trace')],120)
    controls(O)
    lines=(O/'reference.trace').read_text().splitlines()
    queries=bytearray(b'EQRAYS01'+identity.encode()+struct.pack('<I',len(lines)))
    categories=collections.defaultdict(lambda:collections.Counter())
    for n,line in enumerate(lines):
        ident,h=row(line); queries+=struct.pack('<I',n)+b''.join(ident[1]); categories[ident[0]]['queries']+=1; categories[ident[0]]['hits' if h[0][0] else 'misses']+=1
    (O/'queries.bin').write_bytes(queries)
    report['queries']={'count':len(lines),'sha256':sha(O/'queries.bin'),'trace_sha256':sha(O/'reference.trace'),'categories':dict(categories),'seed':0x854f2026,'recipe':'unchanged collision/driver.cpp queries'}
    # Historical references are immutable and authenticated, never regenerated from Rust.
    if Path('/historical.trace').exists():
        expected={'040343c54e3b02bfd1637ce8fca8839dd676e17c41fb24c042ccfa0bd649435c':'891a5edfb2761a2c70be91e261355e3326e9674b26b57f5a8dbb552084193b3f','794b618d86852b47bd593424129aaf34eee326e5e3fec7290aeeb0e32bc41d20':'24350316805971e439651cc7349724348c3307d27b2644df185434663af4e0b1'}
        assert sha(Path('/historical.trace'))==expected[identity], 'historical authentication'
        report['historical']=compare(Path('/historical.trace'),O/'reference.trace')
    run('rust-query',['/rust-build/release/rust-raycaster-poc',str(mesh),identity,str(O/'queries.bin'),str(O/'candidate.bin')],120)
    assert sha(mesh)==report['geometry']['sha256'] and sha(O/'queries.bin')==report['queries']['sha256'], 'shared bytes mutated'
    report['stage']='comparator'; save('report.json',report)
    data=(O/'candidate.bin').read_bytes(); cursor=0; output=[]; ids=[]
    for line in lines:
        fields=line.split()[:7]; flag=data[cursor]; cursor+=1; assert flag in (0,1)
        fields.append(str(flag))
        if flag:
            values=struct.unpack_from('<7fI',data,cursor); cursor+=32
            assert all(math.isfinite(x) for x in values[:7]), 'nonfinite candidate'
            fields.extend(x.hex() for x in values[:7]); ids.append(values[7])
        else: ids.append(None)
        output.append(' '.join(fields)+'\n')
    assert cursor==len(data), 'candidate length'
    (O/'candidate.trace').write_text(''.join(output))
    # Independent exact comparator runs before diagnostics. IDs are candidate-only.
    try: report['comparison']=compare(O/'reference.trace',O/'candidate.trace'); exact=True
    except AssertionError as e: report['comparison']={'failure':str(e)}; exact=False
    diagnostic={'queries':len(lines),'hits':0,'misses':0,'classification_changes':0,'field_bit_changes':0,'max_abs':0.,'max_relative':0.,'max_ulp':0,'signed_zero':0,'nonfinite':0,'examples':[]}
    for n,(a,b) in enumerate(zip(lines,output,strict=True)):
        _,ah=row(a); _,bh=row(b); ah,bh=ah[0],bh[0]; diagnostic['hits' if ah[0] else 'misses']+=1
        if ah[0]!=bh[0]: diagnostic['classification_changes']+=1
        changes=[]
        if ah[0] and bh[0]:
            for k,(x,y) in enumerate(zip(ah[1],bh[1])):
                if x==y: continue
                xf,yf=struct.unpack('<f',x)[0],struct.unpack('<f',y)[0]; delta=abs(xf-yf)
                diagnostic['field_bit_changes']+=1; diagnostic['max_abs']=max(diagnostic['max_abs'],delta); diagnostic['max_ulp']=max(diagnostic['max_ulp'],abs(ordered(x)-ordered(y)))
                if xf: diagnostic['max_relative']=max(diagnostic['max_relative'],delta/abs(xf))
                if xf==yf==0: diagnostic['signed_zero']+=1
                changes.append({'field':k,'reference_bits':x.hex(),'candidate_bits':y.hex()})
        if (changes or ah[0]!=bh[0]) and len(diagnostic['examples'])<8:
            diagnostic['examples'].append({'row':n,'ray':a.split()[:7],'reference':a,'candidate':b.strip(),'candidate_triangle':ids[n],'fields':changes})
    # Extract bounded candidate triangles using original ordered array IDs before mesh cleanup.
    with mesh.open('rb') as f:
        for example in diagnostic['examples']:
            tri=example['candidate_triangle']
            if tri is None: continue
            f.seek(84+nv*12+tri*12); indices=struct.unpack('<3I',f.read(12)); vertices=[]
            for index in indices:
                f.seek(84+index*12); vertices.append(struct.unpack('<3f',f.read(12)))
            example['original_vertex_ids']=indices; example['triangle_vertices']=vertices
            example['oracle_status']='candidate triangle retained; reference public API exposes no triangle ID; independent oracle followup required'
    report['numeric']=diagnostic
    report['coverage']='useful hit and miss' if diagnostic['hits'] and diagnostic['misses'] else 'limited: all-hit or all-miss; not qualified'
    report['outcome']='pass' if exact and diagnostic['hits'] and diagnostic['misses'] else 'comparison-failure' if not exact else 'coverage-limited'
    report['stage']='complete'
except Exception as e:
    report['error']=str(e); report['outcome']='stage-failure'
    if report['stages'] and report['stages'][-1]['exit']=='timeout': report['failure_category']='timeout'
    elif report['stages'] and 'oom_kill 0' not in report['stages'][-1]['after']['memory.events']: report['failure_category']='oom-evidence-see-events'
    else: report['failure_category']=report['stage']
    if report['stage'] in ('cpp-query','rust-query'):
        error=(O/(report['stage']+'.stderr')).read_text()
        if any(s in error for s in ('counts','byte limit','resource','limit')): report['failure_category']=report['stage']+'-adapter-resource-rejection'
finally:
    save('report.json',report)
