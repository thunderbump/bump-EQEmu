"""Guest-only executable reader/replay rejection controls and exact comparator controls."""
import json, struct, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0,'/work/experiments/map-translation/collision')
from compare import controls
identity='a'*64
mesh=b'EQMESH01'+struct.pack('<III',1,3,3)+identity.encode()+struct.pack('<9f3I',0,0,0,4,0,0,0,4,0,0,1,2)
queries=b'EQRAYS01'+identity.encode()+struct.pack('<II6f',1,0,1,1,2,1,1,-2)
binary='/rust-build/release/rust-raycaster-poc'
receipts=[]
with tempfile.TemporaryDirectory() as t:
    root=Path(t);controls(root)
    def run(name,m,q,expected,message=''):
        mp=root/'mesh';qp=root/'queries';out=root/'out';mp.write_bytes(m);qp.write_bytes(q);out.unlink(missing_ok=True)
        p=subprocess.run([binary,str(mp),identity,str(qp),str(out)],capture_output=True,text=True,timeout=10)
        assert p.returncode==expected and message in p.stderr,(name,p.returncode,p.stderr)
        if expected==0:
            result=out.read_bytes();assert result[0]==1 and struct.unpack_from('<3f',result,1)==(1.,1.,0.)
        receipts.append({'control':name,'exit':p.returncode,'diagnostic':message})
    run('analytic',mesh,queries,0)
    def change(raw,offset,value):return raw[:offset]+value+raw[offset+len(value):]
    for name,offset,value,message in [('schema',8,struct.pack('<I',2),'schema'),('identity',20,b'b'*64,'identity'),('index',120,struct.pack('<I',3),'index'),('nonfinite',84,struct.pack('<I',0x7fc00000),'vertex'),('count',12,struct.pack('<I',4000001),'counts')]:
        run(name,change(mesh,offset,value),queries,1,message)
    run('truncated',mesh[:-1],queries,1,'length')
    for name,offset,value,message in [('order',76,struct.pack('<I',1),'order'),('replay_identity',8,b'b'*64,'identity'),('replay_count',72,struct.pack('<I',0),'count')]:
        run(name,mesh,change(queries,offset,value),1,message)
Path('/results/replay-controls.json').write_text(json.dumps({'reader_replay':receipts,'independent_exact_comparator_mutations':'passed'},indent=2))
