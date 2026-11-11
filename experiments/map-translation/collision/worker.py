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
p.add_argument('--inputs', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--references', type=Path, required=True)
p.add_argument('--rust-meshes', type=Path, required=True)
p.add_argument('--sanitizers', action='store_true')
p.add_argument('--memory-gib', type=int, choices=[2,4], default=2)
p.add_argument('--baseline-traces', type=Path, required=True)
a = p.parse_args()
if a.memory_gib == 4 and not a.sanitizers:
    p.error('4 GiB is an opt-in diagnostic limit only')
root = Path(__file__).resolve().parents[3]
if subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain']).strip():
    p.error('commit the reviewed source first; snapshot must be clean')
docker = ['docker', '--host', a.socket]
security = subprocess.check_output(docker + ['info', '--format', '{{json .SecurityOptions}}'], text=True)
if 'name=rootless' not in json.loads(security):
    p.error('rootless Docker required')
a.output.mkdir(parents=True, exist_ok=False)
(a.output / 'command.json').write_text(json.dumps(sys.argv, indent=2))
reused = {}
for label, directory in [('cpp', a.references), ('rust', a.rust_meshes)]:
    base = directory.resolve().parent.parent
    reused[label] = {}
    for name in ['source.json', 'container.json']:
        data = (base / name).read_bytes()
        reused[label][name] = {'path':str(base / name), 'sha256':hashlib.sha256(data).hexdigest(), 'content':json.loads(data)}
(a.output / 'reused-provenance.json').write_text(json.dumps(reused, indent=2))
commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
script = '''set -eu
mkdir -p /results /baseline
cmake -S /work/experiments/map-translation/collision -B /build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCOLLISION_SANITIZERS="$DIAGNOSTICS"
cmake --build /build -j2
cp /build/compile_commands.json /results/
"${CXX:-c++}" --version > /results/compiler.txt
uname -a > /results/uname.txt
sha256sum /build/collision-eval /work/zone/raycast_mesh.cpp /work/zone/raycast_mesh.h /work/common/memory/ksm.cpp > /results/source-binary.sha256
ldd /build/collision-eval > /results/ldd.txt
ldd /build/collision-eval | awk '{for(i=1;i<=NF;i++) if (index($i,"/") == 1) print $i}' | xargs sha256sum > /results/libraries.sha256
python3 /work/experiments/map-translation/collision/run.py
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    archive = subprocess.check_output(['git', '-C', str(root), 'archive', commit])
    (tmp / 'source.tar').write_bytes(archive)
    source = tmp / 'source'; source.mkdir()
    subprocess.run(['tar', '-xf', str(tmp / 'source.tar'), '-C', str(source)], check=True)
    identity = {'commit': commit, 'tree': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD^{tree}'], text=True).strip(),
                'archive_sha256': hashlib.sha256(archive).hexdigest(), 'purpose': 'raycaster memory baseline; one engine per process'}
    (source / 'capture-source.json').write_text(json.dumps(identity, indent=2))
    (a.output / 'source.json').write_text(json.dumps(identity, indent=2))
    create = docker + ['create', '--network', 'none', '--memory', f'{a.memory_gib}g', '--memory-swap', f'{a.memory_gib}g', '--cpus', '2', '--pids-limit', '128',
                       '--entrypoint', '/bin/bash', '-e', f'DIAGNOSTICS={"ON" if a.sanitizers else "OFF"}',
                       '-e', 'ASAN_OPTIONS=halt_on_error=1:abort_on_error=0:detect_leaks=1:symbolize=1:allow_addr2line=1:exitcode=1', '-e', 'ASAN_SYMBOLIZER_PATH=/usr/bin/addr2line', '-e', 'UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:exitcode=1', a.image, '-lc', script]
    cid = subprocess.check_output(create, text=True).strip()
    (a.output / 'container-id.txt').write_text(cid + '\n')
    retain = True
    try:
        with (a.output / 'container.json').open('w') as out: subprocess.run(docker + ['inspect', cid], stdout=out, check=True)
        subprocess.run(docker + ['cp', str(source) + '/.', cid + ':/work'], check=True)
        subprocess.run(docker + ['cp', str(a.inputs.resolve()), cid + ':/inputs'], check=True)
        subprocess.run(docker + ['cp', str(a.references.resolve()), cid + ':/references'], check=True)
        subprocess.run(docker + ['cp', str(a.rust_meshes.resolve()), cid + ':/rust-meshes'], check=True)
        baseline = tmp / 'baseline'; baseline.mkdir()
        import shutil
        for name in ('qeynos2','poknowledge'):
            shutil.copyfile(a.baseline_traces/(name+'.trace'),baseline/(name+'.trace'))
        subprocess.run(docker + ['cp', str(baseline), cid + ':/baseline'], check=True)
        with (a.output / 'worker.log').open('w') as out:
            status = subprocess.run(docker + ['start', '-a', cid], stdout=out, stderr=subprocess.STDOUT).returncode
        copied = subprocess.run(docker + ['cp', cid + ':/results', str(a.output)], capture_output=True)
        retain = copied.returncode != 0
        if retain: (a.output / 'copy-error.txt').write_bytes(copied.stderr)
        # docker start exit is supplemented with the container's actual process status.
        exitcode = int(subprocess.check_output(docker + ['inspect', '--format', '{{.State.ExitCode}}', cid]))
        with (a.output / 'container-final.json').open('w') as out: subprocess.run(docker + ['inspect', cid], stdout=out, check=True)
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
