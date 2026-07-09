# Keep advisory C++ analyzers manual for now

Advisory C++ analyzers (`clang-tidy`, `cppcheck`, and `include-what-you-use`) will stay manual-only for now. Do not move them to local pre-push hooks. If a future rollout wants shared repo-wide analyzer signal, prefer a non-blocking CI job over pre-push.

**Considered options**

- Keep analyzers manual-only and let developers run them when a touched area or bug warrants the extra pass.
- Run analyzers in local pre-push hooks so warnings appear before code leaves a workstation.
- Run analyzers in CI so the toolchain, compile database, and output surface are shared.

**Why**

- Pre-push hooks would force every contributor to install and maintain heavy analyzer tooling locally.
- `clang-tidy`, `cppcheck`, and IWYU still depend on a reliable `build/compile_commands.json`; stale or missing local build state lowers signal.
- Legacy-code false positives and analyzer runtime cost are better absorbed in shared infrastructure than on every local push.
- CI gives one reproducible environment and one place to publish advisory findings without turning them into local iteration blockers.

**Consequences**

- `.pre-commit-config.yaml` keeps advisory analyzers on the `manual` stage only.
- `clang-format` plus build/test validation remain the blocking quality gates.
- A future analyzer rollout should start with a non-blocking CI job after wrapper commands and changed-file scope keep noise acceptable.
