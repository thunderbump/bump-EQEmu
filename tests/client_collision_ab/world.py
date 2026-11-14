#!/usr/bin/env python3
"""Fresh public-fixture smoke, executed only inside the disposable container."""
import hashlib
import json
import os
import resource
from pathlib import Path
import shutil
import socket
import struct
import subprocess as sp
import tarfile
import time
import zipfile
import urllib.request
import re
import secrets

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


def memory():
    cg=Path('/sys/fs/cgroup')
    return {n:(cg/n).read_text().strip() for n in ['memory.max','memory.swap.max','memory.peak','memory.events']}

receipt = {'ready': False, 'cleanup': False, 'memory_before':memory()}
started=time.monotonic()
try:
    for item in json.loads(Path('/inputs.json').read_text())['files']:
        with Path(item['guest_path']).open('rb') as stream:
            assert hashlib.file_digest(stream, 'sha256').hexdigest() == item['sha256'], 'input identity mismatch'
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
    query("USE smoke; DELETE FROM spawn2 WHERE zone='poknowledge'; DELETE FROM spawn2 WHERE zone='qeynos2' AND id<>10863; UPDATE zone SET idle_when_empty=0 WHERE zoneidnumber IN (2,202) AND version=0; UPDATE rule_values SET rule_value='false' WHERE rule_name IN ('Zone:UseZoneController','Bots:Enabled','Analytics:CrashReporting','Zone:StateSavingOnShutdown');")
    for name in ('opcodes',):
        shutil.copytree(fixture/name, root/name)
    shutil.copytree(fixture/'maps',root/'maps')
    for p in Path('/originals').glob('*.map'):
        shutil.copyfile(p,root/'maps/base'/p.name)
    command(['tar', 'xzf', str(fixture/'quests/quests-b3e34b8.tar.gz'), '-C', str(root), '--no-same-owner'])
    next(root.glob('projecteqquests-*')).rename(root/'quests')
    if True:
        query("USE smoke; UPDATE logsys_categories SET log_to_console=1 WHERE log_category_description IN ('Quest Debug','Quests','QuestErrors');")
        # Fixture-only ground query through the existing public Rat's Perl API.
        (root/'quests/qeynos2/v0').mkdir(exist_ok=True)
        (root/'quests/qeynos2/v0/2011.lua').unlink(missing_ok=True)
        shutil.copyfile('/probe.pl', root/'quests/qeynos2/v0/2011.pl')
    for name in ('shared','logs'):
        (root/name).mkdir()
    config = {'server': {'auto_database_updates':'false', 'world': {'shortname':'clientab','longname':'Client collision AB private','address':'127.0.0.1','localaddress':'127.0.0.1','key':'client-ab-private','tcp':{'ip':'127.0.0.1','port':'9000'},'telnet':{'ip':'127.0.0.1','port':'9001','enabled':'true'},'loginserver':{'host':'127.0.0.1','port':'5998'}}, 'database':{'host':'127.0.0.1','port':'3306','username':'smoke','password':'','db':'smoke'}, 'directories':{'quests':'quests','plugins':'quests/plugins','lua_modules':'quests/lua_modules','maps':'maps','patches':'opcodes/','opcodes':'opcodes/','shared_memory':'shared/','logs':'logs/'}}}
    (root/'eqemu_config.json').write_text(json.dumps(config))
    os.umask(0o077)
    # Public source-owned login schema goes only into this fresh private DB.
    query('USE smoke; ' + Path('/login-assets/login_schema.sql').read_text())
    # Pre-register the private world so the normal existing-registration path
    # assigns a nonzero advertised ID. Preserve first auto-registration failure.
    query("USE smoke; INSERT INTO login_world_servers (id,long_name,short_name,login_server_list_type_id,login_server_admin_id,is_server_trusted) VALUES (1,'Client collision AB private','clientab',3,0,0);")
    for name in ('login_opcodes.conf', 'login_opcodes_sod.conf'):
        shutil.copyfile(Path('/login-assets', name), root/name)
    login_config = {
        'database': {'host':'127.0.0.1','port':3306,'db':'smoke','user':'smoke','password':''},
        'account': {'auto_create_accounts': True},
        'worldservers': {'unregistered_allowed':True, 'reject_duplicate_servers':True},
        'web_api': {'enabled':False},
        'security': {'mode':14,'allow_password_login':True,'allow_token_login':False},
        'logging': {'trace':False,'world_trace':False,'dump_packets_in':False,'dump_packets_out':False},
        'client_configuration': {'titanium_port':5998,'sod_port':5999,
            'titanium_opcodes':'login_opcodes.conf','sod_opcodes':'login_opcodes_sod.conf'}}
    (root/'login.json').write_text(json.dumps(login_config))
    command(['/binaries/shared_memory'], cwd=root)
    login = start('login', ['/binaries/loginserver'])
    world = start('world', ['/binaries/world'])
    north = start('qeynos2', ['/binaries/zone','qeynos2:7001'])
    receipt['executables'] = {}
    for name, process in processes:
        executable = Path('/proc', str(process.pid), 'exe')
        with executable.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        receipt['executables'][name] = {'pid':process.pid,'sha256':digest}
    # Wait for actual Server and production quest output, not a port-only probe.
    deadline = time.monotonic()+90
    while time.monotonic()<deadline:
        assert all(p.poll() is None for _,p in processes), 'server exited during startup'
        if 'CLIENT_AB_QUERY' in (results/'qeynos2-smoke.log').read_text(errors='replace'):
            break
        time.sleep(.5)
    else:
        raise RuntimeError('no production ground/LoS diagnostic before deadline')
    profile = {'login': {'login_host':'127.0.0.1','login_port':5999,'world_port':9000,
        'username':'clientab','password':secrets.token_hex(24),'character_name':'Clientab',
        'character_create': {'race':1,'class':7,'gender':0,'deity':396,'start_zone':2,
            'str':80,'sta':80,'agi':105,'dex':85,'wis':75,'int':75,'cha':75}},
        'expected_world': {'name':'Client collision AB private','host':'127.0.0.1'},
        'api_port':18766,'control_token':secrets.token_hex(32),'maps_dir':str(root/'maps/base')}
    profile_path = root/'client-profile.json'
    profile_path.write_text(json.dumps(profile))
    profile_path.chmod(0o600)
    receipt['identity'] = {'world':profile['expected_world'],'zone':'qeynos2',
        'account':'clientab','character':'Clientab','api_port':18766,
        'profile_path':str(profile_path),'profile_mode':'0600','host_publication':False}
    client = start('client', ['/client','--profile',str(profile_path)])
    receipt['client_sha256'] = hashlib.file_digest(Path('/proc',str(client.pid),'exe').open('rb'),'sha256').hexdigest()
    deadline = time.monotonic()+90
    receipt['client_login_attempted'] = True
    last = None
    while time.monotonic()<deadline:
        assert all(p.poll() is None for _,p in processes), 'owned process exited during client login'
        try:
            request = urllib.request.Request('http://127.0.0.1:18766/v2/player/state',
                headers={'Authorization':'Bearer '+profile['control_token']})
            with urllib.request.urlopen(request, timeout=2) as response:
                last = json.load(response)
            (results/'client-state.json').write_text(json.dumps(last,indent=2))
            # API state has a common envelope; inspect only explicit server zone
            # and geometry fields, never infer readiness from an open HTTP socket.
            state = last.get('state', last)
            if state.get('phase') == 'failed':
                raise RuntimeError('ordinary client session reported failed; inspect private state')
            if state.get('phase') == 'ready':
                assert state['zone']['name']=='qeynos2', 'unexpected isolated zone'
                assert state['geometry']['digest']=='040343c54e3b02bfd1637ce8fca8839dd676e17c41fb24c042ccfa0bd649435c', 'unexpected geometry'
                receipt['ready'] = True
                break
        except (OSError, TimeoutError):
            pass
        time.sleep(.5)
    assert receipt['ready'], 'ordinary login/world/zone readiness not reached within 90 seconds'
    receipt['movement_commands_sent'] = 0
except Exception as error:
    receipt['error'] = str(error)
finally:
    (root/'stop-client-collision-ab').touch()
    time.sleep(2)
    receipt['exits'] = {}
    for name, process in reversed(processes):
        if process.poll() is None:
            process.terminate()
        try:
            code = process.wait(timeout=20)
        except sp.TimeoutExpired:
            process.kill()
            code = process.wait(timeout=5)
        receipt['exits'][name] = code
    log = results/'qeynos2-smoke.log'
    if log.exists():
        queries = [line for line in log.read_text(errors='replace').splitlines() if 'CLIENT_AB_QUERY' in line]
        (results/'server-queries.log').write_text('\n'.join(queries)+'\n')
        receipt['direct_query_records'] = len(queries)
    receipt['cleanup'] = all(p.poll() is not None for _, p in processes)
    receipt['orderly_exit'] = all(code==0 for code in receipt['exits'].values())
    receipt['elapsed_s'] = time.monotonic()-started
    receipt['memory_after'] = memory()
    (results/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(0 if receipt['ready'] and receipt['cleanup'] and receipt['orderly_exit'] else 1)
