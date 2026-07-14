#!/usr/bin/env python3
"""Throwaway Actor Objective decision-model prototype."""

from __future__ import annotations

import argparse
import copy
import json
from typing import Any


PROFILES = {
    "cautious": {
        "persistence": 0,
        "risk_tolerance": 35,
        "recovery_threshold": 90,
    },
    "steady": {
        "persistence": 1,
        "risk_tolerance": 60,
        "recovery_threshold": 75,
    },
    "stubborn": {
        "persistence": 2,
        "risk_tolerance": 85,
        "recovery_threshold": 60,
    },
}

PHASES = (
    {"name": "travel", "action_type": "move_to", "payload": {"checkpoint": "hunt-area"}},
    {"name": "hunt", "action_type": "engage", "payload": {"target": "allowlisted-mob"}},
    {"name": "acquire", "action_type": "loot_item", "payload": {"item": "observed-drop"}},
)


def make_snapshot(profile_name: str) -> dict[str, Any]:
    if profile_name not in PROFILES:
        raise ValueError(f"unknown profile: {profile_name}")

    return {
        "schema_version": 1,
        "actor": {
            "id": f"actor-{profile_name}",
            "strategy_version": "objective-prototype-v1",
            "seed": 17,
            "profile": copy.deepcopy(PROFILES[profile_name]),
        },
        "objective": {
            "id": "objective-1",
            "kind": "hunt-and-acquire",
            "status": "active",
            "phase": PHASES[0]["name"],
            "phase_index": 0,
            "attempt": 0,
            "failures": 0,
            "phase_payload": copy.deepcopy(PHASES[0]["payload"]),
            "viability_budget": 3,
            "viability_spent": 0,
            "interrupted_by": None,
        },
        "pending_action": None,
        "decision_sequence": 0,
    }


def _phase(snapshot: dict[str, Any]) -> dict[str, Any]:
    return PHASES[snapshot["objective"]["phase_index"]]


def _action(snapshot: dict[str, Any]) -> dict[str, Any]:
    actor = snapshot["actor"]
    objective = snapshot["objective"]
    phase = _phase(snapshot)
    return {
        "id": ":".join(
            (
                actor["id"],
                objective["id"],
                phase["name"],
                str(objective["attempt"]),
            )
        ),
        "type": phase["action_type"],
        "objective_id": objective["id"],
        "phase": phase["name"],
        "attempt": objective["attempt"],
        "payload": copy.deepcopy(objective["phase_payload"]),
    }


def _recovery_action(snapshot: dict[str, Any]) -> dict[str, Any]:
    objective = snapshot["objective"]
    return {
        "id": ":".join(
            (
                snapshot["actor"]["id"],
                objective["id"],
                "recovery",
                str(snapshot["decision_sequence"]),
            )
        ),
        "type": "recover_until",
        "objective_id": objective["id"],
        "phase": "recovery",
        "attempt": 1,
        "payload": {
            "readiness": snapshot["actor"]["profile"]["recovery_threshold"]
        },
    }


def _request_phase_action(snapshot: dict[str, Any]) -> dict[str, Any]:
    objective = snapshot["objective"]
    objective["attempt"] += 1
    action = _action(snapshot)
    snapshot["pending_action"] = action
    return action


def _decision(
    snapshot: dict[str, Any], observation: dict[str, Any], reason: str, action: dict[str, Any] | None
) -> dict[str, Any]:
    return {
        "sequence": snapshot["decision_sequence"],
        "actor_id": snapshot["actor"]["id"],
        "strategy_version": snapshot["actor"]["strategy_version"],
        "objective_id": snapshot["objective"]["id"],
        "phase": snapshot["objective"]["phase"],
        "status": snapshot["objective"]["status"],
        "observation": copy.deepcopy(observation),
        "reason": reason,
        "emitted_action_id": action["id"] if action else None,
    }


def step(
    persistent_snapshot: dict[str, Any], observation: dict[str, Any]
) -> tuple[dict[str, Any], dict[str, Any] | None, dict[str, Any]]:
    """Apply one observation and emit at most one bounded Actor Action."""
    snapshot = copy.deepcopy(persistent_snapshot)
    snapshot["decision_sequence"] += 1
    objective = snapshot["objective"]
    action = None
    reason = "observation_ignored"

    if objective["status"] in {"completed", "abandoned"}:
        reason = "objective_terminal"
    elif observation["type"] == "death":
        snapshot["pending_action"] = None
        objective["viability_spent"] += 1
        if objective["viability_spent"] > objective["viability_budget"]:
            objective["status"] = "abandoned"
            reason = "objective_viability_exhausted"
        else:
            objective["status"] = "recovering"
            objective["interrupted_by"] = "death"
            action = _recovery_action(snapshot)
            snapshot["pending_action"] = action
            reason = "death_interrupted"
    elif observation["type"] == "danger":
        severity = observation["severity"]
        if severity > snapshot["actor"]["profile"]["risk_tolerance"]:
            snapshot["pending_action"] = None
            objective["viability_spent"] += 1
            if objective["viability_spent"] > objective["viability_budget"]:
                objective["status"] = "abandoned"
                reason = "objective_viability_exhausted"
            else:
                objective["status"] = "recovering"
                objective["interrupted_by"] = "danger"
                action = _recovery_action(snapshot)
                snapshot["pending_action"] = action
                reason = "risk_threshold_exceeded"
        else:
            reason = "risk_accepted"
    elif observation["type"] == "recovery_observation":
        if objective["status"] != "recovering":
            reason = "not_recovering"
        elif observation["readiness"] < snapshot["actor"]["profile"]["recovery_threshold"]:
            reason = "recovery_incomplete"
        else:
            objective["status"] = "active"
            objective["interrupted_by"] = None
            snapshot["pending_action"] = None
            action = _request_phase_action(snapshot)
            reason = "recovery_complete"
    elif observation["type"] == "decide":
        if objective["status"] == "replanning":
            reason = "awaiting_replacement_plan"
        elif objective["status"] == "recovering":
            if snapshot["pending_action"] is None:
                action = _recovery_action(snapshot)
                snapshot["pending_action"] = action
                reason = "recovery_requested"
            else:
                reason = "awaiting_recovery"
        elif snapshot["pending_action"] is not None:
            reason = "awaiting_action_outcome"
        else:
            action = _request_phase_action(snapshot)
            reason = "action_requested"
    elif observation["type"] == "replan":
        if objective["status"] != "replanning":
            reason = "replan_not_requested"
        else:
            objective["status"] = "active"
            objective["failures"] = 0
            objective["phase_payload"] = copy.deepcopy(observation["payload"])
            action = _request_phase_action(snapshot)
            reason = "replacement_plan_observed"
    elif observation["type"] == "action_outcome":
        pending = snapshot["pending_action"]
        if (
            objective["status"] != "active"
            or pending is None
            or observation["action_id"] != pending["id"]
        ):
            reason = "stale_or_unknown_outcome"
        elif observation["outcome"] == "succeeded":
            snapshot["pending_action"] = None
            objective["failures"] = 0
            objective["attempt"] = 0
            objective["phase_index"] += 1
            if objective["phase_index"] == len(PHASES):
                objective["status"] = "completed"
                objective["phase"] = "complete"
                reason = "objective_postcondition_observed"
            else:
                objective["phase"] = _phase(snapshot)["name"]
                objective["phase_payload"] = copy.deepcopy(_phase(snapshot)["payload"])
                action = _request_phase_action(snapshot)
                reason = "postcondition_observed"
        elif observation["outcome"] in {"blocked", "expired", "failed", "interrupted"}:
            snapshot["pending_action"] = None
            objective["failures"] += 1
            if objective["failures"] > snapshot["actor"]["profile"]["persistence"]:
                objective["viability_spent"] += 1
                if objective["viability_spent"] > objective["viability_budget"]:
                    objective["status"] = "abandoned"
                    reason = "objective_viability_exhausted"
                else:
                    objective["status"] = "replanning"
                    reason = "action_retry_budget_exhausted"
            else:
                action = _request_phase_action(snapshot)
                reason = "retrying"
        else:
            reason = "unknown_outcome"

    return snapshot, action, _decision(snapshot, observation, reason, action)


def replay(
    initial_snapshot: dict[str, Any], observations: list[dict[str, Any]]
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    snapshot = copy.deepcopy(initial_snapshot)
    actions = []
    decisions = []
    for observation in observations:
        snapshot, action, decision = step(snapshot, observation)
        if action:
            actions.append(action)
        decisions.append(decision)
    return snapshot, actions, decisions


def _command_observation(command: str, snapshot: dict[str, Any]) -> dict[str, Any] | None:
    words = command.split()
    if not words:
        return None
    if words[0] == "decide":
        return {"type": "decide"}
    if words[0] in {"success", "failed", "blocked", "expired", "interrupted"}:
        pending = snapshot["pending_action"]
        if pending is None:
            raise ValueError("there is no pending action")
        outcome = "succeeded" if words[0] == "success" else words[0]
        return {"type": "action_outcome", "action_id": pending["id"], "outcome": outcome}
    if words[0] == "danger" and len(words) == 2:
        return {"type": "danger", "severity": int(words[1])}
    if words[0] == "death":
        return {"type": "death"}
    if words[0] == "replan" and len(words) == 2:
        payload_key = {
            "travel": "checkpoint",
            "hunt": "target",
            "acquire": "item",
        }[snapshot["objective"]["phase"]]
        return {"type": "replan", "payload": {payload_key: words[1]}}
    if words[0] == "ready" and len(words) == 2:
        return {"type": "recovery_observation", "readiness": int(words[1])}
    raise ValueError("unknown command")


def _print_turn(snapshot: dict[str, Any], action: dict[str, Any] | None, decision: dict[str, Any]) -> None:
    print(json.dumps({"decision": decision, "action": action, "snapshot": snapshot}, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", nargs="?", choices=PROFILES, default="steady")
    args = parser.parse_args()

    initial = make_snapshot(args.profile)
    snapshot = copy.deepcopy(initial)
    observations: list[dict[str, Any]] = []
    print("Commands: decide, success, failed, blocked, expired, interrupted, replan NAME, danger N, death, ready N, show, replay, quit")
    print(json.dumps(snapshot, indent=2))

    while True:
        try:
            command = input("actor> ").strip()
        except EOFError:
            break
        if command in {"quit", "exit"}:
            break
        if command == "show":
            print(json.dumps(snapshot, indent=2))
            continue
        if command == "replay":
            replayed, actions, decisions = replay(initial, observations)
            print(json.dumps({"matches": replayed == snapshot, "actions": actions, "decisions": decisions}, indent=2))
            continue
        try:
            observation = _command_observation(command, snapshot)
            if observation is None:
                continue
            observations.append(observation)
            snapshot, action, decision = step(snapshot, observation)
            _print_turn(snapshot, action, decision)
        except (KeyError, TypeError, ValueError) as error:
            print(f"error: {error}")


if __name__ == "__main__":
    main()
