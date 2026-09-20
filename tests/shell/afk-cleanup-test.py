#!/usr/bin/env python3
"""Use temporary real clones and leases; never remove live repository artifacts."""
import fcntl
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('cleanup', ROOT / 'scripts/afk_cleanup.py')
cleanup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cleanup)


class CleanupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / 'worker'
        (self.home / 'locks').mkdir(parents=True)
        self.stack = self.root / 'stack'
        self.stack.mkdir()
        self.directory = self.root / 'jobs' / ('a' * 16)
        self.evidence = self.directory / 'fixture-evidence'
        self.prepared = self.evidence / 'prepared-fixture'
        self.prepared.mkdir(parents=True)
        self.checkout = self.home / 'checkouts' / 'test'
        self.checkout.mkdir(parents=True)
        subprocess.run(['git', 'init', '-q', str(self.checkout)], check=True)
        (self.checkout / 'source').write_text('retained source')
        subprocess.run(['git', '-C', str(self.checkout), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.checkout), '-c', 'user.name=Test', '-c', 'user.email=test@example.com', 'commit', '-qm', 'source'], check=True)
        self.head = cleanup.run('git', '-C', str(self.checkout), 'rev-parse', 'HEAD')
        self.job = {'id': self.directory.name, 'head': self.head, 'workspace_root': str(self.root / 'workspaces'), 'expected_phases': ['fixtures'], 'fixture_resource': {'worker_home': str(self.home), 'stack_path': str(self.stack)}}
        self.result = {'status': 'passed', 'checkout_dir': str(self.checkout), 'expected_commit': self.head, 'evidence_dir': str(self.evidence), 'request_metadata': {'run_id': 'test', 'source': {'type': 'fetch', 'commit': self.head, 'repo': str(self.root / 'workspaces' / self.job['id'] / 'fixtures')}}}
        (self.evidence / 'result.json').write_text(json.dumps(self.result))
        self.manifest = self.prepared / 'migration-rehearsal-manifest.json'
        self.manifest.write_text(json.dumps({'source': {'fixture_preparer_commit': self.head}}))
        self.leases = [self.home / 'locks/validation-slot.lock', self.stack / '.validation-worker-code.lock']
        self.mounts = mock.patch.object(cleanup, 'container_mounts', return_value=[])
        self.mounts.start()
        self.addCleanup(self.mounts.stop)

    def test_holds_leases_during_deletion_and_preserves_manifest(self):
        with cleanup.cleanup_targets(self.directory, self.job, apply=True) as targets:
            self.assertEqual(targets, [self.checkout, self.prepared])
            for lease in self.leases:
                self.assertTrue(lease.is_dir())
                with Path(str(lease) + '.guard').open('a') as lock:
                    with self.assertRaises(BlockingIOError):
                        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            for target in targets:
                shutil.rmtree(target)
        self.assertTrue((self.evidence / 'prepared-fixture-manifest.json').is_file())
        self.assertTrue((self.evidence / 'result.json').is_file())
        self.assertFalse(any(p.exists() for p in self.leases))
        with cleanup.cleanup_targets(self.directory, self.job) as targets:
            self.assertEqual(targets, [self.checkout, self.prepared])

    def test_preview_keeps_payload_and_does_not_copy_manifest(self):
        with cleanup.cleanup_targets(self.directory, self.job):
            pass
        self.assertTrue(self.prepared.exists())
        self.assertFalse((self.evidence / 'prepared-fixture-manifest.json').exists())

    def test_existing_lease_or_guard_blocks_and_releases_own_first_lock(self):
        self.leases[1].mkdir()
        with self.assertRaises(FileExistsError), cleanup.cleanup_targets(self.directory, self.job):
            pass
        self.assertFalse(self.leases[0].exists())
        self.assertTrue(self.leases[1].exists())
        self.leases[1].rmdir()
        with Path(str(self.leases[0]) + '.guard').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaises(BlockingIOError), cleanup.cleanup_targets(self.directory, self.job):
                pass

    def test_bound_container_and_baseline_references_are_retained(self):
        (self.stack / 'code').symlink_to(self.checkout)
        with self.assertRaisesRegex(ValueError, 'stack'), cleanup.cleanup_targets(self.directory, self.job):
            pass
        (self.stack / 'code').unlink()
        with mock.patch.object(cleanup, 'container_mounts', return_value=[self.checkout / 'build']):
            with self.assertRaisesRegex(ValueError, 'container'), cleanup.cleanup_targets(self.directory, self.job):
                pass
        (self.home / 'migration-rehearsal-baseline.json').write_text(json.dumps({'snapshot': str(self.prepared / 'database.sql.gz')}))
        with self.assertRaisesRegex(ValueError, 'baseline'), cleanup.cleanup_targets(self.directory, self.job):
            pass
        self.assertFalse(any(p.exists() for p in self.leases))

    def test_dirty_symlink_and_unowned_checkout_are_retained(self):
        (self.checkout / 'source').write_text('uncommitted')
        with self.assertRaisesRegex(ValueError, 'unpublished'), cleanup.cleanup_targets(self.directory, self.job):
            pass
        (self.checkout / 'source').write_text('retained source')
        (self.prepared / 'escape').symlink_to(self.root)
        with self.assertRaisesRegex(ValueError, 'symlink'), cleanup.cleanup_targets(self.directory, self.job):
            pass
        (self.prepared / 'escape').unlink()
        self.result['request_metadata']['source']['type'] = 'local-checkout'
        (self.evidence / 'result.json').write_text(json.dumps(self.result))
        with self.assertRaisesRegex(ValueError, 'ownership'), cleanup.cleanup_targets(self.directory, self.job):
            pass

    def test_unpublished_branch_and_reflog_are_retained(self):
        cleanup.run('git', '-C', str(self.checkout), 'checkout', '-qb', 'unpublished')
        (self.checkout / 'source').write_text('unpublished commit')
        cleanup.run('git', '-C', str(self.checkout), '-c', 'user.name=Test', '-c', 'user.email=test@example.com', 'commit', '-qam', 'local work')
        cleanup.run('git', '-C', str(self.checkout), 'checkout', '--detach', self.head)
        with self.assertRaisesRegex(ValueError, 'unpublished commits'), cleanup.cleanup_targets(self.directory, self.job):
            pass
        cleanup.run('git', '-C', str(self.checkout), 'branch', '-D', 'unpublished')
        with self.assertRaisesRegex(ValueError, 'unpublished commits'), cleanup.cleanup_targets(self.directory, self.job):
            pass

    def test_partial_deletion_can_resume(self):
        with cleanup.cleanup_targets(self.directory, self.job, apply=True):
            shutil.rmtree(self.checkout / '.git')
            self.manifest.unlink()
        with cleanup.cleanup_targets(self.directory, self.job, apply=True, resume=True) as targets:
            for target in targets:
                shutil.rmtree(target)
        self.assertTrue((self.evidence / 'prepared-fixture-manifest.json').exists())

    def test_resume_keeps_new_work_in_intact_checkout(self):
        (self.checkout / 'new-work').write_text('keep me')
        with self.assertRaisesRegex(ValueError, 'unpublished'), cleanup.cleanup_targets(self.directory, self.job, resume=True):
            pass
        self.assertTrue((self.checkout / 'new-work').exists())

    def test_killed_collector_leases_are_recovered_by_next_call(self):
        program = """
import importlib.util,json,sys,time
from pathlib import Path
s=importlib.util.spec_from_file_location('cleanup',sys.argv[1]);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
m.container_mounts=lambda:[]
with m.cleanup_targets(Path(sys.argv[2]),json.loads(sys.argv[3])):
 print('held',flush=True)
 time.sleep(60)
"""
        with subprocess.Popen([sys.executable, '-c', program, str(ROOT / 'scripts/afk_cleanup.py'), str(self.directory), json.dumps(self.job)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as child:
            import select
            self.assertTrue(select.select([child.stdout], [], [], 5)[0])
            self.assertEqual(child.stdout.readline().strip(), 'held')
            child.kill()
            child.wait(timeout=5)
        self.assertTrue(all(p.is_symlink() for p in self.leases))
        with cleanup.cleanup_targets(self.directory, self.job):
            self.assertTrue(all(p.exists() for p in self.leases))
        self.assertFalse(any(p.is_symlink() for p in self.leases))

    def test_uncertain_docker_creation_retains_payload(self):
        (self.evidence / 'docker-creation-pending').touch()
        with self.assertRaisesRegex(ValueError, 'unconfirmed'), cleanup.cleanup_targets(self.directory, self.job):
            pass
        self.assertTrue(self.prepared.exists())
        self.assertFalse(any(p.exists() for p in self.leases))

    def test_owned_docker_resources_require_recovery(self):
        (self.evidence / 'lifetime.json').write_text(json.dumps({'token': 'owned-token'}))
        (self.evidence / 'docker-owned').touch()
        with mock.patch.object(cleanup, 'run', return_value='container-id'):
            with self.assertRaisesRegex(ValueError, 'owned Docker'), cleanup.cleanup_targets(self.directory, self.job):
                pass
        self.assertTrue(self.prepared.exists())
        self.assertFalse(any(p.exists() for p in self.leases))

    def test_docker_failure_releases_leases_and_deletes_nothing(self):
        with mock.patch.object(cleanup, 'container_mounts', side_effect=subprocess.CalledProcessError(1, 'docker')):
            with self.assertRaises(subprocess.CalledProcessError), cleanup.cleanup_targets(self.directory, self.job):
                pass
        self.assertTrue(self.checkout.exists())
        self.assertFalse(any(p.exists() for p in self.leases))


if __name__ == '__main__':
    unittest.main()
