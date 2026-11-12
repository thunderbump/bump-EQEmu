"""Transport committed source to an already-running, task-owned rootless daemon.

Run from the repository root. Dependency provisioning and daemon lifecycle belong
in the run record; this helper never starts services or modifies host packages.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--socket', required=True)
parser.add_argument('--image', required=True, help='Immutable sha256 image ID')
parser.add_argument('--output', type=Path, required=True, help='New artifact directory')
parser.add_argument('--deps', type=Path, required=True, help='Prepared vcpkg/ and installed/ export')
parser.add_argument('--cargo', type=Path, required=True, help='Task-owned populated Cargo home')
args = parser.parse_args()
assert args.image.startswith('sha256:') and len(args.image) == 71
assert args.deps.is_absolute() and args.cargo.is_absolute() and args.output.is_absolute()
docker = ['docker', '--host', args.socket]
security = json.loads(subprocess.check_output(docker + ['info', '--format', '{{json .SecurityOptions}}']))
assert 'name=rootless' in security
assert not subprocess.check_output(['git', 'status', '--porcelain']), 'Commit source before capture'
args.output.mkdir(parents=True)
source = args.output / 'source'
source.mkdir()
archive = args.output / 'source.tar'
subprocess.run(['git', 'archive', '--format=tar', '-o', str(archive), 'HEAD'], check=True)
subprocess.run(['tar', '-xf', str(archive), '-C', str(source)], check=True)
assert not subprocess.check_output(['git', '-C', 'submodules/websocketpp', 'status', '--porcelain'])
shutil.copytree('submodules/websocketpp', source / 'submodules/websocketpp', dirs_exist_ok=True,
                ignore=shutil.ignore_patterns('.git'))
results = args.output / 'results'
results.mkdir()
command = docker + ['create', '--user', '0', '--network', 'none', '--memory', '2g',
                    '--memory-swap', '2g', '--cpus', '2', '--pids-limit', '256']
for path, target, readonly in [(source, '/work', True), (args.deps, '/deps', True),
                               (args.cargo, '/cargo', False), (results, '/results', False)]:
    command += ['--mount', f'type=bind,src={path},dst={target}' + (',readonly' if readonly else '')]
command += ['--entrypoint', 'python3', args.image, '/work/tests/map_server_validation/guest.py']
identity = {'source': subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode().strip(),
            'websocketpp': subprocess.check_output(['git', '-C', 'submodules/websocketpp', 'rev-parse', 'HEAD']).decode().strip(),
            'source_archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(), 'command': command}
(args.output / 'input.json').write_text(json.dumps(identity, indent=2))
container = subprocess.check_output(command).decode().strip()
try:
    with (args.output / 'worker.log').open('w') as log:
        subprocess.run(docker + ['start', '-a', container], stdout=log, stderr=subprocess.STDOUT, timeout=33000)
    (args.output / 'container-final.json').write_bytes(subprocess.check_output(docker + ['inspect', container]))
finally:
    subprocess.run(docker + ['rm', '-f', container], check=True)
    (args.output / 'cleanup.json').write_text(json.dumps({'container_removed': container}))
