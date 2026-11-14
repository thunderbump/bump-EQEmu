#!/usr/bin/env python3
"""Host transport only. Run the committed preflight in an owned rootless daemon."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess as sp

IMAGE = 'sha256:93429edbf01c1631611af6c88220405136072afd78dc65b6ebf47415b54c38e1'
BASELINE = '3c2f12b8fc8e12cdc9cca7e0ab753fd22a8ad597'
BINARY_SOURCE = '4ce5d7391a048f609b037817b772d6c15ea482d6'
CLIENT_SOURCE = 'd034b7eafae6a0c9cbd4461da6a09a847d61ba4a'
CLIENT_SHA = '6271c614d1668d2966f68867e2cdab50df6b94f4c8339a37c1750c564c460505'
ZONE_SHA = {'OFF': '7a0cb456c697c3fad5c5350cf84979f23b2c7d7940399ac16fce9171a8fc5b81',
            'ON': '18025f30ef137f235e23598258100e1edf59db17ea40858075ca64876e448d90'}
STATE = Path.home() / '.local/state'
ROOT = STATE / 'eqemu-map-translation/client-collision-ab-controls-20261114'
PRIOR = ROOT.parent / 'zone-map-reload-20261111'
FIXTURE = STATE / 'eqemu-vm-proof/runtime-qeynos2-inputs-20261025/payload'
CLIENT = STATE / 'eqoxide-headless-poc/2026-11-03/eqoxide-headless-d034b7e'


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--socket', required=True)
    parser.add_argument('--run-label', required=True, help='fresh evidence name, never reused')
    parser.add_argument('--stage', choices=['preflight', 'OFF', 'ON'], default='preflight')
    args = parser.parse_args()
    if not __import__('re').fullmatch(r'[a-z0-9][a-z0-9-]{0,48}', args.run_label):
        raise SystemExit('invalid run label')
    expected_socket = 'unix:///run/user/' + str(__import__('os').getuid()) + '/eqemu-client-collision-ab-controls-20261114.sock'
    if args.socket != expected_socket:
        raise SystemExit('refusing a daemon outside this task')
    if sp.check_output(['git', 'status', '--porcelain']).strip():
        raise SystemExit('commit harness before execution')
    sp.run(['git', 'merge-base', '--is-ancestor', BASELINE, 'HEAD'], check=True)
    docker = ['docker', '--host', args.socket]
    security = json.loads(sp.check_output(docker + ['info', '--format', '{{json .SecurityOptions}}']))
    if 'name=rootless' not in security:
        raise SystemExit('rootless daemon required')
    image = json.loads(sp.check_output(docker + ['image', 'inspect', IMAGE]))[0]
    if image['Id'] != IMAGE:
        raise SystemExit('image identity mismatch')
    files = []
    mounts = []

    def pin(path, guest, expected=None):
        digest = sha(path)
        if expected and digest != expected:
            raise SystemExit('input identity mismatch: ' + str(path))
        files.append({'path': str(path), 'guest_path': guest, 'sha256': digest,
                      'bytes': path.stat().st_size})

    pin(CLIENT, '/client', CLIENT_SHA)
    mounts.append((CLIENT, '/client'))
    for backend in ('OFF', 'ON'):
        binaries = PRIOR / 'results' / backend / 'bin'
        recorded = json.loads((PRIOR / 'results' / (backend + '-new-binaries.json')).read_text())
        for binary in ('zone', 'world', 'loginserver', 'shared_memory'):
            pin(binaries / binary, '/' + backend + '/' + binary,
                ZONE_SHA[backend] if binary == 'zone' else recorded[binary])
        mounts.append((binaries, '/' + backend))
    fixture_manifest = ROOT.parent / 'rust-map-server-validation-20261111/fixture-manifest-v2.json'
    for item in json.loads(fixture_manifest.read_text())['files']:
        path = FIXTURE / item['path']
        pin(path, '/fixture/' + item['path'], item['sha256'])
    mounts.append((FIXTURE, '/fixture'))
    for name in ('qeynos2', 'poknowledge'):
        original = ROOT.parent / 'two-maps-v1/inputs' / (name + '.map')
        pin(original, '/originals/' + name + '.map',
            sha(FIXTURE / 'maps/base' / (name + '.map')))
    mounts.append((ROOT.parent / 'two-maps-v1/inputs', '/originals'))
    out = ROOT / (args.stage + '-' + args.run_label)
    out.mkdir(parents=True, mode=0o700)  # refuses overwriting first failure
    script = out / 'worker.py'
    worker = 'preflight.py' if args.stage == 'preflight' else 'world.py'
    shutil.copyfile(Path(__file__).with_name(worker), script)
    pin(script, '/worker.py')
    if args.stage != 'preflight':
        if not json.loads((ROOT.parent / 'client-collision-ab-20261114/preflight/receipt.json').read_text())['closure_ready']:
            raise SystemExit('runtime closure not qualified')
        for name in ('runtime', 'results'):
            (out / name).mkdir(mode=0o700)
        mounts.append((PRIOR / 'results' / args.stage / 'bin', '/binaries'))
        login_assets = out / 'login-assets'
        login_assets.mkdir()
        for name in ('login_schema.sql', 'login_opcodes.conf', 'login_opcodes_sod.conf'):
            data = sp.check_output(['git', 'show', BASELINE + ':loginserver/login_util/' + name])
            (login_assets / name).write_bytes(data)
            pin(login_assets / name, '/login-assets/' + name)
        mounts.append((login_assets, '/login-assets'))
        controls = Path(__file__).with_name('controls.py').resolve()
        pin(controls, '/controls.py')
        mounts.append((controls, '/controls.py'))
        probe = Path(__file__).with_name('2011.pl').resolve()
        pin(probe, '/probe.pl')
        mounts.append((probe, '/probe.pl'))
    inputs = out / 'inputs.json'
    inputs.write_text(json.dumps({'files': files, 'baseline': BASELINE,
        'harness_commit': sp.check_output(['git', 'rev-parse', 'HEAD']).decode().strip(),
        'binary_source': BINARY_SOURCE, 'client_source': CLIENT_SOURCE,
        'run_label': args.run_label, 'stage': args.stage, 'image': IMAGE, 'image_config': image['Config']}, indent=2) + '\n')
    mounts.extend([(inputs, '/inputs.json'), (script, '/worker.py')])
    cmd = docker + ['create', '--name', 'eqemu-client-collision-ab-controls-' + args.stage.lower() + '-' + args.run_label,
        '--network', 'none', '--memory', '2g', '--memory-swap', '2g',
        '--cpus', '1', '--pids-limit', '128', '--user', '0']
    for src, dst in mounts:
        cmd += ['--mount', f'type=bind,src={src},dst={dst},readonly']
    if args.stage == 'preflight':
        cmd += ['--mount', f'type=bind,src={out},dst=/evidence']
    else:
        cmd += ['--mount', f'type=bind,src={out / "runtime"},dst=/runtime',
                '--mount', f'type=bind,src={out / "results"},dst=/runtime-results',
                '--mount', f'type=bind,src={fixture_manifest},dst=/fixture-manifest.json,readonly']
    cmd += ['--entrypoint', 'python3', IMAGE, '/worker.py']
    (out / 'command.json').write_text(json.dumps(cmd, indent=2) + '\n')
    cid = sp.check_output(cmd).decode().strip()
    try:
        with (out / 'worker.log').open('wb') as log:
            result = sp.run(docker + ['start', '-a', cid], stdout=log, stderr=sp.STDOUT, timeout=420)
        (out / 'exit.json').write_text(json.dumps({'exit': result.returncode}))
    finally:
        (out / 'container.json').write_bytes(sp.check_output(docker + ['inspect', cid]))
        sp.run(docker + ['rm', '-f', cid], check=True, stdout=sp.DEVNULL)
        (out / 'cleanup.json').write_text(json.dumps({'removed': cid}) + '\n')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
