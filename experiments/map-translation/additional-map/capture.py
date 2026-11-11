"""Worker-only one-map capture. Counts come from outputs, never census predictions."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent), str(HERE.parent / 'rust')]
from mesh import compare, read
from diagnose import diagnose


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def main():
    out = Path('/results/capture'); out.mkdir(exist_ok=False)
    item = json.loads((HERE / 'manifest.json').read_text())['map']
    source = Path('/inputs') / Path(item['path']).name
    assert source.stat().st_size == item['bytes'] and sha(source) == item['sha256']
    results = []
    binaries = {'cpp': Path('/build/map-capture'), 'rust': Path('/rust-build/release/map-poc')}
    provenance = {'schema': 'EQMESH01 v1', 'input_sha256': sha(source),
                  'fp': {'RUSTFLAGS': os.getenv('RUSTFLAGS'), 'CARGO_ENCODED_RUSTFLAGS': os.getenv('CARGO_ENCODED_RUSTFLAGS'),
                         'cpp': '-fno-fast-math -ffp-contract=off', 'rust': 'release default target CPU, no mul_add; default process FP environment'},
                  'cpp_runtime': json.loads(subprocess.check_output([str(binaries['cpp']), '--runtime-info'])),
                  'compile_commands': json.loads(Path('/build/compile_commands.json').read_text()),
                  'source': json.loads(Path('/work/capture-source.json').read_text()), 'binaries': {}}
    glm = Path(os.environ['GLM_INCLUDE']) / 'glm'
    provenance['glm_headers'] = {str(p.relative_to(glm)): sha(p) for p in sorted(glm.rglob('*')) if p.is_file()}
    assert provenance['glm_headers']
    for name, binary in binaries.items():
        linked = subprocess.check_output(['ldd', str(binary)], text=True)
        provenance['binaries'][name] = {'sha256': sha(binary), 'ldd': linked,
            'libraries': {word: sha(word) for word in linked.split() if word.startswith('/') and Path(word).is_file()}}
    write(out / 'provenance.json', provenance)
    failures = []
    for name, binary in binaries.items():
        for n in range(2):
            target = out / f'{source.stem}.{name}.{n}.mesh'
            usage = out / f'{name}.{n}.time'
            command = ['/usr/bin/time', '-o', str(usage), '-f', '%M %U %S', str(binary), str(source), str(target), item['sha256']]
            start = time.monotonic()
            with (out / f'{name}.{n}.stdout').open('w') as stdout, (out / f'{name}.{n}.stderr').open('w') as stderr:
                process = subprocess.run(command, stdout=stdout, stderr=stderr)
            result = {'implementation': name, 'run': n, 'command': command, 'exit': process.returncode,
                      'elapsed_seconds': time.monotonic() - start, 'time_raw': usage.read_text()}
            if target.exists():
                result.update(bytes=target.stat().st_size, sha256=sha(target))
            if process.returncode:
                failures.append(f'{name}.{n} exit {process.returncode}')
            else:
                identity, vertices, indices = read(target)
                assert identity.decode() == item['sha256']
                result.update(vertices=len(vertices)//3, indices=len(indices))
            results.append(result)
    write(out / 'results.json', results)
    for name in binaries:
        if all(r['exit'] == 0 for r in results if r['implementation'] == name):
            try:
                payload = compare(out / f'{source.stem}.{name}.0.mesh', out / f'{source.stem}.{name}.1.mesh')
                write(out / f'{name}.repeat.json', {'exact': True, 'payload_sha256': payload})
            except ValueError as error:
                write(out / f'{name}.repeat.json', {'exact': False, 'error': str(error)})
                failures.append(f'{name} repeat mismatch')
    if not failures:
        ref, candidate = (out / f'{source.stem}.{name}.0.mesh' for name in binaries)
        strict = subprocess.run([sys.executable, str(HERE.parent / 'mesh.py'), str(ref), str(candidate)], capture_output=True, text=True)
        write(out / 'strict.json', {'exit': strict.returncode, 'stdout': strict.stdout, 'stderr': strict.stderr})
        try:
            write(out / 'numeric.json', diagnose(ref, candidate))
        except ValueError as error:
            write(out / 'numeric.json', {'structural_error': str(error)})
        if strict.returncode:
            failures.append('strict parity mismatch')
    write(out / 'summary.json', {'failures': failures, 'capture_runs': len(results)})
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
