"""Host transport only. Builds and executes the clean committed snapshot in rootless Docker."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--socket', required=True)
p.add_argument('--image', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--rust-meshes', type=Path, required=True)
p.add_argument('--references', type=Path, required=True)
a = p.parse_args()
if not a.image.startswith("sha256:") or len(a.image)!=71:
    p.error("immutable sha256 image ID required")
root = Path(__file__).resolve().parents[3]
if subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain']).strip():
    p.error('commit the reviewed source first; snapshot must be clean')
docker = ['docker', '--host', a.socket]
security = subprocess.check_output(docker + ['info', '--format', '{{json .SecurityOptions}}'], text=True)
if 'name=rootless' not in json.loads(security):
    p.error('rootless Docker required')
a.output.mkdir(parents=True, exist_ok=False)
(a.output / "command.json").write_text(json.dumps(sys.argv,indent=2))
commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
script = '''set -eu
mkdir -p /results
cd /work/experiments/map-translation/rust-raycaster
export CARGO_TARGET_DIR=/rust-build
rustc -Vv > /results/rustc.txt
cargo tree --frozen --offline > /results/dependencies.txt
cargo fmt --check
/usr/bin/time -v -o /results/build.time cargo build --release --frozen --offline -vv
cargo test --release --frozen --offline > /results/tests.txt 2>&1
sha256sum /rust-build/release/rust-raycaster-poc > /results/binary.sha256
ldd /rust-build/release/rust-raycaster-poc > /results/ldd.txt
ldd /rust-build/release/rust-raycaster-poc | awk '{for(i=1;i<=NF;i++) if (index($i,"/") == 1) print $i}' | xargs sha256sum > /results/libraries.sha256
printf '%s\n' "RUSTFLAGS=${RUSTFLAGS-}" "CARGO_ENCODED_RUSTFLAGS=${CARGO_ENCODED_RUSTFLAGS-}" 'release opt-level=3 default CPU; no fast-math or mul_add; default process FP environment' > /results/fp.txt
cp Cargo.lock /results/
cmake -S . -B /reference-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
/usr/bin/time -v -o /results/reference-build.time cmake --build /reference-build -j2
cp /reference-build/compile_commands.json /results/
c++ --version > /results/compiler.txt
sha256sum /reference-build/raycaster-reference > /results/reference-binary.sha256
ldd /reference-build/raycaster-reference > /results/reference-ldd.txt
ldd /reference-build/raycaster-reference | awk '{for(i=1;i<=NF;i++) if (index($i,"/") == 1) print $i}' | xargs sha256sum > /results/reference-libraries.sha256
cp /rust-build/release/rust-raycaster-poc /results/
cp /reference-build/raycaster-reference /results/
python3 qualify.py
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    archive = subprocess.check_output(['git', '-C', str(root), 'archive', commit])
    (tmp / 'source.tar').write_bytes(archive)
    (a.output / 'source.tar').write_bytes(archive)
    source = tmp / 'source'; source.mkdir()
    subprocess.run(['tar', '-xf', str(tmp / 'source.tar'), '-C', str(source)], check=True)
    identity = {'commit': commit, 'tree': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD^{tree}'], text=True).strip(),
                'archive_sha256': hashlib.sha256(archive).hexdigest(), 'baseline': 'f2cd85d4161bf5d75bab54e2ab5a0e6c7bc303f2'}
    (source / 'capture-source.json').write_text(json.dumps(identity, indent=2))
    (a.output / 'source.json').write_text(json.dumps(identity, indent=2))
    create = docker + ['create', '--pull', 'never', '--network', 'none', '--memory', '2g', '--memory-swap', '2g', '--cpus', '2', '--pids-limit', '128',
                       '--entrypoint', '/bin/bash', a.image, '-lc', script]
    cid = subprocess.check_output(create, text=True).strip()
    (a.output / 'container-id.txt').write_text(cid + '\n')
    retain = True
    try:
        with (a.output / 'container.json').open('w') as out: subprocess.run(docker + ['inspect', cid], stdout=out, check=True)
        subprocess.run(docker + ['cp', str(source) + '/.', cid + ':/work'], check=True)
        subprocess.run(docker + ['cp', str(a.baseline.resolve()), cid + ':/baseline'], check=True)
        subprocess.run(docker + ['cp', str(a.rust_meshes.resolve()), cid + ':/rust-meshes'], check=True)
        subprocess.run(docker + ['cp', str(a.references.resolve()), cid + ':/references'], check=True)
        with (a.output / 'worker.log').open('w') as out:
            status = subprocess.run(docker + ['start', '-a', cid], stdout=out, stderr=subprocess.STDOUT).returncode
        copied = subprocess.run(docker + ['cp', cid + ':/results', str(a.output)], capture_output=True)
        retain = copied.returncode != 0
        if retain: (a.output / 'copy-error.txt').write_bytes(copied.stderr)
        with (a.output / 'container-final.json').open('w') as out: subprocess.run(docker + ['inspect', cid], stdout=out, check=True)
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
