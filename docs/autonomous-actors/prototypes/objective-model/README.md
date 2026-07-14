# Actor Objective Decision-Model Prototype

This is a disposable logic prototype for Wayfinder ticket `central-dcq7.7`. It asks whether a small persistent Actor
Profile and a pure one-observation transition can produce understandable objective behavior before any production
runtime or database design is selected.

The public seam is:

```text
step(actor_snapshot, observation_or_outcome)
  -> updated_snapshot, zero_or_one_actor_action, decision_record
```

Three exaggerated profiles make the behavior easy to compare:

| Profile | Persistence | Risk tolerance | Recovery threshold |
| --- | ---: | ---: | ---: |
| cautious | 0 | 35 | 90 |
| steady | 1 | 60 | 75 |
| stubborn | 2 | 85 | 60 |

These values are prototype policy, not proposed production tuning. The seed and strategy version are durable replay
metadata, not personality traits. Objective execution state remains separate from the stable profile.

Run one profile from this directory:

```sh
python3 objective_model.py cautious
```

Useful comparison:

1. Enter `decide`, then `failed` for each profile. Cautious requests a replacement plan immediately; steady and
   stubborn retry the current action first.
2. Enter `danger 70`. Cautious and steady recover; stubborn accepts the risk.
3. After recovery begins, enter `ready 80`. Steady resumes; cautious waits for `ready 90`.
4. Enter `replay` at any point to rebuild the state from the original snapshot and recorded observations.

When a profile exhausts its action retry budget, enter `replan replacement-name`. The old checkpoint, target, or item is
cleared and no action is emitted until the replacement payload receives a fresh action attempt. Replanning and actor
interruption recovery consume a separate objective viability allowance; exhausting it abandons the broader objective
instead of looping forever. Recovery returns to the interrupted `active` or `replanning` state rather than assuming an
action is ready.

Interruption observations use an ordered ID watermark so duplicate delivery is harmless. They also advance an action
generation before recovery is emitted. Older action attempts are fenced, older outcomes are stale, and readiness must
correlate to the current recovery action. The snapshot transition still emits at most one action.

Other commands are `success`, `blocked`, `expired`, `interrupted`, `death`, `show`, and `quit`. Every transition prints
the observation, decision reason, emitted action, and full persistent snapshot.

Run the focused behavior checks with:

```sh
python3 -m unittest -v test_objective_model.py
```

This prototype does not touch EQEmu runtime code, world data, databases, zone topology, or deployment configuration.
