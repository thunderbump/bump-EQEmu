# Runtime Proof Launch

Use this path when a client-visible smoke test needs the persistent gameplay EQEmu runtime to stay available after
idle. It is intentionally scoped to the gameplay AkkStack checkout at `../bump-akk-stack`, not the validation
stack.

```sh
./scripts/start-akkstack-runtime-proof.sh
./scripts/start-akkstack-runtime-proof.sh --stack gameplay
./scripts/start-akkstack-runtime-proof.sh --stack validation --dry-run
./scripts/start-akkstack-runtime-proof.sh --stack gameplay --dry-run
```

The helper defaults to `--stack gameplay`. Pass `--stack validation` only when a runtime proof intentionally
targets the validation AkkStack; dry-run output labels that as a non-default runtime-proof selection.
`AKKSTACK_DIR=/path/to/stack` remains an explicit custom-path override for diagnostics; the selected role is
still printed so custom paths are not mistaken for a role default.

Use `--dry-run` before changing runtime state. It prints the selected stack role, resolved path, path source,
Compose files, `mariadb` and `eqemu-server` services, and the launcher/runtime actions that would run without
invoking Docker. Those actions include starting the services, waiting for MariaDB, using Spire launcher restart
when available, using the fallback supervised runtime when Spire is unavailable, and waiting for stable zone
capacity.

The helper:

- verifies and prints the selected AkkStack role and path for this checkout;
- starts only the AkkStack `mariadb` and `eqemu-server` services;
- uses the gameplay stack `docker-compose.local.yml` when host port `8080` is already occupied;
- restarts runtime processes through `./bin/spire spire:launcher restart` when Spire is available;
- otherwise starts `world`, `ucs`, and a dev-only supervisor that keeps five sleeping dynamic `zone` processes up;
- waits until `world` is running and the configured zone capacity is present.

Do not manually start `world` and standalone `zone` processes for runtime proofs unless you are diagnosing the
launcher itself. Manual dynamic zones can go idle and exit, which can leave the world process without an
available zone server for the next login or zone handoff. The fallback path uses a small supervisor loop instead
of one-off manual starts, so exited zone processes are replaced during the proof window.

The fallback supervisor keeps five zones by default. To choose another count:

```sh
AKKSTACK_SUPERVISED_ZONE_COUNT=10 ./scripts/start-akkstack-runtime-proof.sh
```

Loginserver credentials and address settings live in:

```text
../bump-akk-stack/server/eqemu_config.json
```

Those values are dev configuration and may contain secrets. Do not copy them into git-tracked docs or scripts.

If the helper succeeds, AFK confidence is limited to process and launcher readiness. A final client login/zoning
check is still the strongest proof that the remote login server and client-visible addresses are correct.
