#!/usr/bin/env python3
"""Standalone opt-in fixed-query survey; incomplete route evidence is fatal."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess as sp
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from evidence import compare, verdict
spec = importlib.util.spec_from_file_location('client_transport', HERE.parent/'client_collision_ab/run.py')
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)
ROOT = shared.ROOT.parent/'server-collision-scenarios-20261115'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arm', choices=['OFF', 'ON', 'OFF-repeat'], required=True)
    parser.add_argument('--label', required=True)
    args = parser.parse_args()
    import re
    if not re.fullmatch('[a-z0-9][a-z0-9-]{0,48}', args.label):
        raise SystemExit('invalid label')
    if sp.check_output(['git', 'status', '--porcelain']).strip():
        raise SystemExit('commit all harness inputs before execution')
    backend = 'ON' if args.arm == 'ON' else 'OFF'
    socket = f'unix:///run/user/{os.getuid()}/eqemu-server-collision-scenarios-20261115.sock'
    docker = ['docker', '--host', socket]
    security = json.loads(sp.check_output(docker+['info', '--format', '{{json .SecurityOptions}}']))
    if 'name=rootless' not in security:
        raise SystemExit('owned rootless daemon required')
    image = json.loads(sp.check_output(docker+['image', 'inspect', shared.IMAGE]))[0]
    assert image['Id'] == shared.IMAGE
    out = ROOT/(args.arm+'-'+args.label)
    out.mkdir(mode=0o700)  # Never overwrite an original failure.
    files, mounts = [], []

    def pin(path, guest, expected=None):
        digest = shared.sha(path)
        if expected and expected != digest:
            raise RuntimeError('input hash mismatch: '+str(path))
        files.append({'path': str(path), 'guest_path': guest, 'sha256': digest})
        mounts.append((path, guest, True))

    for binary in ('zone', 'world', 'shared_memory'):
        recorded = json.loads((shared.PRIOR/'results'/f'{backend}-new-binaries.json').read_text())
        pin(shared.PRIOR/'results'/backend/'bin'/binary, '/binaries/'+binary,
            shared.ZONE_SHA[backend] if binary == 'zone' else recorded[binary])
    fixture_manifest = ROOT.parent/'rust-map-server-validation-20261111/fixture-manifest-v2.json'
    for item in json.loads(fixture_manifest.read_text())['files']:
        digest = shared.sha(shared.FIXTURE/item['path'])
        assert digest == item['sha256']
    mounts.append((shared.FIXTURE, '/fixture', True))
    pin(fixture_manifest, '/fixture-manifest.json')
    # Base worker restores both original maps, but this extension launches only Qeynos2.
    originals = ROOT.parent/'two-maps-v1/inputs'
    for name in ('qeynos2', 'poknowledge'):
        pin(originals/(name+'.map'), '/originals/'+name+'.map',
            shared.sha(shared.FIXTURE/'maps/base'/(name+'.map')))
    for path, guest in [(HERE/'scenario.py','/scenario.py'), (HERE/'evidence.py','/evidence.py'),
                        (HERE/'2011.pl','/probe.pl'),
                        (HERE.parent/'map_server_validation/runtime.py','/worker.py')]:
        pin(path, guest)
    # Pin host transport and inherited source relationship as evidence too.
    transport = {str(p.relative_to(HERE.parent.parent)): shared.sha(p) for p in
                 (HERE/'run.py', HERE.parent/'client_collision_ab/run.py')}
    head = sp.check_output(['git','rev-parse','HEAD']).decode().strip()
    source_delta = sp.check_output(['git','diff',shared.BINARY_SOURCE,head,'--','zone','common','CMakeLists.txt'])
    changed = sp.check_output(['git','diff','--name-only',shared.BINARY_SOURCE,head,
                               '--','zone','common','CMakeLists.txt']).decode().splitlines()
    assert set(changed) == {'zone/replace_collision_map.h', 'zone/zone.cpp'}, 'production changed; rebuild both arms'
    old = sp.check_output(['git','show',shared.BINARY_SOURCE+':tests/map_reload/replace_collision_map.h'])
    assert old == Path('zone/replace_collision_map.h').read_bytes(), 'helper relocation differs'
    old_zone = sp.check_output(['git','show',shared.BINARY_SOURCE+':zone/zone.cpp'])
    assert old_zone.replace(b'tests/map_reload/replace_collision_map.h', b'zone/replace_collision_map.h') == Path('zone/zone.cpp').read_bytes()
    (out/'production-source-delta.patch').write_bytes(source_delta)
    inputs = {'arm': args.arm, 'backend': backend, 'harness_commit': head,
              'binary_source': shared.BINARY_SOURCE, 'image': shared.IMAGE,
              'files': files, 'transport': transport, 'numeric_tolerance': 0,
              'scope': 'fixed queries; route evidence required but unavailable'}
    reference = ROOT/('OFF-'+args.label)
    if args.arm != 'OFF':
        before = json.loads((reference/'inputs.json').read_text())
        # Binary differences are intentional. Everything else is frozen before ON.
        stable = lambda data: {f['guest_path']: f['sha256'] for f in data['files']
                               if not f['guest_path'].startswith('/binaries/')}
        assert stable(before) == stable(inputs) and before['transport'] == transport
        baseline = json.loads((reference/'results/smoke.json').read_text())
        assert baseline.get('fixed_queries_complete'), 'OFF positive controls required'
        assert json.loads((reference/'cleanup.json').read_text())['removed']
    (out/'inputs.json').write_text(json.dumps(inputs, indent=2)+'\n')
    for name, guest in [('runtime','/runtime'), ('results','/runtime-results')]:
        (out/name).mkdir(mode=0o700)
        mounts.append((out/name, guest, False))
    cmd = docker+['create', '--name', 'eqemu-server-collision-'+args.arm.lower()+'-'+args.label,
                  '--network','none','--memory','2g','--memory-swap','2g','--cpus','1',
                  '--pids-limit','128','--user','0','--env','EQEMU_SERVER_COLLISION_SCENARIO=1']
    for src, dst, readonly in mounts:
        cmd += ['--mount', f'type=bind,src={src},dst={dst}'+(',readonly' if readonly else '')]
    cmd += ['--entrypoint','python3',shared.IMAGE,'/worker.py']
    (out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
    cid = sp.check_output(cmd).decode().strip()
    try:
        with (out/'worker.log').open('wb') as log:
            result = sp.run(docker+['start','-a',cid],stdout=log,stderr=sp.STDOUT,timeout=420)
        (out/'exit.json').write_text(json.dumps({'exit': result.returncode}))
    finally:
        (out/'container.json').write_bytes(sp.check_output(docker+['inspect',cid]))
        sp.run(docker+['rm','-f',cid],check=True,stdout=sp.DEVNULL)
        assert not sp.check_output(docker+['ps','-aq','--filter','id='+cid]).strip()
        (out/'cleanup.json').write_text(json.dumps({'removed':cid}))
    receipt = json.loads((out/'results/smoke.json').read_text())
    phases = verdict(receipt)
    if args.arm != 'OFF':
        phases['fixed_query_parity'] = compare(baseline['queries'], receipt['queries'])
    (out/'verdict.json').write_text(json.dumps(phases,indent=2)+'\n')
    return 0 if result.returncode == 0 and all(phases.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
