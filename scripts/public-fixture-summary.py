#!/usr/bin/env python3
"""Project private validation records into a bounded public diagnostic contract."""
import json
from pathlib import Path
import re
import sys

ASSERTIONS = frozenset('''preexisting_actor_tables critical_columns column_counts indexes
actor_status.status_json_constraint actor_events.event_json_constraint
actor_action_queue.source_metadata_json_constraint actor_action_queue.action_json_constraint
actor_action_queue.result_json_constraint character_count character_owners currency bot_count
bot_owner_total bot_owners fixture_characters fixture_bots fixture_currency
restored_version_schema_or_fixture_rows'''.split())
STEPS = frozenset('''setup prerequisites restore_snapshot seed_old_format candidate_update
candidate_scenarios upgraded_assertions candidate_target_versions idempotent_update
idempotent_data_assertions rollback_restore restored_assertions old_build_recovery
post_startup_restored_assertions cleanup unconfirmed_docker_creation'''.split())
CHECKS = {'tier1-build-and-unit-tests': 'tier1',
          'isolated-database-migration-rehearsal': 'migration_rehearsal',
          'tier3-zone-harness': 'tier3'}


def read_record(path):
    with path.open('rb') as stream:
        data = stream.read(1024 * 1024 + 1)
    if len(data) > 1024 * 1024:
        raise ValueError('record exceeds limit')
    value = json.loads(data)
    if not isinstance(value, dict):
        raise ValueError('record must be an object')
    return value


def milliseconds(value):
    return value if type(value) is int and 0 <= value <= 604800000 else None


def summarize(outer, checks, rehearsal):
    head = outer.get('actual_checkout_commit') or outer.get('expected_commit')
    if not isinstance(head, str) or not re.fullmatch('[0-9a-f]{40}', head):
        raise ValueError('no exact candidate identity')
    profile = outer.get('profile')
    if profile not in {'tier1-migration-tier3', 'migration-rehearsal', 'tier1',
                       'tier3-harness', 'tier1-tier3-harness', 'actor-queue-tier3',
                       'preflight', 'tier2-readonly', 'safe'}:
        raise ValueError('unknown profile')
    status = outer.get('status')
    if status not in {'passed', 'failed'}:
        raise ValueError('unknown outcome')
    step, codes = None, []
    if status == 'failed':
        step, codes = ('migration_rehearsal' if profile == 'migration-rehearsal' else 'validation'), ['validation_failed']
        if outer.get('category') == 'fixture_preparation_failed':
            step = 'fixture_preparation'
            codes = ['baseline_unavailable' if outer.get('exit_code') == 125 else 'preparation_failed']
        else:
            for check in checks.get('checks', []):
                if check.get('status') in {'rejected', 'inconclusive'} and check.get('name') in CHECKS:
                    step = CHECKS[check['name']]
                    codes = ['prerequisite_unavailable' if check['status'] == 'inconclusive' else 'validation_failed']
                    break
    # Old or unrelated nested evidence cannot explain this run's failure.
    matching = rehearsal.get('candidate_commit') == head
    if status == 'failed' and step == 'migration_rehearsal' and matching and rehearsal.get('status') == 'failed':
        if rehearsal.get('failure_step') in STEPS:
            step = rehearsal['failure_step']
        raw = rehearsal.get('assertion_result', '')
        if isinstance(raw, str) and raw.startswith('failed:'):
            labels = raw[7:].split(',')
            if labels and len(labels) <= len(ASSERTIONS) and all(label in ASSERTIONS for label in labels):
                codes = list(dict.fromkeys(labels))
    return {'schema_version': 1, 'head': head, 'profile': profile, 'status': status,
            'step': step, 'diagnostic_codes': codes,
            'timings_ms': {'validation': milliseconds(outer.get('validation_elapsed_ms')),
                           'restore': milliseconds(rehearsal.get('restore_elapsed_ms')) if matching else None}}


def main(directory):
    outer = read_record(directory / 'result.json')
    optional = []
    for name in ('afk-checks.json', 'migration-rehearsal/result.json'):
        try:
            optional.append(read_record(directory / name))
        except (OSError, ValueError):
            optional.append({})
    summary = summarize(outer, *optional)
    temporary = directory / 'public-summary.json.tmp'
    temporary.write_text(json.dumps(summary, indent=2) + '\n')
    temporary.replace(directory / 'public-summary.json')


if __name__ == '__main__':
    main(Path(sys.argv[1]))
