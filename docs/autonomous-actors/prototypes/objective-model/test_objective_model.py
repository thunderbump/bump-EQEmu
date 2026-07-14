import copy
import unittest

from objective_model import make_snapshot, replay, step


class ObjectiveModelTest(unittest.TestCase):
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
        self.assertEqual("action_retry_budget_exhausted", decision["reason"])

        snapshot, replacement_action, decision = step(
            snapshot,
            {"type": "replan", "payload": {"target": "easier-allowlisted-mob"}},
        )
        self.assertEqual("active", snapshot["objective"]["status"])
        self.assertEqual("easier-allowlisted-mob", replacement_action["payload"]["target"])
        self.assertEqual("replacement_plan_observed", decision["reason"])

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
            {"type": "replan", "payload": {"checkpoint": "alternate-route"}},
        )
        snapshot, next_action, decision = step(
            snapshot,
            {"type": "action_outcome", "action_id": action["id"], "outcome": "failed"},
        )

        self.assertIsNone(next_action)
        self.assertEqual("abandoned", snapshot["objective"]["status"])
        self.assertEqual(2, snapshot["objective"]["viability_spent"])
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
            snapshot, {"type": "danger", "severity": 70}
        )
        self.assertEqual("recover_until", recovery_action["type"])
        self.assertEqual("recovering", snapshot["objective"]["status"])
        self.assertEqual("risk_threshold_exceeded", decision["reason"])

        snapshot, resumed_action, _ = step(
            snapshot,
            {
                "type": "recovery_observation",
                "readiness": snapshot["actor"]["profile"]["recovery_threshold"],
            },
        )
        self.assertEqual("hunt", snapshot["objective"]["phase"])
        self.assertEqual("engage", resumed_action["type"])

        snapshot, death_recovery_action, decision = step(snapshot, {"type": "death"})
        self.assertEqual("recover_until", death_recovery_action["type"])
        self.assertEqual("death_interrupted", decision["reason"])
        self.assertEqual("hunt", snapshot["objective"]["phase"])

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
