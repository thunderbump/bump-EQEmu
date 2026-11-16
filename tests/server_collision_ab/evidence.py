"""Strict fixed-query comparator. Missing movement evidence never passes collision."""
import math
import re

IDENTITY = ('id', 'npc', 'from', 'size', 'origin', 'adjusted', 'eyes', 'to',
            'target_size', 'target_eyes')


def parse_queries(text):
    rows = {}
    for payload in re.findall(r'SERVER_COLLISION_QUERY ([^\r\n]+)', text):
        row = dict(re.findall(r'(\w+)=([^\s]+)', payload))
        key = row['id']
        if key in rows:
            raise ValueError('duplicate query identity')
        for field in (*IDENTITY, 'time', 'entity', 'ground', 'los'):
            if field not in row:
                raise ValueError('missing query field: ' + field)
        for field in ('from', 'size', 'origin', 'adjusted', 'eyes', 'to', 'target_size',
                      'target_eyes', 'ground', 'time'):
            if not all(math.isfinite(float(n)) for n in row[field].split(',')):
                raise ValueError('nonfinite query')
        if row['los'] not in ('0', '1') or float(row['ground']) == -99999:
            raise ValueError('invalid ground/LoS result')
        rows[key] = row
    if set(rows) != {'start', 'clear', 'blocked'}:
        raise ValueError('incomplete fixed queries')
    if rows['clear']['los'] != '1' or rows['blocked']['los'] != '0':
        raise ValueError('positive clear/wall control failed')
    return rows


def compare(reference, candidate):
    """Exact identity, hit/miss and numeric ground, matching prior exact A/B tests."""
    if set(reference) != {'start', 'clear', 'blocked'} or set(candidate) != set(reference):
        raise ValueError('query set changed')
    for key, before in reference.items():
        after = candidate[key]
        for field in (*IDENTITY, 'los'):
            if before[field] != after[field]:
                raise ValueError('query identity/decision changed: ' + key + '/' + field)
        a, b = float(before['ground']), float(after['ground'])
        if not math.isfinite(a) or not math.isfinite(b) or a != b:
            raise ValueError('ground changed: ' + key)
    return True


def verdict(receipt):
    return {'fixed_queries': bool(receipt.get('fixed_queries_complete')),
            'collision': bool(receipt.get('fixed_queries_complete') and receipt.get('route_callers_complete')),
            'shutdown': bool(receipt.get('exits')) and all(v == 0 for v in receipt['exits'].values())}
