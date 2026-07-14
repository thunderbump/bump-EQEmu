# Actor Objective And Profile Decision Model

Date: 2026-07-14

Wayfinder ticket: `central-dcq7.7`

## Accepted answer

The smallest useful decision seam is a pure deterministic transition:

```text
step(persistent snapshot, one bounded observation or outcome)
  -> updated snapshot, zero-or-one Actor Action, decision record
```

The stable **Actor Profile** needs only three objective-mechanics controls in the first contract:

- persistence: how many times to retry one concrete action plan;
- risk tolerance: when observed danger interrupts the current action;
- recovery threshold: how ready the actor must be before resuming.

Identity, strategy version, and deterministic seed are durable metadata rather than personality traits. Preferences,
sociability, chatter, and economic behavior should remain separate strategy facets so the first objective runner does
not become the owner of every actor behavior.

## Objective state

An **Actor Objective** owns its execution state separately from the Actor Profile. The prototype proved five useful
states:

- `active`: may emit one bounded action and wait for its correlated outcome;
- `recovering`: preserves objective phase and whether execution should resume as `active` or `replanning` while waiting
  for readiness;
- `replanning`: emits no gameplay action until a fresh bounded replacement plan is observed;
- `completed`: terminal because the objective postcondition was observed;
- `abandoned`: terminal because the broader objective is no longer viable.

Action acceptance is not success. A phase advances only after the expected correlated outcome or checked postcondition.
Stale outcomes are ignored.

## Retry, replanning, and viability

Two limits answer different questions:

1. The **Action Retry Budget** asks whether one concrete checkpoint, target, item, or action strategy is working.
2. The **Objective Viability Allowance** asks whether the broader objective remains worth pursuing across replans,
   danger interruptions, and death recovery.

Exhausting the Action Retry Budget clears the concrete phase payload and enters `replanning`. No phase action can be
emitted without a payload. A fresh observation must supply a replacement before another action is emitted. This
prevents an actor from fixating on a target that is too difficult.

Danger and death preserve the current phase and do not consume the Action Retry Budget because they do not necessarily
prove the concrete strategy was bad. They do consume Objective Viability. Exhausting Objective Viability abandons the
broader objective, preventing infinite recover-and-resume loops.

Actor-level interruption, including the public `interrupted` outcome, follows the same recovery/viability path rather
than being classified as action failure. Recovery remembers whether it interrupted active execution or replanning; a
replanning objective returns to `replanning` and emits nothing until it receives a replacement plan.

Temporary deferrals such as player contention should not automatically consume the Action Retry Budget. Structural or
terminal failures such as no route, an expired action, or a failed engagement should. The exact outcome classification
belongs in each live action contract.

## Deterministic replay

Replay requires the initial snapshot, ordered bounded observations/outcomes, strategy version, and seed. The decision
record exposes the actor, objective, phase, state, observation, reason, and emitted action identity. Production storage
is deliberately not selected by this prototype.

Interruption observations carry monotonically increasing IDs. The persistent snapshot keeps a watermark so duplicate
or stale danger, death, and interruption delivery cannot spend Objective Viability twice. A readiness observation must
name the current recovery action; stale readiness cannot finish a replaced recovery.

Every emitted action also carries the objective's current action generation. An interruption atomically increments the
generation and clears the pending action before emitting recovery, fencing every older action attempt. An executor must
check an attempted action against the snapshot's pending action and generation; late attempts and outcomes from the
interrupted generation are rejected. This is the prototype's deterministic cancellation barrier and preserves the
zero-or-one action seam.

## Evidence and remaining decisions

The disposable simulator is in [`prototypes/objective-model/`](prototypes/objective-model/). Twelve behavior tests cover
bounded emission, observed phase progress, profile differences, danger/death recovery, replacement-plan enforcement,
viability exhaustion, recovery resume state, interruption deduplication and fencing, stale readiness and outcomes, and
deterministic replay. Interactive failure, replanning, replacement, and replay matched exactly.

This result does not choose production persistence, tune numeric values, select replacement targets, or define the
first objective hierarchy. Those decisions follow in `central-dcq7.8` using this state model and the mapped gameplay
seams.
