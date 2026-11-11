"""Independent fixed-offset check. No model traversal or coordinate transformation."""
import hashlib,json,struct,zlib
from pathlib import Path
cases=[('breedinggrounds',5792314,5837703,(1661,1660,1613,1),6619240,'OBJ_maus_InnerWalls.MOD'),('chapterhouse',253650,263642,(294,324,323,1),8970529,'OBJ_BFS_HallwallH.MOD'),('chapterhouse',253686,263655,(326,573,574,1),8970529,'OBJ_BFS_HallwallH.MOD')]
out=[]
for name,vo,po,poly,placement,model in cases:
    raw=Path('/inputs/'+name+'.map').read_bytes();d=zlib.decompress(raw[12:])
    assert d[vo:vo+12]==bytes.fromhex('0000c0ff'*3)
    w=0xffc00000
    assert (w>>23)&255==255 and w&0x7fffff and w&(1<<22) # quiet NaN, negative sign
    assert struct.unpack_from('<IIIB',d,po)==poly
    anchor=model.encode()+b'\0';assert d[placement:placement+len(anchor)]==anchor
    transform=d[placement+len(anchor):placement+len(anchor)+36]
    assert all((w&0x7f800000)!=0x7f800000 for w, in struct.iter_unpack('<I',transform))
    out.append(dict(input=name,raw_sha256=hashlib.sha256(raw).hexdigest(),vertex_offset=vo,vertex_hex=d[vo:vo+12].hex(),polygon_offset=po,polygon=poly,placement_offset=placement,placement_name=model,transform_hex=transform.hex()))
Path('/results/independent-excerpts.json').write_text(json.dumps(out,indent=2))
