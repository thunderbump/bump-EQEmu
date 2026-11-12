"""Incremental ordinary rebuild in the disposable worker, preserving prior evidence."""
from pathlib import Path
import hashlib
import json
import os

# Reuse the existing resource/phase recorder without executing its full-build loop.
source = Path('/work/tests/map_server_validation/guest.py').read_text()
exec(compile(source[:source.index("assert memory()")], 'guest-recording', 'exec'))
assert memory()['memory.swap.max'] == '0'
os.environ.update(CARGO_HOME='/cargo', CARGO_BUILD_JOBS='1', CARGO_NET_OFFLINE='true',
                  CCACHE_DISABLE='1', RUSTUP_AUTO_INSTALL='0')
assert run('ownership-compile', ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I/work',
    '/work/tests/map_reload/ownership.cpp', '-o', '/results/ownership'])
assert run('ownership', ['/results/ownership'])
for mode in ('OFF', 'ON'):
    build = OUT / mode
    os.environ['CARGO_TARGET_DIR'] = str(OUT / ('cargo-'+mode))
    assert run(mode+'-incremental-full-build', ['cmake','--build',str(build),'--parallel','1'],14400)
    (OUT/(mode+'-new-binaries.json')).write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (build/'bin').iterdir() if p.is_file()},indent=2))
    assert run(mode+'-utility', [str(build/'bin/tests')])
