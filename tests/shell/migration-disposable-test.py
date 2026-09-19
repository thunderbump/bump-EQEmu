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
            candidate_commit = subprocess.check_output(
                ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
            manifest = {
                'snapshot': {'id': 'test', 'file': 'snapshot.sql', 'sha256': digest},
                'source': {'build': 'unattested', 'build_identity_attested': False,
                           'fixture_preparer_commit': candidate_commit,
                           'mariadb_version': '10.5.4',
                           'database_versions': {'server': 9328, 'bots': 9055, 'custom': 0}},
                'old_build': {'host_directory': 'old', 'world_binary_container_path': '/opt/eqemu-old/world',
                              'world_binary_sha256': digest},
                'fixtures': {'seed_sql': 'seed.sql', 'upgraded_assert_sql': 'assert.sql',
                             'restored_assert_sql': 'assert.sql'},
                'candidate_scenarios': ['true'],
            }
            (root / 'manifest.json').write_text(json.dumps(manifest))
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
if a[0] == 'create':
 (root/'owner').write_text(a[a.index('--label')+1].split('=',1)[1])
 (root/'container').write_text(a[a.index('--name')+1])
 sys.exit(23 if os.environ.get('CREATE_REPLY_LOST') else 0)
if a[0] == 'start': sys.exit(23)
if a[:2] == ['container','ls']:
 name=(root/'container').read_text()
 if os.environ.get('RUNTIME_INSPECT_FAIL'): print(name.removesuffix('-db')+'-runtime')
 print(name); sys.exit(0)
if a[0] == 'inspect':
 if os.environ.get('RUNTIME_INSPECT_FAIL') and a[-1].endswith('-runtime'): sys.exit(1)
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
            command = [str(ROOT/'scripts/rehearse-database-migration.sh')]

            manifest['old_build']['world_binary_container_path'] = \
                '/opt/eqemu-old/../home/eqemu/code/build/bin/world'
            (root / 'manifest.json').write_text(json.dumps(manifest))
            rejected = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 2)
            self.assertIn('may not contain . or .. components', rejected.stderr)
            self.assertFalse((root / 'calls').exists())

            manifest['old_build']['world_binary_container_path'] = '/opt/eqemu-old/world'
            manifest['source']['fixture_preparer_commit'] = '0' * 40
            (root / 'manifest.json').write_text(json.dumps(manifest))
            rejected = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(rejected.returncode, 125)
            self.assertIn('does not match Candidate', rejected.stderr)
            self.assertFalse((root / 'calls').exists())

            manifest['source']['fixture_preparer_commit'] = candidate_commit
            (root / 'manifest.json').write_text(json.dumps(manifest))
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stderr)
            calls = [json.loads(line) for line in (root/'calls').read_text().splitlines()]
            network = next(c for c in calls if c[:2] == ['network','create'])
            self.assertIn('--internal', network)
            run = next(c for c in calls if c[0] == 'create')
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
            self.assertFalse(evidence['candidate']['artifact_identity_attested'])

            (root/'calls').unlink()
            result=subprocess.run(command,env={**env,'RUNTIME_INSPECT_FAIL':'1'},capture_output=True,text=True)
            self.assertEqual(result.returncode,1,result.stderr)
            calls=[json.loads(line) for line in (root/'calls').read_text().splitlines()]
            self.assertFalse(any(c[0]=='rm' or c[:2] in (['network','rm'],['volume','rm']) for c in calls))
            self.assertEqual(json.loads((root/'evidence/result.json').read_text())['failure_step'],'cleanup')

            # Accepted creation with a lost reply must not be treated as absence.
            (root/'calls').unlink()
            result=subprocess.run(command,env={**env,'CREATE_REPLY_LOST':'1'},capture_output=True,text=True)
            self.assertEqual(result.returncode,1,result.stderr)
            self.assertTrue((root/'evidence/docker-creation-pending').exists())
            calls=[json.loads(line) for line in (root/'calls').read_text().splitlines()]
            self.assertFalse(any(c[0]=='rm' or c[:2] in (['network','rm'],['volume','rm']) for c in calls))
            record=json.loads((root/'evidence/result.json').read_text())
            self.assertEqual(record['failure_step'],'unconfirmed_docker_creation')

    def test_scenarios_require_actor_completion_and_successful_exit(self):
        runner=ROOT/'scripts/lib/run-migration-scenarios.sh'
        for commands,expected in (
            (["printf '[PASS] actor-events-runtime\\n'"],0),
            (['true'],1),
            (["printf '[PASS] actor-events-runtime\\n'; exit 7"],7),
            (["printf '[PASS] actor-events-runtime\\n'", 'exit 8'],8),
            (["printf 'first\\n'\n" + "printf '[PASS] actor-events-runtime\\n'"],0),
        ):
            with self.subTest(commands=commands):
                result=subprocess.run([str(runner)],env={**os.environ,
                    'MIGRATION_SCENARIOS_JSON':json.dumps(commands)},capture_output=True,text=True)
                self.assertEqual(result.returncode,expected,result.stderr)

    def test_runtime_preserves_multiline_scenarios_and_checks_process_survival(self):
        runtime = (ROOT / 'scripts/lib/migration-runtime.sh').read_text() + (ROOT / 'scripts/lib/run-migration-scenarios.sh').read_text()
        self.assertIn("read -r -d '' command", runtime)
        self.assertIn('.[] + "\\u0000"', runtime)
        self.assertIn('kill -0 "$world_pid"', runtime)
        self.assertNotIn('startup_status" == 124', runtime)

    def test_major_minor_mariadb_versions_allow_only_a_bounded_patch_suffix(self):
        script = (ROOT / 'scripts/rehearse-database-migration.sh').read_text()
        self.assertIn('^[0-9]+[.][0-9]+$', script)
        self.assertIn('"$source_mariadb".*', script)

    def test_candidate_targets_require_complete_numeric_definitions(self):
        import runpy
        read = runpy.run_path(str(ROOT / 'scripts/lib/migration-target-versions.py'))['target_versions']
        header = ('#define CURRENT_BINARY_DATABASE_VERSION 9335\n'
                  '#define CURRENT_BINARY_BOTS_DATABASE_VERSION 9055\n'
                  '#define CUSTOM_BINARY_DATABASE_VERSION 0\n')
        self.assertEqual(read(header), '9335:9055:0')
        for invalid in (header.replace('9335', 'unknown'),
                        header + '#define CUSTOM_BINARY_DATABASE_VERSION 1\n',
                        header.replace('#define CUSTOM_BINARY_DATABASE_VERSION 0\n', '')):
            with self.assertRaises(ValueError):
                read(invalid)

    def test_json_constraints_are_checked_behaviorally_not_via_truncated_metadata(self):
        preparer = (ROOT / 'scripts/prepare-migration-rehearsal-fixture.sh').read_text()
        assertion = preparer.split(
            'cat >"$fixture_dir/assert-upgraded.sql"', 1)[1].split('\nSQL\n', 1)[0]
        self.assertNotIn('FROM information_schema.check_constraints', assertion)
        self.assertEqual(assertion.count('DECLARE CONTINUE HANDLER FOR 4025'), 10)
        for label in (
                'actor_status.status_json_constraint',
                'actor_events.event_json_constraint',
                'actor_action_queue.source_metadata_json_constraint',
                'actor_action_queue.action_json_constraint',
                'actor_action_queue.result_json_constraint'):
            self.assertIn(label, assertion)


if __name__ == '__main__':
    unittest.main()
