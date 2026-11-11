"""Host-only inventory selection. Recheck the pinned choice; never reroll."""
import argparse
import hashlib
import json
from pathlib import Path

SEED = 'brian-extra-map-20261110-v1'

def sha(data):
    return hashlib.sha256(data).hexdigest()


def selection(census):
    collection = census['collections'][0]
    eligible = sorted((m for m in collection['maps']
                       if m['path'].startswith('base/') and m['version'] == '0x2000000'
                       and m['header']['groups'] == 0 and m['header']['tiles'] == 0
                       and Path(m['path']).name not in ('qeynos2.map', 'poknowledge.map')),
                      key=lambda m: m['path'])
    names = ''.join(m['path'] + '\n' for m in eligible).encode('utf-8')
    index = int(sha(SEED.encode('utf-8')), 16) % len(eligible)
    return {'seed': SEED, 'algorithm': 'big-endian SHA256(UTF-8 seed) integer modulo eligible count; zero-based index into lexicographically sorted paths',
            'filter': 'base/*.map; V2; groups=0; tiles=0; exclude qeynos2.map and poknowledge.map. Header-supported pool, resource bounds not prequalified.',
            'eligible_count': len(eligible), 'candidate_list_sha256': sha(names),
            'candidate_list_encoding': 'UTF-8 paths sorted ascending, each followed by LF',
            'selected_index': index, 'collection_root': collection['root'], 'map': eligible[index]}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--census', type=Path, required=True)
    p.add_argument('--inputs', type=Path, required=True)
    p.add_argument('--pin', action='store_true', help='first selection only; exclusively create tracked manifest')
    a = p.parse_args()
    raw = a.census.read_bytes()
    manifest = selection(json.loads(raw))
    manifest['census_sha256'] = sha(raw)
    pinned = Path(__file__).with_name('manifest.json')
    if a.pin:
        with pinned.open('x') as f:
            json.dump(manifest, f, indent=2); f.write('\n')
    else:
        if json.loads(pinned.read_text()) != manifest:
            p.error('census selection differs from pinned manifest; refusing a new choice')
    item = manifest['map']
    source = Path(manifest['collection_root']) / item['path']
    data = source.read_bytes()
    if len(data) != item['bytes'] or sha(data) != item['sha256']:
        p.error('selected actual asset differs from census')
    a.inputs.mkdir(parents=True, exist_ok=True)
    target = a.inputs / Path(item['path']).name
    if target.exists():
        if target.read_bytes() != data:
            p.error('existing copied input differs; preserved without overwrite')
    else:
        with target.open('xb') as f:
            f.write(data)
    print(json.dumps(manifest, indent=2))
