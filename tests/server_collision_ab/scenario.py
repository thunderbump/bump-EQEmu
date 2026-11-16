"""Opt-in extension to the existing fresh-DB runtime. No client or shared state."""
import json
import time
from pathlib import Path
from evidence import parse_queries


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
UPDATE npc_types SET size=6,aggroradius=0,assistradius=0,npc_faction_id=0,runspeed=0 WHERE id=2011;
UPDATE rule_values SET rule_value='true' WHERE rule_name='Map:FixZWhenPathing';
UPDATE logsys_categories SET log_to_console=1 WHERE log_category_description IN ('Fix Z','Pathing');
""")
    import shutil
    shutil.copyfile('/probe.pl', env['root'] / 'quests/qeynos2/v0/2011.pl')


def run(env):
    zone = env['start']('qeynos2', ['/binaries/zone', 'qeynos2:7001'])
    env['receipt']['executables'] = {}
    import hashlib
    for name, process in env['processes']:
        with Path('/proc', str(process.pid), 'exe').open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        env['receipt']['executables'][name] = {'pid': process.pid, 'sha256': digest}
    deadline = time.monotonic() + 90
    log = env['results'] / 'qeynos2-smoke.log'
    while time.monotonic() < deadline:
        assert all(p.poll() is None for _, p in env['processes']), 'owned child exited'
        text = log.read_text(errors='replace')
        if 'SERVER_COLLISION_QUERIES_DONE' in text:
            queries = parse_queries(text)
            env['receipt'].update(ready=True, fixed_queries_complete=True, queries=queries)
            break
        time.sleep(.2)
    else:
        raise RuntimeError('production fixed query deadline expired')
    env['receipt']['route_callers_complete'] = False
    env['receipt']['route_state'] = 'incomplete'
    env['receipt']['coverage_gap'] = (
        'No exported ordinary NPC route-node or GetZOffset observation. '
        'Fixed Perl GetGroundZ/CheckLosFN queries do not prove AdjustRoute or movement FixZ. '
        'No movement requested; no height-transition route qualified; PoK blocked.')
    # Shared cleanup expects this timer acknowledgement before terminating zone.
    (env['root'] / 'stop-map-validation').touch()
    until = time.monotonic() + 3
    while time.monotonic() < until:
        if 'MAP_SERVER_PROBE_STOPPED' in log.read_text(errors='replace'):
            break
        time.sleep(.1)
