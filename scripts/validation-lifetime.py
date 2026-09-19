#!/usr/bin/env python3
"""Own one validation process group, its leases, and recoverable cleanup."""
import argparse
import fcntl
import json
import os
import re
from pathlib import Path
import signal
import subprocess
import sys
import time
import uuid

LABEL = 'org.eqemu.validation'


def save(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def guard(path, wait=0):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(str(path) + '.guard', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    deadline = time.monotonic() + wait
    while True:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return fd
        except BlockingIOError:
            if time.monotonic() >= deadline:
                os.close(fd)
                raise RuntimeError(f'live validation owns {path}')
            time.sleep(.1)


def docker(deadline, *args):
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise RuntimeError('Docker cleanup deadline exceeded; leases retained')
    return subprocess.check_output(['docker', *args], text=True, stderr=subprocess.STDOUT, timeout=min(5, remaining)).strip()


def clean(owner):
    """Called only with both leases held and after all owned processes stop."""
    evidence = Path(owner['evidence'])
    deadline = time.monotonic() + 20
    if (evidence / 'docker-owned').exists():
        for kind in ('container', 'volume', 'network'):
            ids = docker(deadline, kind, 'ls', '-q', '--filter', f'label={LABEL}={owner["token"]}').split()
            for identity in ids:
                obj = json.loads(docker(deadline, kind, 'inspect', identity))[0]
                labels = obj.get('Config', {}).get('Labels', {}) if kind == 'container' else obj.get('Labels', {})
                if (labels or {}).get(LABEL) != owner['token']:
                    raise RuntimeError('Docker ownership changed; refusing cleanup')
                docker(deadline, kind, 'rm', *(['-f'] if kind == 'container' else []), identity)
    binding_path = evidence / 'stack-binding.json'
    if binding_path.exists():
        binding = json.loads(binding_path.read_text())
        if binding.get('restore_status') == 'pending':
            code = Path(owner['stack']) / 'code'
            target = owner['checkout']
            if binding['code_path'] != str(code) or binding['target'] != target:
                raise RuntimeError('stack binding ownership does not match lease')
            previous = binding['previous_target']
            kind = binding['previous_kind']
            # A prior cleanup can have restored the link before its journal update.
            already = (kind == 'symlink' and code.is_symlink() and os.readlink(code) == previous) or (kind == 'missing' and not os.path.lexists(code))
            if not already:
                if not code.is_symlink() or os.readlink(code) != target:
                    raise RuntimeError('stack code changed; refusing to overwrite it')
                if kind == 'symlink':
                    temporary = code.with_name('.validation-restore-' + owner['token'])
                    temporary.symlink_to(previous)
                    temporary.replace(code)
                elif kind == 'missing':
                    code.unlink()
                else:
                    raise RuntimeError('unknown previous stack binding')
            binding['restore_status'] = 'restored' if kind == 'symlink' else 'removed'
            save(binding_path, binding)


def release(owner):
    for name in reversed(owner['locks']):
        path = Path(name)
        if not path.exists():
            continue
        if json.loads((path / 'owner.json').read_text()) != owner:
            raise RuntimeError('lease ownership changed; refusing removal')
        (path / 'owner.json').unlink()
        path.rmdir()


def recover(locks, held):
    for lock in locks:
        if not lock.exists():
            continue
        try:
            owner = json.loads((lock / 'owner.json').read_text())
        except (OSError, ValueError) as error:
            raise RuntimeError(f'Unowned or damaged lease {lock}; manual inspection required') from error
        if not re.fullmatch('[0-9a-f]{32}', owner.get('token', '')):
            raise RuntimeError('invalid recovery token')
        if json.loads((Path(owner['evidence']) / 'lifetime.json').read_text()) != owner:
            raise RuntimeError('recovery evidence does not match lease')
        if owner.get('schema_version') != 1 or str(lock) not in owner['locks']:
            raise RuntimeError('unknown lease; manual inspection required')
        # A stack can have been used by another worker home. Take its other
        # lease too; never recover while an inherited descriptor is still live.
        extra = []
        try:
            for name in owner['locks']:
                if name not in held:
                    extra.append(guard(Path(name)))
            for name in owner['locks']:
                path = Path(name)
                if path.exists() and json.loads((path / 'owner.json').read_text()) != owner:
                    raise RuntimeError('inconsistent recovery ownership')
            clean(owner)
            release(owner)
            save(Path(owner['evidence']) / 'recovery.json', {'status': 'recovered', 'token': owner['token']})
        finally:
            for fd in extra:
                os.close(fd)


def group_alive(pgid):
    # Zombies have stopped executing and cannot hold leases or use Docker.
    for entry in Path('/proc').glob('[0-9]*/stat'):
        try:
            fields = entry.read_text().rsplit(')', 1)[1].split()
            if int(fields[2]) == pgid and fields[0] != 'Z':
                return True
        except (FileNotFoundError, ProcessLookupError):
            continue
    return False


def stop_group(process):
    # Always signal the group, even if its leader exited leaving descendants.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + 5
    while group_alive(process.pid) and time.monotonic() < deadline:
        process.poll()
        time.sleep(.05)
    if group_alive(process.pid):
        os.killpg(process.pid, signal.SIGKILL)
    process.wait(timeout=5)
    deadline = time.monotonic() + 5
    while group_alive(process.pid) and time.monotonic() < deadline:
        time.sleep(.05)
    if group_alive(process.pid):
        raise RuntimeError('validation process group is still alive; leases retained')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('worker-home', 'stack', 'evidence', 'checkout'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--wait', type=int, required=True)
    parser.add_argument('--timeout', type=int, required=True)
    parser.add_argument('--recover', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    evidence = Path(args.evidence).resolve()
    locks = [Path(args.worker_home).resolve() / 'locks/validation-slot.lock']
    if args.stack:
        locks.append(Path(args.stack).resolve() / '.validation-worker-code.lock')
    held = {}
    owner = None
    child = None
    interrupted = []
    status = 1
    category = 'worker_busy'
    message = ''
    signal.signal(signal.SIGTERM, lambda *_: interrupted.append(signal.SIGTERM))
    signal.signal(signal.SIGINT, lambda *_: interrupted.append(signal.SIGINT))
    try:
        for lock in locks:
            category = 'worker_busy' if lock == locks[0] else 'stack_busy'
            held[str(lock)] = guard(lock, args.wait)
        recover(locks, held)
        if args.recover:
            print('Validation recovery complete; no live lease was removed.')
            return 0
        owner = dict(schema_version=1, token=uuid.uuid4().hex, locks=[str(p) for p in locks], evidence=str(evidence), stack=str(Path(args.stack).resolve()) if args.stack else '', checkout=str(Path(args.checkout).resolve()))
        for lock in locks:
            lock.mkdir()
            save(lock / 'owner.json', owner)
        save(evidence / 'lifetime.json', owner)
        env = {**os.environ, 'VALIDATION_WORKER_LIFETIME_TOKEN': owner['token'], 'VALIDATION_WORKER_LIFETIME_PARENT': str(os.getpid()), 'VALIDATION_WORKER_DOCKER_MARKER': str(evidence / 'docker-owned')}
        category = 'launch_failed'
        child = subprocess.Popen(args.command[1:], env=env, start_new_session=True, pass_fds=tuple(held.values()))
        deadline = time.monotonic() + args.timeout
        while child.poll() is None and not interrupted and time.monotonic() < deadline:
            time.sleep(.05)
        if interrupted:
            status, category, message = 128 + interrupted[0], 'interrupted', 'Validation interrupted; owned resources cleaned.'
        elif child.poll() is None:
            status, category, message = 1, 'timeout', 'Validation exceeded the repository deadline.'
        else:
            status = child.returncode
            category = ''
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        message = str(error)
    finally:
        try:
            if child:
                stop_group(child)
            if owner:
                clean(owner)
                release(owner)
        except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
            status, category, message = 1, 'cleanup_failed', str(error)
        if category and not args.recover:
            result_path = evidence / 'result.json'
            result = json.loads(result_path.read_text()) if result_path.exists() else {}
            result.update(status='failed', category=category, exit_code=status, message=message)
            save(result_path, result)
            save(evidence / 'worker-output.json', result)
            subprocess.run([sys.executable, str(Path(__file__).with_name('public-fixture-summary.py')), str(evidence)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        for fd in held.values():
            os.close(fd)
    if message:
        print(message, file=sys.stderr)
    return status


if __name__ == '__main__':
    sys.exit(main())
