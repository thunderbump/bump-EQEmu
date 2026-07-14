import copy
import subprocess
import sys
import unittest

from objective_model import make_snapshot, replay, step


class ObjectiveModelTest(unittest.TestCase):
    def test_cli_replan_requires_replanning_status(self):
        result = subprocess.run(
            [sys.executable, "objective_model.py", "cautious"],
            input="decide\nfailed\ndeath\nreplan alternate-route\nquit\n",
            text=True,
            capture_output=True,
            check=True,
        )

        self.assertIn("error: objective is not awaiting a replacement plan", result.stdout)

    def test_emits_only_one_action_and_advances_after_observed_success(self):
        snapshot = make_snapshot("steady")

        snapshot, action, decision = step(snapshot, {"type": "decide"})
        self.assertEqual("move_to", action["type"])
        self.assertEqual("travel", snapshot["objective"]["phase"])
        self.assertEqual("action_requested", decision["reason"])

        snapshot, action, decision = step(
            snapshot,
            {
                "type": "action_outcome",
                "action_id": action["id"],
                "outcome": "succeeded",
            },
        )
        self.assertEqual("engage", action["type"])
        self.assertEqual("hunt", snapshot["objective"]["phase"])
        self.assertEqual("postcondition_observed", decision["reason"])

    def test_profile_persistence_produces_understandable_retry_differences(self):
        cautious = make_snapshot("cautious")
        stubborn = make_snapshot("stubborn")

        cautious, cautious_action, _ = step(cautious, {"type": "decide"})
        stubborn, stubborn_action, _ = step(stubborn, {"type": "decide"})

        cautious, next_cautious_action, cautious_decision = step(
            cautious,
            {
                "type": "action_outcome",
                "action_id": cautious_action["id"],
                "outcome": "failed",
            },
        )
        stubborn, next_stubborn_action, stubborn_decision = step(
            stubborn,
            {
                "type": "action_outcome",
                "action_id": stubborn_action["id"],
                "outcome": "failed",
            },
        )

        self.assertIsNone(next_cautious_action)
        self.assertEqual("replanning", cautious["objective"]["status"])
        self.assertEqual("action_retry_budget_exhausted", cautious_decision["reason"])
        self.assertEqual("move_to", next_stubborn_action["type"])
        self.assertEqual("retrying", stubborn_decision["reason"])

    def test_replanning_requires_a_fresh_subject_before_retrying(self):
        snapshot = make_snapshot("cautious")
        snapshot, travel_action, _ = step(snapshot, {"type": "decide"})
        snapshot, hunt_action, _ = step(
            snapshot,
            {
                "type": "action_outcome",
                "action_id": travel_action["id"],
                "outcome": "succeeded",
            },
        )

        snapshot, action, decision = step(
            snapshot,
            {
                "type": "action_outcome",
                "action_id": hunt_action["id"],
                "outcome": "failed",
            },
        )
        self.assertIsNone(action)
        self.assertEqual("replanning", snapshot["objective"]["status"])
        self.assertIsNone(snapshot["objective"]["phase_payload"])
        self.assertEqual("action_retry_budget_exhausted", decision["reason"])

        snapshot, action, decision = step(snapshot, {"type": "decide"})
        self.assertIsNone(action)
        self.assertEqual("awaiting_replacement_plan", decision["reason"])

        snapshot, replacement_action, decision = step(
            snapshot,
            {
                "type": "replan",
                "request_id": snapshot["objective"]["replan_request"]["id"],
                "payload": {"target": "easier-allowlisted-mob"},
            },
        )
        self.assertEqual("active", snapshot["objective"]["status"])
        self.assertEqual("easier-allowlisted-mob", replacement_action["payload"]["target"])
        self.assertEqual("replacement_plan_observed", decision["reason"])

    def test_active_objective_refuses_to_emit_without_a_phase_plan(self):
        snapshot = make_snapshot("steady")
        snapshot["objective"]["phase_payload"] = None

        snapshot, action, decision = step(snapshot, {"type": "decide"})

        self.assertIsNone(action)
        self.assertEqual("phase_plan_required", decision["reason"])

    def test_objective_viability_bounds_repeated_replanning(self):
        snapshot = make_snapshot("cautious")
        snapshot["objective"]["viability_budget"] = 1
        snapshot, action, _ = step(snapshot, {"type": "decide"})

        snapshot, _, _ = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "failed"},
        )
        snapshot, action, _ = step(
            snapshot,
            {
                "type": "replan",
                "request_id": snapshot["objective"]["replan_request"]["id"],
                "payload": {"checkpoint": "alternate-route"},
            },
        )
        snapshot, next_action, decision = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "failed"},
        )

        self.assertIsNone(next_action)
        self.assertEqual("abandoned", snapshot["objective"]["status"])
        self.assertEqual(2, snapshot["objective"]["viability_spent"])
        self.assertEqual("objective_viability_exhausted", decision["reason"])

    def test_viability_abandonment_clears_replan_and_recovery_state(self):
        snapshot = make_snapshot("cautious")
        snapshot["objective"]["viability_budget"] = 2
        snapshot, action, _ = step(snapshot, {"type": "decide"})
        snapshot, _, _ = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "failed"},
        )
        snapshot, _, _ = step(
            snapshot,
            {"type": "interruption", "observation_id": 1, "reason": "capacity_reclaimed"},
        )

        snapshot, action, decision = step(
            snapshot,
            {"type": "interruption", "observation_id": 2, "reason": "capacity_reclaimed"},
        )

        self.assertIsNone(action)
        self.assertEqual("abandoned", snapshot["objective"]["status"])
        self.assertIsNone(snapshot["objective"]["replan_request"])
        self.assertIsNone(snapshot["objective"]["resume_status"])
        self.assertIsNone(snapshot["objective"]["interrupted_by"])
        self.assertIsNone(snapshot["pending_action"])
        self.assertEqual("objective_viability_exhausted", decision["reason"])

    def test_danger_and_death_interrupt_without_losing_objective_progress(self):
        snapshot = make_snapshot("cautious")
        snapshot, travel_action, _ = step(snapshot, {"type": "decide"})
        snapshot, hunt_action, _ = step(
            snapshot,
            {
                "type": "action_outcome",
                "action_id": travel_action["id"],
                "outcome": "succeeded",
            },
        )

        snapshot, recovery_action, decision = step(
            snapshot, {"type": "danger", "observation_id": 1, "severity": 70}
        )
        self.assertEqual("recover_until", recovery_action["type"])
        self.assertEqual("recovering", snapshot["objective"]["status"])
        self.assertEqual("risk_threshold_exceeded", decision["reason"])

        snapshot, resumed_action, _ = step(
            snapshot,
            {
                "type": "recovery_observation",
                "action_id": recovery_action["id"],
                "readiness": snapshot["actor"]["profile"]["recovery_threshold"],
            },
        )
        self.assertEqual("hunt", snapshot["objective"]["phase"])
        self.assertEqual("engage", resumed_action["type"])

        snapshot, death_recovery_action, decision = step(
            snapshot, {"type": "death", "observation_id": 2}
        )
        self.assertEqual("recover_until", death_recovery_action["type"])
        self.assertEqual("death_interrupted", decision["reason"])
        self.assertEqual("hunt", snapshot["objective"]["phase"])

    def test_actor_interruption_recovers_without_spending_action_retries(self):
        snapshot = make_snapshot("cautious")
        snapshot, action, _ = step(snapshot, {"type": "decide"})

        snapshot, recovery_action, decision = step(
            snapshot,
            {
                "type": "action_outcome",
                "action_id": action["id"],
                "outcome": "interrupted",
                "observation_id": 1,
            },
        )

        self.assertEqual("recovering", snapshot["objective"]["status"])
        self.assertEqual(0, snapshot["objective"]["failures"])
        self.assertEqual(1, snapshot["objective"]["viability_spent"])
        self.assertEqual("recover_until", recovery_action["type"])
        self.assertEqual("actor_interrupted", decision["reason"])

    def test_recovery_resumes_replanning_without_emitting_an_action(self):
        snapshot = make_snapshot("cautious")
        snapshot, action, _ = step(snapshot, {"type": "decide"})
        snapshot, _, _ = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "failed"},
        )
        exhausted_request_id = snapshot["objective"]["replan_request"]["id"]
        snapshot, recovery_action, _ = step(
            snapshot,
            {"type": "interruption", "observation_id": 1, "reason": "death"},
        )
        refreshed_request = copy.deepcopy(snapshot["objective"]["replan_request"])
        self.assertNotEqual(exhausted_request_id, refreshed_request["id"])
        self.assertEqual(snapshot["objective"]["action_generation"], refreshed_request["generation"])

        snapshot, action, decision = step(
            snapshot,
            {
                "type": "recovery_observation",
                "action_id": recovery_action["id"],
                "readiness": snapshot["actor"]["profile"]["recovery_threshold"],
            },
        )

        self.assertEqual("replanning", snapshot["objective"]["status"])
        self.assertIsNone(action)
        self.assertEqual("recovery_complete_awaiting_replacement", decision["reason"])

        snapshot, action, decision = step(
            snapshot,
            {
                "type": "replan",
                "request_id": refreshed_request["id"],
                "payload": {"checkpoint": "safe-route"},
            },
        )
        self.assertEqual("move_to", action["type"])
        self.assertEqual("replacement_plan_observed", decision["reason"])

    def test_duplicate_interruption_observation_does_not_spend_viability_twice(self):
        snapshot = make_snapshot("steady")
        snapshot, _, _ = step(snapshot, {"type": "decide"})
        death = {"type": "death", "observation_id": 7}

        snapshot, recovery_action, _ = step(snapshot, death)
        before_duplicate = copy.deepcopy(snapshot)
        snapshot, duplicate_action, decision = step(snapshot, death)

        self.assertEqual(before_duplicate, snapshot)
        self.assertEqual(1, snapshot["objective"]["viability_spent"])
        self.assertEqual(recovery_action, snapshot["pending_action"])
        self.assertIsNone(duplicate_action)
        self.assertEqual("duplicate_or_stale_interruption", decision["reason"])

    def test_duplicate_accepted_danger_observation_leaves_snapshot_unchanged(self):
        snapshot = make_snapshot("stubborn")
        danger = {"type": "danger", "observation_id": 7, "severity": 70}

        snapshot, action, decision = step(snapshot, danger)
        self.assertIsNone(action)
        self.assertEqual("risk_accepted", decision["reason"])
        before_duplicate = copy.deepcopy(snapshot)

        snapshot, action, decision = step(snapshot, danger)

        self.assertEqual(before_duplicate, snapshot)
        self.assertIsNone(action)
        self.assertEqual("duplicate_or_stale_danger", decision["reason"])

    def test_stale_readiness_cannot_complete_a_replaced_recovery_action(self):
        snapshot = make_snapshot("steady")
        snapshot, _, _ = step(snapshot, {"type": "decide"})
        snapshot, first_recovery, _ = step(
            snapshot, {"type": "death", "observation_id": 1}
        )
        snapshot, current_recovery, _ = step(
            snapshot, {"type": "danger", "observation_id": 2, "severity": 90}
        )

        snapshot, action, decision = step(
            snapshot,
            {
                "type": "recovery_observation",
                "action_id": first_recovery["id"],
                "readiness": 100,
            },
        )

        self.assertEqual("recovering", snapshot["objective"]["status"])
        self.assertEqual(current_recovery, snapshot["pending_action"])
        self.assertIsNone(action)
        self.assertEqual("stale_or_unknown_recovery", decision["reason"])

    def test_interruption_fences_late_action_attempts_and_outcomes(self):
        snapshot = make_snapshot("steady")
        snapshot, interrupted_action, _ = step(snapshot, {"type": "decide"})
        snapshot, recovery_action, _ = step(
            snapshot,
            {"type": "interruption", "observation_id": 1, "reason": "capacity_reclaimed"},
        )

        snapshot, action, attempt_decision = step(
            snapshot, {"type": "action_attempt", "action": interrupted_action}
        )
        self.assertIsNone(action)
        self.assertEqual("action_attempt_fenced", attempt_decision["reason"])

        snapshot, action, outcome_decision = step(
            snapshot,
            {
                "type": "action_outcome",
                "action_id": interrupted_action["id"],
                "outcome": "succeeded",
            },
        )
        self.assertIsNone(action)
        self.assertEqual("stale_or_unknown_outcome", outcome_decision["reason"])

        snapshot, action, recovery_decision = step(
            snapshot, {"type": "action_attempt", "action": recovery_action}
        )
        self.assertIsNone(action)
        self.assertEqual("action_attempt_allowed", recovery_decision["reason"])

    def test_replan_rejects_stale_same_phase_request(self):
        snapshot = make_snapshot("cautious")
        snapshot, action, _ = step(snapshot, {"type": "decide"})
        snapshot, _, _ = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "failed"},
        )
        old_request_id = snapshot["objective"]["replan_request"]["id"]
        snapshot, action, _ = step(
            snapshot,
            {
                "type": "replan",
                "request_id": old_request_id,
                "payload": {"checkpoint": "route-b"},
            },
        )
        snapshot, _, _ = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "failed"},
        )
        current_request = copy.deepcopy(snapshot["objective"]["replan_request"])

        snapshot, action, decision = step(
            snapshot,
            {
                "type": "replan",
                "request_id": old_request_id,
                "payload": {"checkpoint": "stale-route-a"},
            },
        )

        self.assertIsNone(action)
        self.assertEqual("replanning", snapshot["objective"]["status"])
        self.assertEqual(current_request, snapshot["objective"]["replan_request"])
        self.assertEqual("stale_or_unknown_replan", decision["reason"])

    def test_replan_rejects_delayed_cross_phase_request_and_wrong_payload_shape(self):
        snapshot = make_snapshot("cautious")
        snapshot, action, _ = step(snapshot, {"type": "decide"})
        snapshot, _, _ = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "failed"},
        )
        travel_request_id = snapshot["objective"]["replan_request"]["id"]
        snapshot, action, _ = step(
            snapshot,
            {
                "type": "replan",
                "request_id": travel_request_id,
                "payload": {"checkpoint": "route-b"},
            },
        )
        snapshot, hunt_action, _ = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "succeeded"},
        )
        snapshot, _, _ = step(
            snapshot,
            {"type": "action_outcome", "action_id": hunt_action["id"], "outcome": "failed"},
        )
        hunt_request_id = snapshot["objective"]["replan_request"]["id"]

        snapshot, action, decision = step(
            snapshot,
            {
                "type": "replan",
                "request_id": travel_request_id,
                "payload": {"checkpoint": "stale-route-a"},
            },
        )
        self.assertIsNone(action)
        self.assertEqual("stale_or_unknown_replan", decision["reason"])

        snapshot, action, decision = step(
            snapshot,
            {
                "type": "replan",
                "request_id": hunt_request_id,
                "payload": {"checkpoint": "wrong-key", "target": "also-extra"},
            },
        )
        self.assertIsNone(action)
        self.assertEqual("replacement_plan_invalid", decision["reason"])
        self.assertEqual(hunt_request_id, snapshot["objective"]["replan_request"]["id"])

    def test_replan_rejects_non_concrete_phase_payload_values(self):
        for invalid_value in (None, "", "   "):
            with self.subTest(invalid_value=invalid_value):
                snapshot = make_snapshot("cautious")
                snapshot, action, _ = step(snapshot, {"type": "decide"})
                snapshot, _, _ = step(
                    snapshot,
                    {"type": "action_outcome", "action_id": action["id"], "outcome": "failed"},
                )
                request = copy.deepcopy(snapshot["objective"]["replan_request"])

                snapshot, action, decision = step(
                    snapshot,
                    {
                        "type": "replan",
                        "request_id": request["id"],
                        "payload": {"checkpoint": invalid_value},
                    },
                )

                self.assertIsNone(action)
                self.assertEqual("replanning", snapshot["objective"]["status"])
                self.assertEqual(request, snapshot["objective"]["replan_request"])
                self.assertEqual("replacement_plan_invalid", decision["reason"])

    def test_temporary_deferral_requires_replan_without_spending_budgets(self):
        snapshot = make_snapshot("cautious")
        snapshot, action, _ = step(snapshot, {"type": "decide"})

        snapshot, next_action, decision = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "deferred"},
        )

        self.assertIsNone(next_action)
        self.assertEqual("replanning", snapshot["objective"]["status"])
        self.assertEqual(0, snapshot["objective"]["failures"])
        self.assertEqual(0, snapshot["objective"]["viability_spent"])
        self.assertEqual(action["id"], snapshot["objective"]["replan_request"]["action_id"])
        self.assertEqual(action["phase"], snapshot["objective"]["replan_request"]["phase"])
        self.assertEqual(action["generation"], snapshot["objective"]["replan_request"]["generation"])
        self.assertEqual("action_temporarily_deferred", decision["reason"])

    def test_structural_blocked_outcome_spends_retry_and_viability(self):
        snapshot = make_snapshot("cautious")
        snapshot, action, _ = step(snapshot, {"type": "decide"})

        snapshot, next_action, decision = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "blocked"},
        )

        self.assertIsNone(next_action)
        self.assertEqual(1, snapshot["objective"]["failures"])
        self.assertEqual(1, snapshot["objective"]["viability_spent"])
        self.assertEqual("action_retry_budget_exhausted", decision["reason"])

    def test_replay_is_deterministic_and_ignores_stale_outcomes(self):
        initial = make_snapshot("steady")
        inputs = [
            {"type": "decide"},
            {
                "type": "action_outcome",
                "action_id": "actor-steady:objective-1:travel:1",
                "outcome": "succeeded",
            },
            {
                "type": "action_outcome",
                "action_id": "actor-steady:objective-1:travel:1",
                "outcome": "succeeded",
            },
        ]

        first = replay(copy.deepcopy(initial), inputs)
        second = replay(copy.deepcopy(initial), inputs)

        self.assertEqual(first, second)
        final_snapshot, _, decisions = first
        self.assertEqual("hunt", final_snapshot["objective"]["phase"])
        self.assertEqual("stale_or_unknown_outcome", decisions[-1]["reason"])


if __name__ == "__main__":
    unittest.main()
