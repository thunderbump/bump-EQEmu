#!/usr/bin/env python3
"""Exercise disposable resource cleanup after a partially failed Docker start."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DisposableRehearsal(unittest.TestCase):
    def test_failed_container_start_cleans_only_owned_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('snapshot.sql', 'seed.sql', 'assert.sql', 'world'):
                (root / name).write_text('fixture')
            digest = hashlib.sha256(b'fixture').hexdigest()
            (root / 'manifest.json').write_text(json.dumps({
                'snapshot': {'id': 'test', 'file': 'snapshot.sql', 'sha256': digest},
                'source': {'build': 'unattested', 'build_identity_attested': False,
                           'mariadb_version': '10.5.4',
                           'database_versions': {'server': 9328, 'bots': 9055, 'custom': 0}},
                'old_build': {'host_directory': 'old', 'world_binary_container_path': '/opt/eqemu-old/world',
                              'world_binary_sha256': digest},
                'fixtures': {'seed_sql': 'seed.sql', 'upgraded_assert_sql': 'assert.sql',
                             'restored_assert_sql': 'assert.sql'},
                'candidate_scenarios': ['true'],
            }))
            (root / 'old').mkdir()
            stack = root / 'validation-stack'
            for asset in ('server/shared', 'server/quests/plugins', 'server/quests/lua_modules'):
                (stack / asset).mkdir(parents=True, exist_ok=True)
            docker = root / 'docker'
            docker.write_text('''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
root = Path(os.environ['FAKE_DOCKER_ROOT'])
a = sys.argv[1:]
with (root/'calls').open('a') as f: f.write(json.dumps(a)+'\\n')
if a[:2] == ['image','inspect']: sys.exit(0)
if a[:2] in (['network','create'], ['volume','create']): sys.exit(0)
if a[0] == 'run':
 (root/'owner').write_text(a[a.index('--label')+1].split('=',1)[1])
 (root/'container').write_text(a[a.index('--name')+1])
 sys.exit(23)
if a[0] == 'inspect':
 if a[-1] == (root/'container').read_text(): print((root/'owner').read_text()); sys.exit(0)
 sys.exit(1)
if a[:2] in (['network','rm'], ['volume','rm']) or a[0] == 'rm': sys.exit(0)
sys.exit(98)
''')
            docker.chmod(0o755)
            env = dict(os.environ, PATH=str(root)+os.pathsep+os.environ['PATH'],
                       AKKSTACK_DIR=str(stack), FAKE_DOCKER_ROOT=str(root),
                       MIGRATION_REHEARSAL_MANIFEST=str(root/'manifest.json'),
                       MIGRATION_REHEARSAL_EVIDENCE_DIR=str(root/'evidence'))
            result = subprocess.run([str(ROOT/'scripts/rehearse-database-migration.sh')], env=env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stderr)
            calls = [json.loads(line) for line in (root/'calls').read_text().splitlines()]
            network = next(c for c in calls if c[:2] == ['network','create'])
            self.assertIn('--internal', network)
            run = next(c for c in calls if c[0] == 'run')
            self.assertNotIn('-p', run)
            self.assertTrue(any('type=volume' in x for x in run))
            self.assertFalse(any('type=bind' in x for x in run))
            owner = (root/'owner').read_text()
            removals = [c for c in calls if c[0] == 'rm' or c[:2] in (['network','rm'], ['volume','rm'])]
            self.assertEqual(len(removals), 3)
            self.assertTrue(all(c[-1].startswith(owner) for c in removals))
            evidence = json.loads((root/'evidence/result.json').read_text())
            self.assertEqual(evidence['status'], 'failed')
            self.assertFalse(evidence['source']['build_identity_attested'])


if __name__ == '__main__':
    unittest.main()
