"""Host-only header census and frozen hash-ranked selection; no decoder execution."""
import hashlib,json,shutil,struct,zlib
from pathlib import Path
HERE=Path(__file__).resolve().parent
STATE=Path.home()/'.local/state/eqemu-map-translation/additional-pipeline-20261111'
def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    seed=(HERE/'seed.txt').read_bytes(); assert not (HERE/'manifest.json').exists(), 'selection already frozen'
    root=Path('/home/bump/Projects/bump-eqemu/bump-akk-stack/server/maps')
    deployed=Path.home()/'.local/share/eqemu-upstream/state/server/maps'
    prior=Path.home()/'.local/state/eqemu-map-translation/investigation-2026-11-10/asset-census.json'
    old={r['path']:r for r in json.loads(prior.read_text())['collections'][0]['maps']}
    excluded={'qeynos2','poknowledge','griegsend','breedinggrounds','chapterhouse','chelsithreborn','cityofbronze','riftseekers'}
    census=[]; eligible=[]
    for p in sorted((root/'base').glob('*.map')):
        with p.open('rb') as f: raw=f.read()
        r={'path':str(p.relative_to(root)),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'version':raw[:4].hex()}
        if raw[:4]==bytes.fromhex('00000002'):
            h=struct.unpack('<9If',zlib.decompressobj().decompress(raw[12:],40)); r['header_counts']=list(h)
            ok=h[5]>0 and h[6]==0 and h[7]==0 and p.stem not in excluded
        else: ok=False
        r['eligible']=ok; census.append(r)
        if ok: eligible.append(r)
    paths=''.join(r['path']+'\n' for r in eligible).encode()
    selected=sorted(eligible,key=lambda r:hashlib.sha256(seed+r['path'].encode()).digest())[:2]
    inputs=STATE/'inputs'; inputs.mkdir(exist_ok=False)
    for r in selected:
        p=root/r['path']; q=deployed/r['path']; assert sha(p)==sha(q)==r['sha256']==old[r['path']]['sha256']
        target=inputs/p.name; shutil.copyfile(p,target); assert sha(target)==r['sha256']
        r['deployed_sha256']=sha(q); r['prior_sha256']=old[r['path']]['sha256']
    manifest={'seed':seed.decode(),'algorithm':'ascending SHA256(seed file bytes || UTF8 relative path), first two without replacement; no reroll', 'filter':'base/*.map only; V2, placements>0, groups=tiles=0; excludes original three, two known NaN and three known large decoder failures; no other resource prequalification','candidate_paths_sha256':hashlib.sha256(paths).hexdigest(),'eligible_paths':[r['path'] for r in eligible],'census':census,'prior_inventory_sha256':sha(prior),'selected':selected}
    (HERE/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(selected,indent=2))
if __name__=='__main__': main()
