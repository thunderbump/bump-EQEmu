#!/usr/bin/env python3
"""Fresh public-fixture smoke, executed only inside the disposable container."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess as sp
import tarfile
import time
import zipfile

root = Path('/runtime')
fixture = Path('/fixture')
results = Path('/runtime-results')
processes = []

def command(args, **kwargs):
    return sp.run(args, check=True, timeout=300, **kwargs)

def query(sql):
    command(['mariadb', '--no-defaults', '--socket='+str(root/'mysql.sock'), '-uroot'], input=sql.encode())

def start(name, args):
    log = (results/(name+'-smoke.log')).open('wb')
    try:
        p = sp.Popen(args, cwd=root, stdout=log, stderr=sp.STDOUT)
    finally:
        log.close()
    processes.append((name, p))
    return p

def socket_inodes(pid):
    result = set()
    for fd in Path('/proc', str(pid), 'fd').iterdir():
        try:
            target = os.readlink(fd)
        except FileNotFoundError:
            continue
        if target.startswith('socket:['):
            result.add(target[8:-1])
    return result


def world_connected(world, zone):
    """Require both ends of an established loopback connection owned by our PIDs."""
    world_inodes, zone_inodes = socket_inodes(world), socket_inodes(zone)
    rows = [line.split() for line in Path('/proc/net/tcp').read_text().splitlines()[1:]]
    for row in rows:
        if (row[3] == '01' and row[9] in zone_inodes
                and row[2] == '0100007F:2328' and row[1].startswith('0100007F:')):
            if any(peer[3] == '01' and peer[9] in world_inodes
                   and peer[1] == row[2] and peer[2] == row[1] for peer in rows):
                return True
    return False


receipt = {'ready': False, 'cleanup': False}
try:
    manifest = json.loads((fixture.parent/"fixture-manifest.json").read_text())
    for item in manifest["files"]:
        path = fixture/item["path"]
        assert path.stat().st_size == item["bytes"]
        assert hashlib.file_digest(path.open("rb"), "sha256").hexdigest() == item["sha256"]
    root.mkdir(exist_ok=True)
    command(['mariadb-install-db', '--no-defaults', '--user=root', '--datadir='+str(root/'db'), '--skip-test-db'])
    db = start('database', ['mariadbd', '--no-defaults', '--user=root', '--datadir='+str(root/'db'), '--socket='+str(root/'mysql.sock'), '--pid-file='+str(root/'mysql.pid'), '--bind-address=127.0.0.1', '--innodb-buffer-pool-size=256M'])
    for _ in range(100):
        if (root/'mysql.sock').exists():
            break
        assert db.poll() is None
        time.sleep(.2)
    query('CREATE DATABASE smoke; CREATE USER smoke@127.0.0.1; GRANT ALL ON smoke.* TO smoke@127.0.0.1;')
    contract = json.loads((fixture/'runtime-input-contract.json').read_text())
    with zipfile.ZipFile(fixture/'database/peq-1787356814.zip') as archive:
        for member in contract['database']['import_members']:
            with archive.open(member) as src, (root/'import.sql').open('wb') as out:
                shutil.copyfileobj(src, out)
            with (root/'import.sql').open('rb') as src:
                command(['mariadb', '--no-defaults', '--socket='+str(root/'mysql.sock'), '-uroot', 'smoke'], stdin=src)
    checked = command(['mariadb','--no-defaults','--socket='+str(root/'mysql.sock'),'-uroot','--batch','--skip-column-names','smoke'], input=b'SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE(); SELECT version,bots_version,custom_version FROM db_version;', capture_output=True).stdout.decode().strip()
    assert checked == '221\n9328\t0\t0', 'public fixture schema/version mismatch'
    receipt['imported_schema'] = checked
    query("USE smoke; DELETE FROM spawn2 WHERE zone='poknowledge'; DELETE FROM spawn2 WHERE zone='qeynos2' AND id<>10863; UPDATE zone SET idle_when_empty=0 WHERE zoneidnumber IN (2,202) AND version=0; UPDATE logsys_categories SET log_to_console=1 WHERE log_category_description IN ('Quest Debug','Quests','QuestErrors'); UPDATE rule_values SET rule_value='false' WHERE rule_name IN ('Zone:UseZoneController','Bots:Enabled','Analytics:CrashReporting','Zone:StateSavingOnShutdown');")
    for name in ('opcodes',):
        shutil.copytree(fixture/name, root/name)
    (root/'maps/base').mkdir(parents=True)
    for p in Path('/originals').glob('*.map'):
        shutil.copyfile(p,root/'maps/base'/p.name)
    command(['tar', 'xzf', str(fixture/'quests/quests-b3e34b8.tar.gz'), '-C', str(root), '--no-same-owner'])
    next(root.glob('projecteqquests-*')).rename(root/'quests')
    # Fixture-only ground query through the existing public Rat's Perl API.
    (root/'quests/qeynos2/v0').mkdir(exist_ok=True)
    (root/'quests/qeynos2/v0/2011.lua').unlink(missing_ok=True)
    (root/'quests/qeynos2/v0/2011.pl').write_text('sub EVENT_SPAWN { quest::settimer("map_validation",1); }\nsub EVENT_TIMER { if ($timer eq "map_validation") { $|=1; my $z=$npc->FindGroundZ(-935,1194,100); quest::debug("MAP_SERVER_QUERY z=$z"); } }\n')
    for name in ('shared','logs'):
        (root/name).mkdir()
    config = {'server': {'auto_database_updates':'false', 'world': {'shortname':'smoke','longname':'Disposable smoke','address':'127.0.0.1','localaddress':'127.0.0.1','key':'disposable-smoke','tcp':{'ip':'127.0.0.1','port':'9000'},'telnet':{'ip':'127.0.0.1','port':'9001','enabled':'true'},'loginserver':{'host':'127.0.0.1','port':'5998'}}, 'database':{'host':'127.0.0.1','port':'3306','username':'smoke','password':'','db':'smoke'}, 'directories':{'quests':'quests','plugins':'quests/plugins','lua_modules':'quests/lua_modules','maps':'maps','patches':'opcodes/','opcodes':'opcodes/','shared_memory':'shared/','logs':'logs/'}}}
    (root/'eqemu_config.json').write_text(json.dumps(config))
    command(['/binaries/shared_memory'], cwd=root)
    world = start('world', ['/binaries/world'])
    zone = start('poknowledge', ['/binaries/zone','poknowledge','7000'])
    north = start('qeynos2', ['/binaries/zone','qeynos2','7001'])
    successes = 0
    deadline = time.monotonic()+120
    while time.monotonic() < deadline:
        assert world.poll() is None and zone.poll() is None and north.poll() is None
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
            sock.settimeout(.25)
            challenge = os.urandom(4)
            sock.sendto(b'\x00\x01'+struct.pack('!I',2)+challenge+struct.pack('!I',512), ('127.0.0.1',7000))
            try:
                response = sock.recv(512)
                successes += int(response[:6] == b'\x00\x02'+challenge)
            except (TimeoutError, ConnectionRefusedError):
                pass
        if successes >= 6 and world_connected(world.pid, zone.pid) and world_connected(world.pid,north.pid) and all(('Loaded V2 Map File' in (results/(n+'-smoke.log')).read_text(errors='replace')) for n in ['poknowledge','qeynos2']):
            receipt['ready'] = True
            break
        time.sleep(1)
    assert receipt['ready'], 'two-zone map/session readiness missing'
    # The localhost world console authorizes this isolated namespace only.
    with socket.create_connection(('127.0.0.1',9001),timeout=10) as console:
        console.settimeout(2)
        console.recv(4096)
        console.sendall(b'api reload maps\r\n')
        time.sleep(10)
        try: (results/'reload-console.log').write_bytes(console.recv(16384))
        except TimeoutError: pass
    receipt['successful_reload'] = all((results/(n+'-smoke.log')).read_text(errors='replace').count('Loaded V2 Map File') + (results/(n+'-smoke.log')).read_text(errors='replace').count('Loaded .MMF Map File') >= 2 for n in ['poknowledge','qeynos2'])
    assert receipt['successful_reload'], 'reload evidence missing'
    def ground_values():
        import re
        return [float(v) for v in re.findall(r'MAP_SERVER_QUERY z=([-+0-9.eE]+)', (results/'qeynos2-smoke.log').read_text(errors='replace'))]
    initial = ground_values()
    assert initial and initial[-1] != -99999, 'fixture ground query unavailable'
    receipt['ground_before_failure'] = initial[-1]
    def reload_maps():
        with socket.create_connection(('127.0.0.1',9001),timeout=10) as c:
            c.recv(4096)
            c.sendall(b'api reload maps\r\n')
            time.sleep(10)
    victim=root/'maps/base/qeynos2.map'
    victim.rename(victim.with_suffix('.held'))
    try:
        reload_maps()
        failed = ground_values()
        assert len(failed)>len(initial), 'no ground query after failed reload'
        receipt['ground_after_failed_reload'] = failed[-1]
        receipt['failed_reload_engine_retention'] = failed[-1] == initial[-1]
    finally:
        victim.with_suffix('.held').rename(victim)
        reload_maps()
    receipt['ground_after_recovery'] = ground_values()[-1]
    assert receipt['ground_after_recovery'] == initial[-1], 'restored map query did not recover'


finally:
    exits = {}
    for name,p in reversed(processes):
        p.terminate()
        try:
            exits[name] = p.wait(timeout=30)
        except sp.TimeoutExpired:
            p.kill()
            exits[name] = p.wait()
    receipt['exits'] = exits
    receipt['cleanup'] = all(rc == 0 for rc in exits.values())
    (results/'smoke.json').write_text(json.dumps(receipt))
assert receipt['cleanup'], receipt

assert receipt.get('failed_reload_engine_retention'), 'failed reload did not retain query engine'
