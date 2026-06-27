# Autonomous Actor Spikes

- `headless-client-viability-spike.md`
  - `central-lhy.5` spike on whether a synthetic headless `Client` can safely perform a bounded player-like action in the **Zone Harness** without an `EQStream` session or normal login lifecycle.
- `actor-event-perception-expansion-spike.md`
  - `central-lhy.6` spike on expanding harness **Actor Event** and **Actor Perception** coverage for bounded **Autonomous Actor** validation.
- `actor-action-event-control-plane-spike.md`
  - `central-lhy.4` spike on a lowest-impact async **Actor Action** request/ack and cursor-based **Actor Event** control plane, starting from harness HTTP and owned-bot fixtures rather than a headless `Client`.
- `actor-led-bot-party-spike.md`
  - `central-lhy.9` spike on proving the owned-bot actor-leader party shape and identifying target/leash command-source blockers.
- `actor-command-source-seam.md`
  - `central-lhy.10` implementation note for the narrow **ActorCommandSource** seam that keeps owner authority intact while allowing actor-sourced target and leash intent in bot AI.
- `actor-planner-deployment-spike.md`
  - `central-lhy.7` spike on keeping high-level **Autonomous Actor** planning out of zone ticks, recommending a same-container helper-process MVP that can later promote to a dedicated sidecar container.
- `actor-persistence-event-schema-spike.md`
  - `central-lhy.3` spike on reusing bot/social persistence where it fits while adding actor-specific profile, status, action queue, and event tables before persistent actor planning/reporting.
