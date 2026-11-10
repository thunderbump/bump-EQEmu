"""Strict schema-1 reader and ordered bit comparator. Never promotes references."""
import argparse
import hashlib
import struct
from pathlib import Path

HEADER = 84

def read(path):
    data = Path(path).read_bytes()
    if len(data) < HEADER:
        raise ValueError(f'{path}: truncated header')
    magic, schema, nv, ni, identity = struct.unpack_from('<8sIII64s', data)
    if magic != b'EQMESH01' or schema != 1:
        raise ValueError(f'{path}: unsupported schema/magic')
    if any(c not in b'0123456789abcdef' for c in identity):
        raise ValueError(f'{path}: invalid input SHA256')
    if ni % 3 or len(data) != HEADER + nv * 12 + ni * 4:
        raise ValueError(f'{path}: counts/length mismatch or truncated payload')
    words = struct.unpack_from(f'<{nv * 3 + ni}I', data, HEADER)
    vertices, indices = words[:nv * 3], words[nv * 3:]
    if any(i >= nv for i in indices):
        raise ValueError(f'{path}: index out of bounds')
    return identity, vertices, indices

def compare(reference, candidate):
    a, b = read(reference), read(candidate)
    if a[0] != b[0]:
        raise ValueError(f'{candidate}: input identity mismatch')
    for label, x, y in zip(('vertex', 'index'), a[1:], b[1:]):
        if len(x) != len(y):
            raise ValueError(f'{candidate}: {label} count {len(y)} != {len(x)}')
        for n, (expected, actual) in enumerate(zip(x, y)):
            if expected != actual:
                location = f'{n // 3}.{("xyz"[n % 3] if label == "vertex" else n % 3)}'
                detail = ''
                if label == 'vertex':
                    ev, av = (struct.unpack('<f', struct.pack('<I', v))[0] for v in (expected, actual))
                    detail = f' values {ev!r} != {av!r}, delta {av - ev!r}'
                raise ValueError(f'{candidate}: first {label} {location}: 0x{expected:08x} != 0x{actual:08x}{detail}')
    return hashlib.sha256(Path(candidate).read_bytes()[HEADER:]).hexdigest()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference')
    parser.add_argument('candidate')
    args = parser.parse_args()
    try:
        print('exact geometry match', compare(args.reference, args.candidate))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'{error}\n')
