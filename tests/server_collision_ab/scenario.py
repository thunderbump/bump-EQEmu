"""Opt-in extension to the existing fresh-DB runtime. No client or shared state."""
import json
import time
from pathlib import Path
from evidence import parse_queries, parse_route


def inventory(env):
    # Capture original public fixture identities before the shared worker prunes spawns.
    sql = """USE smoke;
SELECT * FROM spawn2 WHERE id=10863;
SELECT * FROM npc_types WHERE id=2011;
SELECT * FROM grid_entries WHERE zoneid=2 ORDER BY gridid,number;
SELECT rule_name,rule_value FROM rule_values WHERE rule_name LIKE 'Map:%';
"""
    result = env['command'](['mariadb', '--no-defaults', '--socket=/runtime/mysql.sock',
                             '-uroot', '--batch'], input=sql.encode(), capture_output=True)
    (env['results'] / 'public-fixture.tsv').write_bytes(result.stdout)


def configure(env):
    env['query']("""USE smoke;
UPDATE spawn2 SET x=297.75,y=344,z=3.75,pathgrid=0 WHERE id=10863;
UPDATE npc_types SET size=6,aggroradius=0,assistradius=0,npc_faction_id=0,runspeed=1.25 WHERE id=2011;
UPDATE rule_values SET rule_value='1' WHERE rule_name='Map:FindBestZHeightAdjust';
UPDATE rule_values SET rule_value='true' WHERE rule_name='Map:FixZWhenPathing';
UPDATE logsys_categories SET log_to_console=1 WHERE log_category_description IN ('Fix Z','Pathing');
""")
    active = env['command'](['mariadb', '--no-defaults', '--socket=/runtime/mysql.sock',
                             '-uroot', '--batch'], input=b"USE smoke; SELECT rule_name,rule_value FROM rule_values WHERE rule_name LIKE 'Map:%';", capture_output=True)
    (env['results'] / 'configured-map-rules.tsv').write_bytes(active.stdout)
    zone = json.loads(Path('/inputs.json').read_text()).get('zone','qeynos2')
    if zone == 'poknowledge':
        env['query']("USE smoke; UPDATE spawn2 SET zone='poknowledge',x=313.068,y=-560.554,z=-155.671 WHERE id=10863;")
    import shutil
    shutil.copyfile('/probe.pl', env['root'] / ('quests/'+zone+'/v0/2011.pl'))


def run(env):
    name = json.loads(Path('/inputs.json').read_text()).get('zone','qeynos2')
    zone = env['start'](name, ['/binaries/zone', name+':7001'])
    env['receipt']['executables'] = {}
    import hashlib
    for name, process in env['processes']:
        with Path('/proc', str(process.pid), 'exe').open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        env['receipt']['executables'][name] = {'pid': process.pid, 'sha256': digest}
    inputs = json.loads(Path('/inputs.json').read_text())
    expected = next(f['sha256'] for f in inputs['files'] if f['guest_path'] == '/binaries/zone')
    assert env['receipt']['executables'][name]['sha256'] == expected, 'executed zone differs'
    deadline = time.monotonic() + 120
    log = env['results'] / (name+'-smoke.log')
    while time.monotonic() < deadline:
        assert all(p.poll() is None for _, p in env['processes']), 'owned child exited'
        text = log.read_text(errors='replace')
        if 'SERVER_COLLISION_ROUTE_DONE' in text:
            assert 'Loaded V2 Map File' in text or 'Loaded .MMF Map File' in text, 'map load not observed'
            queries = parse_queries(text)
            env['receipt'].update(ready=True, fixed_queries_complete=True, queries=queries)
            break
        time.sleep(.2)
    else:
        raise RuntimeError('production fixed query deadline expired')
    try:
        route = parse_route(text, json.loads(Path('/inputs.json').read_text())['backend'])
        env['receipt'].update(route=route, route_callers_complete=True, route_state='stopped')
    finally:
        (env['root'] / 'stop-map-validation').touch()
    # Shared cleanup expects this timer acknowledgement before terminating zone.
    (env['root'] / 'stop-map-validation').touch()
    until = time.monotonic() + 3
    while time.monotonic() < until:
        if 'MAP_SERVER_PROBE_STOPPED' in log.read_text(errors='replace'):
            break
        time.sleep(.1)
