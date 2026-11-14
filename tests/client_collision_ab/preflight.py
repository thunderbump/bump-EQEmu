#!/usr/bin/env python3
"""Guest-only closure check. No profile, credentials, DB or game connection."""
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import time

out = Path('/evidence')
manifest = json.loads(Path('/inputs.json').read_text())
receipt = {'stage': 'runtime-closure', 'started_unix': time.time(),
           'libc': platform.libc_ver(), 'client_login_attempted': False,
           'world_started': False, 'backend_queries_executed': False}

def run(name, argv):
    result = subprocess.run(argv, capture_output=True, timeout=30)
    (out / (name + '.stdout')).write_bytes(result.stdout)
    (out / (name + '.stderr')).write_bytes(result.stderr)
    return {'argv': argv, 'exit': result.returncode}

try:
    for item in manifest['files']:
        with Path(item['guest_path']).open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != item['sha256']:
            raise RuntimeError('input hash mismatch: ' + item['guest_path'])
    receipt['inputs_verified'] = True
    receipt['client_ldd'] = run('client-ldd', ['ldd', '/client'])
    # No args reaches usage before profile parsing or runtime login. Exit 2 is
    # expected only if the loader successfully enters the pinned program.
    receipt['client_usage'] = run('client-usage', ['/client'])
    usage = (out / 'client-usage.stderr').read_text()
    receipt['client_loadable'] = (receipt['client_usage']['exit'] == 2 and
                                  'Usage: eqoxide-headless --profile' in usage)
    for backend in ('OFF', 'ON'):
        name = backend + '-login-ldd'
        receipt[name] = run(name, ['ldd', '-r', '/' + backend + '/loginserver'])
        text = (out / (name + '.stdout')).read_text() + (out / (name + '.stderr')).read_text()
        receipt[name]['closure_resolved'] = (receipt[name]['exit'] == 0 and
                                             'not found' not in text and 'undefined symbol' not in text)
    receipt['closure_ready'] = receipt['client_loadable'] and all(
        receipt[b + '-login-ldd']['closure_resolved'] for b in ('OFF', 'ON'))
except Exception as error:
    receipt['error'] = str(error)
finally:
    receipt['finished_unix'] = time.time()
    receipt['cgroup'] = {n: Path('/sys/fs/cgroup', n).read_text().strip()
                         for n in ('memory.max', 'memory.swap.max', 'memory.peak', 'memory.events')}
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
raise SystemExit(0 if receipt.get('closure_ready') else 1)
