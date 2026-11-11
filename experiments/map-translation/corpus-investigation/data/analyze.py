"""Analysis only. Run inside bounded worker; offsets are zero-based decompressed V2 bytes."""
import collections, hashlib, json, math, struct, zlib
from pathlib import Path

def sha(b): return hashlib.sha256(b).hexdigest()
def badbits(w): return w & 0x7f800000 == 0x7f800000
def words(b): return (x[0] for x in struct.iter_unpack('<I',b))
def scan(b, offset, count, label):
    assert 0<=offset<=offset+count*12<=len(b)
    return [dict(record=i//3,axis='xyz'[i%3],offset=offset+i*4,bits=f'{w:08x}',kind='nan' if w&0x7fffff else 'inf',section=label) for i,w in enumerate(words(b[offset:offset+count*12])) if badbits(w)]
def main(name):
    raw=Path('/inputs/'+name+'.map').read_bytes(); mesh=Path('/inputs/'+name+'.mesh').read_bytes()
    magic,schema,nv,ni,identity=struct.unpack_from('<8sIII64s',mesh)
    assert magic==b'EQMESH01' and schema==1 and len(mesh)==84+nv*12+ni*4 and identity.decode()==sha(raw)
    bad=scan(mesh,84,nv,'export'); bv={r['record'] for r in bad}; refs=collections.defaultdict(list)
    for i,w in enumerate(words(mesh[84+nv*12:])):
        assert w<nv
        if w in bv: refs[w].append(i//3)
    result=dict(input_sha256=sha(raw),bytes=len(raw),mesh_sha256=sha(mesh),mesh_bytes=len(mesh),vertices=nv,indices=ni,nonfinite=bad,references=dict(refs))
    if raw[:4]==b'\0\0\0\1':
        result['v1']=dict(hex=raw.hex(),face_count=struct.unpack_from('<I',raw,4)[0],node_count=struct.unpack_from('<H',raw,8)[0],facelist_count=struct.unpack_from('<I',raw,10)[0],body_hex=raw[14:].hex())
        result['v1']['node_scalars']=struct.unpack_from('<4fBII',raw,14)
        assert len(raw)==39 and struct.unpack_from('<4fBII',raw,14)==(struct.unpack('<f',struct.pack('<f',1e12))[0],)*2+(struct.unpack('<f',struct.pack('<f',-1e12))[0],)*2+(1,0,0)
    else:
        version,cs,ds=struct.unpack_from('<III',raw); assert version==0x02000000 and len(raw)==12+cs
        d=zlib.decompress(raw[12:]); assert len(d)==ds
        Path('/results/'+name+'.inflated').write_bytes(d)
        h=struct.unpack_from('<9If',d); vc,ic,nvc,nic,mc,pc,gc,tc,q,u=h
        p=40; source_bad=scan(d,p,vc,'collidable');p+=vc*12+ic*4
        source_bad+=scan(d,p,nvc,'noncollidable');p+=nvc*12+nic*4
        models=[]; model_details={}; provenance=[]; output_vertex=vc; output_index=ic
        def take(fmt):
            nonlocal p
            size=struct.calcsize('<'+fmt); assert p+size<=len(d)
            v=struct.unpack_from('<'+fmt,d,p);p+=size;return v
        def string():
            nonlocal p
            end=d.index(0,p);s=d[p:end].decode('ascii');p=end+1;return s
        for m in range(mc):
            start=p; mn=string();assert mn not in model_details;mv,mp=take('II');vo=p
            source_bad+=scan(d,p,mv,'model:'+mn);p+=mv*12
            assert p+mp*13<=len(d)
            visible=0; polys=[]
            for j in range(mp):
                po=p; a,b,c,v=take('IIIB');assert max(a,b,c)<mv;visible+=bool(v);polys.append((po,a,b,c,v))
            model_details[mn]=(m,vo,mv,polys)
            models.append(dict(ordinal=m,name=mn,offset=start,vertex_offset=vo,vertices=mv,polygons=mp,visible=visible))
        placements=[]
        for i in range(pc):
            start=p;mn=string();off=p;values=take('9f')
            if mn in model_details:
                m,vo,mv,polys=model_details[mn]
                for j,(po,a,b,c,v) in enumerate(polys):
                    if not v:continue
                    for corner,vi in enumerate((a,b,c)):
                        chunk=d[vo+vi*12:vo+(vi+1)*12]
                        if any(badbits(w) for w in words(chunk)):
                            provenance.append(dict(model_ordinal=m,model=mn,model_vertex=vi,raw_vertex_offset=vo+vi*12,raw_vertex_hex=chunk.hex(),polygon=j,polygon_offset=po,polygon_hex=d[po:po+13].hex(),corner=corner,placement=i,placement_offset=start,placement_hex=d[start:p].hex(),output_vertex=output_vertex+corner,output_triangle=output_index//3))
                    output_vertex+=3;output_index+=3
            placements.append(dict(ordinal=i,name=mn,offset=start,scalar_offset=off,values=values,hex=d[off:p].hex()))
        result['raw_v2']=dict(header=h,decompressed_bytes=len(d),decompressed_sha256=sha(d),after_placements=p,source_nonfinite=source_bad,models=models,placements=placements)
        # Group/terrain parser added only if declared. Never infer a complete cursor if absent.
        result['raw_v2']['remaining_hex_prefix']=d[p:p+64].hex()
        result['provenance']=provenance
        assert gc==tc==0 and p==len(d), 'this diagnostic only covers no-group/no-terrain assets'
        assert (output_vertex,output_index)==(nv,ni)
        assert {r['output_vertex'] for r in provenance}==bv, 'all and only exported nonfinite vertices must be localized'
        assert all(r['output_triangle'] in refs[r['output_vertex']] for r in provenance)
    Path('/results/'+name+'.json').write_text(json.dumps(result,indent=2,allow_nan=True))
for n in ('breedinggrounds','chapterhouse','firiona_lit','warslikswood_lit'):main(n)
comparison=[]
for n in ('firiona','warslikswood'):
    b=Path('/inputs/'+n+'.map').read_bytes();v,cs,ds=struct.unpack_from('<III',b)
    assert v==0x02000000 and cs==len(b)-12
    d=zlib.decompress(b[12:]);assert len(d)==ds
    comparison.append(dict(name=n,sha256=sha(b),bytes=len(b),v2_header=struct.unpack_from('<9If',d)))
Path('/results/counterparts.json').write_text(json.dumps(comparison,indent=2))

