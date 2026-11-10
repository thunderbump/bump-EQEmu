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
p.add_argument('--references', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[3]
if subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain']).strip():
    p.error('commit the reviewed source first; snapshot must be clean')
docker = ['docker', '--host', a.socket]
security = subprocess.check_output(docker + ['info', '--format', '{{json .SecurityOptions}}'], text=True)
if 'name=rootless' not in json.loads(security):
    p.error('rootless Docker required')
a.output.mkdir(parents=True, exist_ok=False)
commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
script = '''set -eu
mkdir -p /results
cd /work/experiments/map-translation/rust
export CARGO_TARGET_DIR=/rust-build
rustc -Vv > /results/rustc.txt
cargo tree --locked --offline > /results/dependencies.txt
cargo fmt --check
cargo build --release --frozen --offline -vv
cargo test --release --frozen --offline
MAP_CAPTURE_BINARY=/rust-build/release/map-poc python3 test_poc.py
cmake -S /work/experiments/map-translation -B /build -DGLM_INCLUDE_DIR="$GLM_INCLUDE" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /build -j2
cd /work/experiments/map-translation
GLM_ROOT="$GLM_INCLUDE/glm" python3 run.py /build/map-capture /inputs /results/cpp-timing
cd rust
python3 qualify.py /rust-build/release/map-poc /inputs /references /results/rust
sha256sum /rust-build/release/map-poc > /results/binary.sha256
ldd /rust-build/release/map-poc > /results/ldd.txt
ldd /rust-build/release/map-poc | awk '{for(i=1;i<=NF;i++) if (index($i,"/") == 1) print $i}' | xargs sha256sum > /results/shared-libraries.sha256
printf '%s\n' "RUSTFLAGS=${RUSTFLAGS-}" "CARGO_ENCODED_RUSTFLAGS=${CARGO_ENCODED_RUSTFLAGS-}" 'Rust release default target CPU; no fast-math, no mul_add, default process FP environment' > /results/fp.txt
cp Cargo.lock /results/
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
                       '--entrypoint', '/bin/bash', '-e', f'GLM_INCLUDE={a.glm}', 
                       '-e', 'ASAN_OPTIONS=halt_on_error=1:detect_leaks=1', '-e', 'UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1', a.image, '-lc', script]
    cid = subprocess.check_output(create, text=True).strip()
    (a.output / 'container-id.txt').write_text(cid + '\n')
    retain = True
    try:
        with (a.output / 'container.json').open('w') as out: subprocess.run(docker + ['inspect', cid], stdout=out, check=True)
        subprocess.run(docker + ['cp', str(source) + '/.', cid + ':/work'], check=True)
        subprocess.run(docker + ['cp', str(a.inputs.resolve()), cid + ':/inputs'], check=True)
        subprocess.run(docker + ['cp', str(a.references.resolve()), cid + ':/references'], check=True)
        with (a.output / 'worker.log').open('w') as out:
            status = subprocess.run(docker + ['start', '-a', cid], stdout=out, stderr=subprocess.STDOUT).returncode
        copied = subprocess.run(docker + ['cp', cid + ':/results', str(a.output)], capture_output=True)
        retain = copied.returncode != 0
        if retain: (a.output / 'copy-error.txt').write_bytes(copied.stderr)
        # docker start exit is supplemented with the container's actual process status.
        exitcode = int(subprocess.check_output(docker + ['inspect', '--format', '{{.State.ExitCode}}', cid]))
        (a.output / 'exit.json').write_text(json.dumps({'transport_exit': status, 'worker_exit': exitcode}))
    finally:
        if retain:
            subprocess.run(docker + ['stop', '--time', '10', cid], check=False)
            (a.output / 'cleanup.json').write_text(json.dumps({'container_retained_for_recovery': cid}))
        else:
            subprocess.run(docker + ['rm', '-f', cid], check=True)
            (a.output / 'cleanup.json').write_text(json.dumps({'container_removed': cid}))
    if status or exitcode or retain:
        raise SystemExit(1)
