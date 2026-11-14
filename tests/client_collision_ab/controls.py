"""Pure admission and evidence checks; no client transport or credentials."""
import math
import re

WORLD = {'name': 'Client collision AB private', 'host': '127.0.0.1'}
MAP = 'sha256:040343c54e3b02bfd1637ce8fca8839dd676e17c41fb24c042ccfa0bd649435c'
# Existing native_qeynos_route spawn room. 100 ms east stays well short of x=304.825 wall contact.
BOX = ((287, 293), (343, 345), (-.02, .02))

def admit(state, identity, world, now, deadline, server):
    assert world == WORLD and now < deadline, 'world/deadline guard'
    assert (state['session'], state['zone_epoch'], state['self']['id']) == identity, 'session/epoch/entity guard'
    assert state['phase'] == 'ready' and not state.get('failure'), 'phase guard'
    assert state['zone']['name'] == 'qeynos2' and state['zone']['id'] == 2, 'zone guard'
    assert state['geometry']['digest'] == MAP and state['geometry']['status'] == 'ready', 'map guard'
    assert all(state['freshness'][key] <= 2000 for key in ('link_age_ms','network_tick_age_ms','inbound_age_ms')), 'freshness guard'
    body = state['self']
    assert body['name'] == 'Clientab' and body['on_ground'] and not body['in_water'] and not body['attack_on'], 'body guard'
    assert all(math.isfinite(v) and lo <= v <= hi for v,(lo,hi) in zip(body['position'], BOX)) and len(body['position']) == 3, 'position guard'
    assert server['name'] == 'Clientab' and server['aggro'] == 0 and now-server['observed'] < 2.5, 'server aggro/freshness guard'
    assert server['entity'] == identity[2] and server['account'] > 0 and server['character'] > 0, 'server identity guard'

QUERY = re.compile(r'CLIENT_AB_QUERY id=(clear|blocked) time=(\S+) from=(\S+) size=(\S+) eyes=(\S+) to=(\S+) target_size=(\S+) target_eyes=(\S+) ground=(\S+) los=([01])')

def parse_queries(log):
    cases = {}
    for line in log.splitlines():
        if 'CLIENT_AB_QUERY' not in line:
            continue
        m = QUERY.search(line)
        assert m, 'malformed query record'
        name, stamp, *values = m.groups()
        record = dict(zip(('origin','size','eyes','target','target_size','target_eyes','ground','los'), values))
        assert all(math.isfinite(float(v)) for key,value in record.items() for v in value.split(',')), 'nonfinite query'
        assert record['los'] == ('1' if name == 'clear' else '0'), 'positive control failed'
        if name in cases:
            assert cases[name] == record, 'unstable canonical inputs/results'
        cases[name] = record
    assert set(cases) == {'clear','blocked'}, 'missing canonical query IDs'
    return cases

def verdicts(receipt):
    return {'backend_direct_query': receipt.get('canonical_queries', False),
            'client_command_smoke': receipt.get('movement_complete', False),
            'server_pose_observation': receipt.get('server_pose_observed', False),
            'orderly_shutdown': bool(receipt.get('exits')) and all(v == 0 for v in receipt['exits'].values())}
