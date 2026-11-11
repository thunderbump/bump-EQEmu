"""Guest-only generator for a distinctly labeled lab C++ adapter.

Production engine/allocator are included unchanged. The frozen collision driver
is copied, with an exact known-input count envelope and read-only tree statistics.
The generated diff, source and binary must be retained; this is not original
adapter success and provides no Rust result.
"""
import difflib
import hashlib
import json
from pathlib import Path

ROOT = Path('/work')
OUT = Path('/results')
source = ROOT / 'experiments/map-translation/collision/driver.cpp'
original = source.read_text()
cases = json.loads((ROOT / 'experiments/map-translation/corpus-investigation/limits/cases.json').read_text())['cases']
envelope = ' || '.join(f'(id=="{c["input"]["sha256"]}" && nv=={c["geometry"]["vertices"]} && ni=={c["geometry"]["indices"]} && b.size()=={c["geometry"]["bytes"]})' for c in cases)
old = 'require(nv>0 && nv<=4000000 && ni>0 && ni<=12000000 && ni%3==0,"counts");'
new = 'require((' + envelope + ') && nv<=6161009 && ni<=6684999 && ni%3==0,"lab exact envelope counts");'
assert original.count(old) == 1
text = original.replace(old, new)
old = 'auto e=build(m); phase("build");'
new = '''auto e=build(m); phase("build");
 auto* lab=static_cast<RAYCAST_MESH::MyRaycastMesh*>(e.get());
 size_t leafCount=0, maxLeaf=0;
 for(size_t n=0;n<lab->mNodeCount;n++) {
  auto offset=lab->mNodes[n].mLeafTriangleIndex;
  if(offset!=RAYCAST_MESH::TRI_EOF) { leafCount++; maxLeaf=std::max(maxLeaf,size_t(lab->mLeafTriangles[offset])); }
 }
 std::cerr<<"LAB_STATS {\\"nodes\\":"<<lab->mNodeCount<<",\\"node_size\\":"<<sizeof(RAYCAST_MESH::NodeAABB)
 <<",\\"allocated_nodes\\":"<<lab->mMaxNodeCount<<",\\"leaf_nodes\\":"<<leafCount
 <<",\\"leaf_references\\":"<<lab->mLeafTriangles.size()-leafCount<<",\\"leaf_words\\":"<<lab->mLeafTriangles.size()
 <<",\\"leaf_capacity_words\\":"<<lab->mLeafTriangles.capacity()<<",\\"max_leaf_references\\":"<<maxLeaf<<"}\\n";
'''
assert text.count(old) == 1
text = text.replace(old, new)
# Restrict this copied adapter to one-engine record and analytic controls.
old = 'int main(int argc,char** argv) { try {'
assert text.count(old) == 1
text = text.replace(old, old + '\n require(argc>1 && (std::string(argv[1])=="--record" || std::string(argv[1])=="--controls"),"lab record/controls only");')
text = '// INVESTIGATION-ONLY ADAPTER. Engine source below is unchanged.\n#include "/work/zone/raycast_mesh.cpp"\n' + text
(OUT / 'lab_driver.cpp').write_text(text)
(OUT / 'lab_driver.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True), text.splitlines(True), fromfile='frozen/collision/driver.cpp', tofile='lab_driver.cpp')))
(OUT / 'source-hashes.json').write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (source, ROOT / 'zone/raycast_mesh.cpp', ROOT / 'common/memory/ksm.cpp', OUT / 'lab_driver.cpp')}, indent=2))
