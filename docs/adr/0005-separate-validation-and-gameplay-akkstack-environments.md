# Separate Validation and Gameplay AkkStack Environments

Automated validation will use a validation AkkStack checkout and validation database, separate from the gameplay AkkStack checkout and gameplay database used for client-facing runtime, manual play, and live smoke checks. This keeps test containers, DB-backed CLI checks, and **Zone Harness** scenarios from taking down or mutating the server and data used for gameplay while still allowing the validation database to stay running between test passes.

**Considered Options**

- Reuse the gameplay AkkStack environment for validation and avoid extra setup, accepting that tests may collide with ports, restart server processes, or mutate gameplay data.
- Use one AkkStack checkout with per-command port overrides and careful database backup/restore gates.
- Use separate AkkStack checkouts so Compose project state, `.env`, `server/eqemu_config.json`, volumes, and `data/mariadb` stay separate by default.

**Consequences**

- Validation wrappers should default to the validation stack, while runtime-proof and manual-client helpers should default to the gameplay stack.
- Validation `eqemu-server` processes should run as one-off validation containers by default; persistent validation `mariadb` may remain running.
- `AKKSTACK_DIR` remains an explicit escape hatch for diagnostics, but wrappers should print the resolved stack role and path before acting.
- The separate validation stack adds setup and data-prepopulation work, but it prevents routine validation from depending on or disrupting the persistent gameplay server.
