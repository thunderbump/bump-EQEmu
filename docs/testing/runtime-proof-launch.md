# Runtime Proof Launch

Use this path when a client-visible smoke test needs the local dev EQEmu runtime to stay available after idle.
It is intentionally scoped to the sibling AkkStack checkout at `../bump-akk-stack`.

```sh
./scripts/start-akkstack-runtime-proof.sh
```

The helper:

- verifies the AkkStack contract for this checkout;
- starts only the AkkStack `mariadb` and `eqemu-server` services;
- uses `../bump-akk-stack/docker-compose.local.yml` when host port `8080` is already occupied;
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
