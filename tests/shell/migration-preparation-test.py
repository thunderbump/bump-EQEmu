#!/usr/bin/env python3
"""Exercise real fixture preparation without Docker or private captured data."""
import concurrent.futures
import hashlib
import io
import json
import subprocess
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class PreparationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.baseline = self.root / 'baseline'
        self.baseline.mkdir()
        snapshot = self.baseline / 'database.sql'
        snapshot.write_text('SELECT 1;\n')
        archive = self.baseline / 'installed-binaries.tar.gz'
        with tarfile.open(archive, 'w:gz') as tf:
            world = tarfile.TarInfo('world')
            world.mode = 0o755
            world.size = 4
            tf.addfile(world, io.BytesIO(b'test'))
        self.manifest = {
            'files': {p.name: {'sha256': digest(p)} for p in (snapshot, archive)},
            'source': {'build': 'test-build', 'mariadb_version': '10.5.4',
                       'database_versions': {'server': 9328, 'bots': 9055, 'custom': 0}},
        }
        (self.baseline / 'manifest.json').write_text(json.dumps(self.manifest))

    def prepare(self, output):
        return subprocess.run([
            str(ROOT / 'scripts/prepare-migration-rehearsal-fixture.sh'),
            '--baseline-dir', str(self.baseline), '--output-dir', str(output),
        ], capture_output=True, text=True)

    def test_parallel_runs_preserve_baseline_and_record_private_input_identity(self):
        before = {p.name: digest(p) for p in self.baseline.iterdir()}
        outputs = [self.root / 'run-a', self.root / 'run-b']
        with concurrent.futures.ThreadPoolExecutor() as pool:
            results = list(pool.map(self.prepare, outputs))
        for result in results:
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(before, {p.name: digest(p) for p in self.baseline.iterdir()})
        head = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        for output in outputs:
            manifest = json.loads((output / 'migration-rehearsal-manifest.json').read_text())
            self.assertEqual(manifest['source']['fixture_preparer_commit'], head)
            self.assertEqual(manifest['source']['capture_manifest_sha256'], before['manifest.json'])
            self.assertEqual(manifest['source']['binary_archive_sha256'], before['installed-binaries.tar.gz'])
            self.assertEqual(digest(output / manifest['snapshot']['file']), manifest['snapshot']['sha256'])
            for name, relative in manifest['fixtures'].items():
                self.assertEqual(digest(output / relative), manifest['fixture_sha256'][name])
            self.assertEqual(output.stat().st_mode & 0o777, 0o700)
            self.assertNotEqual((output / 'database.sql').stat().st_ino,
                                (self.baseline / 'database.sql').stat().st_ino)
            self.assertFalse((output / 'migration-rehearsal.manifest-path').exists())
        old_manifest = (outputs[0] / 'migration-rehearsal-manifest.json').read_bytes()
        self.assertNotEqual(self.prepare(outputs[0]).returncode, 0)
        self.assertEqual((outputs[0] / 'migration-rehearsal-manifest.json').read_bytes(), old_manifest)

    def test_overlap_and_invalid_baseline_fail_without_generation(self):
        for output in (self.baseline, self.baseline / 'child', self.root):
            result = self.prepare(output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('must not overlap', result.stderr)
        (self.baseline / 'database.sql').write_text('changed')
        output = self.root / 'invalid-run'
        result = self.prepare(output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checksum', result.stderr)
        self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
