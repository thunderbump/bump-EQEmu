"""Guest adapter rejection controls. Large mutations never reach tree construction."""
import struct,json
from pathlib import Path
def check(c,run,out):
 id=c['input']['sha256'];policy=['--known-asset',c['input']['path'],'/original.map'];raw=Path('/geometry.mesh').read_bytes();nv=c['geometry']['vertices'];ni=c['geometry']['indices']
 rust='/build/target/release/rust-raycaster-poc';cpp='/build/reference';qp=out/'control.queries';qp.write_bytes(b'EQRAYS01'+id.encode()+struct.pack('<I',1)+struct.pack('<I6f',0,0,0,1,0,0,-1))
 def reject(label,path,ident=id,opts=policy,message=None):
  for tool in ['rust','cpp']:
   dest=out/(label+'.'+tool+'.result')
   args=[rust,str(path),ident,str(qp),str(dest),*opts] if tool=='rust' else [cpp,'--record',str(path),ident,'unused',str(dest),*opts]
   run('reject-'+label+'-'+tool,args,1 if tool=='rust' else 2,message)
 for label,offset,value in [('schema',8,struct.pack('<I',2)),('count',12,struct.pack('<I',nv+1)),('indices',16,struct.pack('<I',ni+3)),('hash',84,bytes([raw[84]^1])),('identity',20,b'f'*64)]:
  bad=out/'bad.mesh';bad.write_bytes(raw[:offset]+value+raw[offset+len(value):]);reject(label,bad)
 bad.write_bytes(raw[:-1]);reject('truncated',bad)
 bad.write_bytes(raw+b'!');reject('trailing',bad)
 reject('unknown-policy','/geometry.mesh',opts=['--known-asset','../base/'+Path(c['input']['path']).name,'/original.map'])
 reject('wrong-input','/geometry.mesh',opts=['--known-asset',c['input']['path'],str(qp)])
 original=bytearray(Path('/original.map').read_bytes());original[-1]^=1;ip=out/'bad-original.map';ip.write_bytes(original)
 reject('input-hash','/geometry.mesh',opts=['--known-asset',c['input']['path'],str(ip)],message='policy input hash/length');ip.unlink()
 reject('wrong-id','/geometry.mesh',ident='f'*64)
 reject('default','/geometry.mesh',opts=[])
 # Ordinary adapter semantic checks, independent of policy hashes.
 tiny=b'EQMESH01'+struct.pack('<III',1,3,3)+id.encode()+struct.pack('<9f3I',0,0,0,1,0,0,0,1,0,0,1,2)
 for label,offset,value in [('nonfinite',84,struct.pack('<I',0x7fc00000)),('index',120,struct.pack('<I',3))]:
  bad.write_bytes(tiny[:offset]+value+tiny[offset+len(value):]);reject(label,bad,opts=[])
 # Rust's coordinate range is stricter than the historical C++ reader.
 bad.write_bytes(tiny[:84]+struct.pack('<f',1e9)+tiny[88:]);run('reject-range',[rust,str(bad),id,str(qp),str(out/'range.bin')],1)
 bad.write_bytes(tiny);qb=bytearray(qp.read_bytes());qb[76:80]=struct.pack('<I',1);qp.write_bytes(qb)
 run('reject-order',[rust,str(bad),id,str(qp),str(out/'order.bin')],1)
 bad.unlink();qp.unlink();(out/'controls.json').write_text(json.dumps({'paired_rejections':14,'rust_range_and_order':2}))
