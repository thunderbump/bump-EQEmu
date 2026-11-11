"""Worker-only retained-mesh inspection. Never changes or qualifies geometry.

Usage: python3 inspect_mesh.py cases.json /inputs /results/structure.json
The input directory must contain <zone>.mesh for all three pinned cases.
"""
import hashlib
import json
import math
import struct
import sys
from pathlib import Path


def inspect(path, case):
    expected = case['geometry']
    with path.open('rb') as f:
        digest = hashlib.file_digest(f, 'sha256').hexdigest()
        if digest != expected['sha256'] or path.stat().st_size != expected['bytes']:
            raise ValueError('mesh identity or size changed')
        f.seek(0)
        header = f.read(84)
        magic, version, nv, ni, identity = struct.unpack('<8sIII64s', header)
        if (magic, version, nv, ni, identity) != (
            b'EQMESH01', 1, expected['vertices'], expected['indices'],
            case['input']['sha256'].encode(),
        ) or ni % 3 or 84 + nv * 12 + ni * 4 != expected['bytes']:
            raise ValueError('header/count/identity mismatch')
        lo, hi = [math.inf] * 3, [-math.inf] * 3
        nonfinite, outside = 0, 0
        # Fixed-size blocks avoid retaining another expanded mesh.
        remaining = nv
        while remaining:
            count = min(remaining, 8192)
            block = f.read(count * 12)
            if len(block) != count * 12:
                raise ValueError('truncated vertices')
            for vertex in struct.iter_unpack('<3f', block):
                for axis, x in enumerate(vertex):
                    if not math.isfinite(x):
                        nonfinite += 1
                        continue
                    outside += abs(x) > 1e8
                    lo[axis] = min(lo[axis], x)
                    hi[axis] = max(hi[axis], x)
            remaining -= count
        used = bytearray(nv)
        repeated, min_index, max_index = 0, nv, -1
        remaining = ni // 3
        while remaining:
            count = min(remaining, 8192)
            block = f.read(count * 12)
            if len(block) != count * 12:
                raise ValueError('truncated indices')
            for a, b, c in struct.iter_unpack('<3I', block):
                repeated += a == b or a == c or b == c
                for index in (a, b, c):
                    if index >= nv:
                        raise ValueError('index out of bounds')
                    used[index] = 1
                    min_index, max_index = min(min_index, index), max(max_index, index)
            remaining -= count
        if f.read(1):
            raise ValueError('trailing bytes')
    return dict(path=case['input']['path'], mesh_sha256=digest,
                vertices=nv, indices=ni, triangles=ni // 3,
                bounds_min=lo, bounds_max=hi, nonfinite_components=nonfinite,
                components_outside_core_range=outside,
                referenced_vertices=sum(used), unreferenced_vertices=nv - sum(used),
                min_index=min_index, max_index=max_index,
                repeated_index_triangles=repeated,
                geometric_degeneracy='not measured', tree='not built')


def main():
    cgroup = Path('/sys/fs/cgroup')
    memory = {name: (cgroup / name).read_text().strip() for name in
              ('memory.max', 'memory.swap.max', 'memory.peak', 'memory.events')}
    if memory['memory.max'] != '2147483648' or memory['memory.swap.max'] != '0':
        raise RuntimeError('requires authorized 2 GiB, zero-swap disposable worker')
    cases_path, inputs, output = map(Path, sys.argv[1:])
    cases = json.loads(cases_path.read_text())['cases']
    report = {'scope': 'investigation only; no engine or candidate qualification',
              'admission': memory, 'cases': []}
    for case in cases:
        name = Path(case['input']['path']).stem
        report['cases'].append(inspect(inputs / (name + '.mesh'), case))
    report['after'] = {name: (cgroup / name).read_text().strip() for name in memory}
    with output.open('x') as f:
        json.dump(report, f, indent=2, allow_nan=False)
        f.write('\n')


if __name__ == '__main__':
    main()
