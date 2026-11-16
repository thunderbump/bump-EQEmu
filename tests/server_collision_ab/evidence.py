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


def parse_route(text, backend):
    """Require actual native calls, finite adjusted endpoints, ground and stopped pose."""
    records = []
    for payload in re.findall(r'SERVER_COLLISION_NATIVE ([^\r\n]+)', text):
        row = dict(re.findall(r'(\w+)=([^\s]+)', payload))
        if row.get('backend') != backend:
            raise ValueError('wrong native backend')
        for key in ('from', 'to', 'result'):
            if not all(math.isfinite(float(n)) for n in row[key].split(',')):
                raise ValueError('nonfinite native observation')
        if int(row['remaining']) == 0:
            raise ValueError('native observation budget exhausted')
        records.append(row)
    required = {('FindGroundZ','FindBestZ'), ('CheckLosFN','CheckLoS'),
                ('FindDestGroundZ','FindBestZ'), ('AdjustRoute','FindBestZ'),
                ('AdjustRoute','node'), ('UpdatePathGround','admission'), ('FixZ','pose')}
    if not required <= {(r['caller'],r['kind']) for r in records}:
        raise ValueError('required native caller absent')
    if any(float(r['result']) <= 0 for r in records if r['kind']=='admission'):
        raise ValueError('partial/stuck/empty route')
    floors = {}
    for payload in re.findall(r'SERVER_COLLISION_FLOOR ([^\r\n]+)', text):
        row = dict(re.findall(r'(\w+)=([^\s]+)', payload))
        if row['id'] in floors: raise ValueError('duplicate floor')
        floors[row['id']] = row
    if set(floors) != {'route_start','transition','destination'}:
        raise ValueError('route floor samples absent')
    heights = [float(r['ground']) for r in floors.values()]
    if not all(math.isfinite(z) and z != -99999 for z in heights) or max(heights)-min(heights)<0.1:
        raise ValueError('no valid height transition')
    poses = []
    for payload in re.findall(r'SERVER_COLLISION_POSE ([^\r\n]+)', text):
        row = dict(re.findall(r'(\w+)=([^\s]+)', payload))
        values = [float(n) for n in row['xyz'].split(',')]+[float(row['ground'])]
        if not all(math.isfinite(n) for n in values) or values[3] == -99999:
            raise ValueError('invalid pose/ground')
        # Rat model=36, size=6 uses GetZOffset=1*size*.2=1.2.
        offsets = [float(r['result']) for r in records if r['caller']=='FixZ' and r['kind']=='pose']
        if abs(values[2]-values[3]-offsets[-1]) > 0.05:
            raise ValueError('pose not ground relative')
        poses.append(row)
    if len(poses)<3 or 'SERVER_COLLISION_ROUTE_DONE' not in text:
        raise ValueError('missing stopped route')
    for row in poses[-3:]:
        x,y,z=map(float,row['xyz'].split(','))
        if abs(x+490.38)>=0.5 or abs(y+118)>=0.5 or row['moving']!='0':
            raise ValueError('wrong final pose/state')
    return {'native':records,'floors':floors,'poses':poses}


def compare_route(before, after):
    """Exact fixed callers and nodes. Movement cadence is intentionally unconstrained."""
    if before['floors'] != after['floors']:
        raise ValueError('fixed route ground changed')
    def stable(route):
        return [{k:v for k,v in r.items() if k not in ('backend','entity','remaining')}
                for r in route['native'] if r['caller'] in ('AdjustRoute','UpdatePathGround')]
    if stable(before) != stable(after):
        raise ValueError('route admission/nodes changed')
    if before['poses'][-1]['xyz'] != after['poses'][-1]['xyz']:
        raise ValueError('final pose changed')
    return True
