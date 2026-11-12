"""Ordinary full Server qualification, executed only in a disposable worker."""
import hashlib
import json
import os
import resource
from pathlib import Path
import signal
import subprocess
import time

OUT = Path('/results')
RUNS = []


def memory():
    return {name: (Path('/sys/fs/cgroup') / name).read_text().strip()
            for name in ('memory.max', 'memory.swap.max', 'memory.peak', 'memory.events')}


def run(name, args, timeout=1800):
    started = time.monotonic()
    before = memory()
    with (OUT / (name + '.log')).open('w') as log:
        child = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            while child.poll() is None:
                if time.monotonic() - started > timeout:
                    raise subprocess.TimeoutExpired(args, timeout)
                with (OUT / (name + '.resources.jsonl')).open('a') as samples:
                    rss = {}
                    for status in Path('/proc').glob('[0-9]*/status'):
                        try:
                            rss[status.parent.name] = [line for line in status.read_text().splitlines()
                                                       if line.startswith(('Name:', 'VmRSS:', 'VmHWM:'))]
                        except (FileNotFoundError, ProcessLookupError):
                            pass
                    samples.write(json.dumps({'seconds': time.monotonic()-started,
                                              'memory': memory(), 'processes': rss}) + '\n')
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    pass
            code = child.returncode
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait()
            code = 'timeout'
    RUNS.append(dict(name=name, args=args, exit=code, seconds=time.monotonic()-started,
                     before=before, after=memory(), cumulative_child_maxrss_kib=resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss))
    (OUT / 'runs.json').write_text(json.dumps(RUNS, indent=2))
    return code == 0


assert memory()['memory.swap.max'] == '0'
os.environ.update(CARGO_HOME='/cargo', CARGO_BUILD_JOBS='1', CARGO_NET_OFFLINE='true',
                  CCACHE_DISABLE='1', RUSTUP_AUTO_INSTALL='0')
for name, args in [('compiler', ['clang++', '--version']), ('cmake', ['cmake', '--version']),
                   ('rust', ['rustc', '-Vv']), ('packages', ['dpkg-query', '-W'])]:
    assert run(name, args)
for mode in ('OFF', 'ON'):
    build = OUT / mode
    os.environ['CARGO_TARGET_DIR'] = str(OUT / ('cargo-' + mode))
    flags = ['cmake', '-S', '/work', '-B', str(build), '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
             '-DCMAKE_C_COMPILER=/usr/bin/clang', '-DCMAKE_CXX_COMPILER=/usr/bin/clang++',
             '-DCMAKE_TOOLCHAIN_FILE=/deps/vcpkg/scripts/buildsystems/vcpkg.cmake',
             '-DVCPKG_MANIFEST_INSTALL=OFF', '-DVCPKG_INSTALLED_DIR=/deps/installed',
             '-DVCPKG_TARGET_TRIPLET=x64-linux', '-DVCPKG_HOST_TRIPLET=x64-linux',
             '-DEQEMU_SANITIZERS=OFF', '-DEQEMU_RUST_MAP_RAYCASTER=' + mode,
             '-DPERL_EXECUTABLE=/opt/eqemu-perl/bin/perl',
             '-DPERL_LIBRARY=/opt/eqemu-perl/lib/5.32.1/x86_64-linux-thread-multi/CORE/libperl.so',
             '-DPERL_INCLUDE_PATH=/opt/eqemu-perl/lib/5.32.1/x86_64-linux-thread-multi/CORE']
    flags += ['-DEQEMU_BUILD_' + name + '=ON' for name in
              ('SERVER', 'TESTS', 'LOGIN', 'LUA', 'PERL', 'CLIENT_FILES')]
    if not run(mode + '-configure', flags):
        continue
    if not run(mode + '-build', ['cmake', '--build', str(build), '--parallel', '1'], 14400):
        continue
    bins = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (build / 'bin').iterdir() if p.is_file()}
    (OUT / (mode + '-binaries.json')).write_text(json.dumps(bins, indent=2))
    run(mode + '-ctest-inventory', ['ctest', '--test-dir', str(build), '-N'])
    run(mode + '-tests', [str(build / 'bin/tests')])
    run(mode + '-rust-tests', ['cargo', 'test', '--manifest-path', '/work/rust/raycaster/Cargo.toml',
                              '--release', '--frozen', '--offline'])

raise SystemExit(0 if all(item['exit'] == 0 for item in RUNS) else 1)
