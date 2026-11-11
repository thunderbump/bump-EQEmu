"""Streaming exact float32 trace comparator, independent of the raycaster."""
import itertools
import math
import struct

def row(line, outcomes=1):
    fields = line.split()
    assert len(fields) >= 8, 'short trace row'
    def bits(s):
        x = float.fromhex(s)
        assert math.isfinite(x), 'nonfinite trace'
        return struct.pack('<f', x)
    identity = (fields[0], tuple(bits(s) for s in fields[1:7]))
    p = 7
    hits = []
    for _ in range(outcomes):
        assert fields[p] in ('0', '1'), 'invalid hit state'
        hit = fields[p] == '1'
        p += 1
        values = tuple(bits(s) for s in fields[p:p+7]) if hit else ()
        assert not hit or len(values) == 7, 'short hit'
        p += 7 if hit else 0
        hits.append((hit, values))
    assert p == len(fields), 'extra trace fields'
    return identity, hits

def compare(a, b, legacy=False):
    counts = {'queries': 0, 'hits': 0, 'misses': 0, 'differences': 0}
    with open(a) as left, open(b) as right:
        for x, y in itertools.zip_longest(left, right):
            assert x is not None and y is not None, 'trace length mismatch'
            xi, xh = row(x, 2 if legacy else 1)
            yi, yh = row(y)
            assert xi == yi, 'query identity/order mismatch'
            assert all(h == yh[0] for h in xh), 'outcome mismatch'
            counts['queries'] += 1
            counts['hits' if yh[0][0] else 'misses'] += 1
    assert counts['queries'] > 0, 'empty trace'
    return counts

def controls(directory):
    from pathlib import Path
    import tempfile
    with tempfile.TemporaryDirectory(dir=directory) as tmp:
        a, b = Path(tmp)/'a', Path(tmp)/'b'
        hit = 'analytic 0x0p+0 0x0p+0 0x1p+0 0x0p+0 0x0p+0 -0x1p+0 1 0x0p+0 0x0p+0 0x0p+0 0x0p+0 0x0p+0 0x1p+0 0x1p+0\n'
        a.write_text(hit); b.write_text(hit); compare(a,b)
        changes = []
        fields = hit.split()
        for i in [0, *range(1,7), *range(8,15)]:
            f = fields.copy(); f[i] = 'changed' if i==0 else '0x1.8p+0'
            changes.append(' '.join(f)+'\n')
        changes += [' '.join(fields[:7]+['0'])+'\n', '', hit+hit]
        for changed in changes:
            b.write_text(changed)
            try:
                compare(a,b)
            except AssertionError:
                continue
            raise AssertionError('comparator accepted mutation')
