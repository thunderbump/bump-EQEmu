"""Host transport only. Builds and executes the clean committed snapshot in rootless Docker."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--socket', required=True)
p.add_argument('--image', required=True)
p.add_argument('--inputs', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--glm', required=True, help='GLM include directory inside image')
p.add_argument('--sanitizers', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
if subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain']).strip():
    p.error('commit the reviewed source first; snapshot must be clean')
docker = ['docker', '--host', a.socket]
security = subprocess.check_output(docker + ['info', '--format', '{{json .SecurityOptions}}'], text=True)
if 'name=rootless' not in json.loads(security):
    p.error('rootless Docker required')
a.output.mkdir(parents=True, exist_ok=False)
commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
script = '''set -eu
cmake -S /work/experiments/map-translation -B /build -DGLM_INCLUDE_DIR="$GLM_INCLUDE" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAP_CAPTURE_SANITIZERS="$DIAGNOSTICS"
cmake --build /build -j2
if [ "$DIAGNOSTICS" = ON ]; then
 mkdir -p /results
 if /build/map-alignment-control > /results/alignment-control.log 2>&1; then
  echo 'alignment negative control unexpectedly passed' >&2; exit 1
 fi
 grep -q 'runtime error: load of misaligned address' /results/alignment-control.log
fi
cd /work/experiments/map-translation
MAP_CAPTURE_BINARY=/build/map-capture python3 test_capture.py
GLM_ROOT="$GLM_INCLUDE/glm" python3 run.py /build/map-capture /inputs /results/capture
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    archive = subprocess.check_output(['git', '-C', str(root), 'archive', commit])
    (tmp / 'source.tar').write_bytes(archive)
    source = tmp / 'source'; source.mkdir()
    subprocess.run(['tar', '-xf', str(tmp / 'source.tar'), '-C', str(source)], check=True)
    identity = {'commit': commit, 'tree': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD^{tree}'], text=True).strip(),
                'archive_sha256': hashlib.sha256(archive).hexdigest(), 'alignment_patch_origin': '78c07284908931297ea72e25da358965b60ee913'}
    (source / 'capture-source.json').write_text(json.dumps(identity, indent=2))
    (a.output / 'source.json').write_text(json.dumps(identity, indent=2))
    create = docker + ['create', '--network', 'none', '--memory', '2g', '--memory-swap', '2g', '--cpus', '2', '--pids-limit', '128',
                       '--entrypoint', '/bin/bash', '-e', f'GLM_INCLUDE={a.glm}', '-e', f'DIAGNOSTICS={"ON" if a.sanitizers else "OFF"}',
                       '-e', 'ASAN_OPTIONS=halt_on_error=1:detect_leaks=1', '-e', 'UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1', a.image, '-lc', script]
    cid = subprocess.check_output(create, text=True).strip()
    try:
        with (a.output / 'container.json').open('w') as out: subprocess.run(docker + ['inspect', cid], stdout=out, check=True)
        subprocess.run(docker + ['cp', str(source) + '/.', cid + ':/work'], check=True)
        subprocess.run(docker + ['cp', str(a.inputs.resolve()), cid + ':/inputs'], check=True)
        with (a.output / 'worker.log').open('w') as out:
            status = subprocess.run(docker + ['start', '-a', cid], stdout=out, stderr=subprocess.STDOUT).returncode
        copied = subprocess.run(docker + ['cp', cid + ':/results', str(a.output)], capture_output=True)
        if copied.returncode: (a.output / 'copy-error.txt').write_bytes(copied.stderr)
        # docker start exit is supplemented with the container's actual process status.
        exitcode = int(subprocess.check_output(docker + ['inspect', '--format', '{{.State.ExitCode}}', cid]))
        (a.output / 'exit.json').write_text(json.dumps({'transport_exit': status, 'worker_exit': exitcode}))
    finally:
        subprocess.run(docker + ['rm', '-f', cid], check=True)
        (a.output / 'cleanup.json').write_text(json.dumps({'container_removed': cid}))
    if status or exitcode:
        raise SystemExit(1)
