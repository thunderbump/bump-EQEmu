#!/usr/bin/env python3
"""No private text survives the repository's public diagnostics projection."""
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODULE = runpy.run_path(str(ROOT / 'scripts/public-fixture-summary.py'))
HEAD = 'a' * 40


class SummaryTest(unittest.TestCase):
    def setUp(self):
        self.outer = {'actual_checkout_commit': HEAD, 'profile': 'tier1-migration-tier3',
                      'status': 'failed', 'category': 'validation_failed',
                      'validation_elapsed_ms': 1500,
                      'message': 'password=PRIVATE_SENTINEL', 'evidence_dir': '/private/SECRET'}
        self.checks = {'checks': [{'name': 'isolated-database-migration-rehearsal', 'status': 'rejected'}]}
        self.rehearsal = {'candidate_commit': HEAD, 'status': 'failed',
                          'failure_step': 'upgraded_assertions',
                          'assertion_result': 'failed:actor_events.event_json_constraint,fixture_currency',
                          'restore_elapsed_ms': None,
                          'source': {'build': 'PRIVATE_SENTINEL'}}

    def test_nested_assertions_are_named_without_copying_private_fields(self):
        summary = MODULE['summarize'](self.outer, self.checks, self.rehearsal)
        self.assertEqual(summary['step'], 'upgraded_assertions')
        self.assertEqual(summary['diagnostic_codes'], ['actor_events.event_json_constraint', 'fixture_currency'])
        self.assertEqual(summary['timings_ms']['validation'], 1500)
        self.assertNotIn('PRIVATE', json.dumps(summary))
        self.assertNotIn('/private', json.dumps(summary))

    def test_unknown_scalar_or_unrelated_candidate_never_becomes_public_text(self):
        for raw in ('failed:PRIVATE_SENTINEL', 'password=PRIVATE_SENTINEL', '', 'failed:fixture_currency,PRIVATE_SENTINEL'):
            self.rehearsal['assertion_result'] = raw
            result = MODULE['summarize'](self.outer, self.checks, self.rehearsal)
            self.assertEqual(result['diagnostic_codes'], ['validation_failed'])
        self.rehearsal['candidate_commit'] = 'b' * 40
        result = MODULE['summarize'](self.outer, self.checks, self.rehearsal)
        self.assertEqual(result['step'], 'migration_rehearsal')

    def test_executed_scenario_failure_is_not_a_missing_prerequisite(self):
        self.checks['checks'][0]['status'] = 'inconclusive'
        self.rehearsal.update(failure_step='candidate_scenarios', assertion_result='',
                              actor_runtime={'status': 'failed'})
        result = MODULE['summarize'](self.outer, self.checks, self.rehearsal)
        self.assertEqual(result['diagnostic_codes'], ['validation_failed'])
        self.rehearsal['failure_step'] = 'prerequisites'
        result = MODULE['summarize'](self.outer, self.checks, self.rehearsal)
        self.assertEqual(result['diagnostic_codes'], ['prerequisite_unavailable'])

    def test_worker_classifies_nested_scenario_timeout_as_validation_failure(self):
        script = (ROOT / 'scripts/validation-worker.sh').read_text()
        block = script[script.index('      if [[ "$validation_status" -eq 124'):]
        block = block.split('      afk_check_statuses[$afk_check_index]', 1)[0]
        with tempfile.TemporaryDirectory() as name:
            evidence = Path(name)
            (evidence / 'migration-rehearsal').mkdir()
            record = {'candidate_commit': HEAD, 'status': 'failed',
                      'failure_step': 'candidate_scenarios', 'actor_runtime': {'status': 'failed'}}
            for step, head, expected in [('candidate_scenarios', HEAD, 'rejected'),
                                         ('prerequisites', HEAD, 'inconclusive'),
                                         ('candidate_scenarios', 'b' * 40, 'inconclusive')]:
                record.update(failure_step=step, candidate_commit=head)
                (evidence / 'migration-rehearsal/result.json').write_text(json.dumps(record))
                env = dict(os.environ, evidence_dir=name, head_commit=HEAD,
                           afk_profile='migration-rehearsal', validation_status='124',
                           afk_failure_status='rejected', afk_inconclusive_message='unavailable')
                result = subprocess.run(['bash', '-c', block + '\nprintf %s "$afk_failure_status"'],
                                        env=env, capture_output=True, text=True, check=True)
                self.assertEqual(result.stdout, expected)

    def test_direct_migration_profile_retains_nested_diagnostic(self):
        self.outer['profile'] = 'migration-rehearsal'
        result = MODULE['summarize'](self.outer, {}, self.rehearsal)
        self.assertEqual(result['step'], 'upgraded_assertions')
        self.assertIn('fixture_currency', result['diagnostic_codes'])

    def test_preparation_failure_and_pass_are_distinct(self):
        self.outer.update(category='fixture_preparation_failed', exit_code=125)
        result = MODULE['summarize'](self.outer, {}, {})
        self.assertEqual(result['diagnostic_codes'], ['baseline_unavailable'])
        self.assertEqual(result['step'], 'fixture_preparation')
        self.outer['status'] = 'passed'
        result = MODULE['summarize'](self.outer, self.checks, self.rehearsal)
        self.assertIsNone(result['step'])
        self.assertEqual(result['diagnostic_codes'], [])

    def test_rehearsal_retains_failed_scalar_for_projection(self):
        # Execute the production shell function with a fake database adapter.
        script = (ROOT / 'scripts/rehearse-database-migration.sh').read_text()
        function = script.split('query_file_expect_ok() {', 1)[1].split('\nrun_candidate()', 1)[0]
        command = ('target_mysql() { printf "failed:fixture_currency"; }\n'
                   'assertion_result=""\nquery_file_expect_ok() {' + function +
                   '\nquery_file_expect_ok /dev/null\nstatus=$?\n'
                   'printf "%s\\n%s" "$status" "$assertion_result"')
        result = subprocess.run(['bash', '-c', command], capture_output=True, text=True, check=True)
        self.assertEqual(result.stdout, '1\nfailed:fixture_currency')

    def test_command_writes_contract_without_inspecting_logs(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            (directory / 'migration-rehearsal').mkdir()
            for filename, value in [('result.json', self.outer), ('afk-checks.json', self.checks),
                                    ('migration-rehearsal/result.json', self.rehearsal)]:
                (directory / filename).write_text(json.dumps(value))
            (directory / 'migration-rehearsal/rehearsal.log').write_text('[FAIL] private scenario detail')
            (directory / 'private.log').write_text('password=PRIVATE_SENTINEL')
            subprocess.run(['python3', str(ROOT / 'scripts/public-fixture-summary.py'), name], check=True)
            result = json.loads((directory / 'public-summary.json').read_text())
            self.assertEqual(result['diagnostic_codes'][0], 'actor_events.event_json_constraint')
            self.assertNotIn('PRIVATE_SENTINEL', json.dumps(result))
            manifest = json.loads((directory / 'diagnostic-files.json').read_text())
            self.assertEqual(manifest, {'schema_version': 1, 'head': HEAD, 'files': ['migration-rehearsal/rehearsal.log']})


if __name__ == '__main__':
    unittest.main()
