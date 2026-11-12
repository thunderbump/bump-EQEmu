"""Frozen original endpoint replay through maintained CXX adapter; comparator is unchanged."""
import sys,struct,hashlib,json
from pathlib import Path
sys.path.insert(0,'/work/experiments/map-translation/collision')
from compare import row,compare,controls
controls(out)
ids={'qeynos2':'040343c54e3b02bfd1637ce8fca8839dd676e17c41fb24c042ccfa0bd649435c','poknowledge':'794b618d86852b47bd593424129aaf34eee326e5e3fec7290aeeb0e32bc41d20'}
traces={'qeynos2':'891a5edfb2761a2c70be91e261355e3326e9674b26b57f5a8dbb552084193b3f','poknowledge':'24350316805971e439651cc7349724348c3307d27b2644df185434663af4e0b1'}
meshes={'qeynos2':'d9860bb39b0b4588534e8294ea3de92ba979dd63f5eadafdd08503feb8541cfa','poknowledge':'ecdbcf4223234c47f1f7125a493421b689fcb6fb9eda507e4ac8137b8ed1ce31'}
summary={}
for name,identity in ids.items():
 mesh=Path('/evidence/rust-poc-20261110/qualified/results/rust')/(name+'.0.mesh')
 ref=Path('/evidence/raycaster-memory-20261110/ordinary-01/results')/(name+'.reference.trace')
 assert hashlib.sha256(mesh.read_bytes()).hexdigest()==meshes[name]
 assert hashlib.sha256(ref.read_bytes()).hexdigest()==traces[name]
 lines=ref.read_text().splitlines(); assert len(lines)==5504
 q=out/(name+'.queries');q.write_bytes(b'EQRAYS01'+identity.encode()+struct.pack('<I',len(lines))+b''.join(struct.pack('<I',k)+b''.join(row(line)[0][1]) for k,line in enumerate(lines)))
 binary=out/(name+'.bin');run(name,['/results/build/bridge_replay',str(mesh),identity,str(q),str(binary)])
 data=binary.read_bytes();cursor=0;rendered=[]
 for line in lines:
  fields=line.split()[:7];hit=data[cursor];cursor+=1;assert hit in (0,1);fields.append(str(hit))
  if hit:
   values=struct.unpack_from('<7fI',data,cursor);cursor+=32;fields.extend(float(x).hex() for x in values[:7])
  rendered.append(' '.join(fields)+'\n')
 assert cursor==len(data)
 dest=out/(name+'.bridge.trace');dest.write_text(''.join(rendered));summary[name]=compare(ref,dest)
(out/'parity.json').write_text(json.dumps(summary,indent=2))
