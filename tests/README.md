# Utility runner reporting

The ordinary utility suite retains its 79-function inventory and registration
order, including the order-dependent mapped-file and fixed-memory tests.
`RunSuite` still emits `EQEMU_TEST_RESULT` version 1 with exactly these seven
fields: `version`, `selected`, `started`, `completed`, `failed`, `finalized`,
`passed`. Timing does not change acceptance or introduce performance thresholds.

Each started function also emits one separate `EQEMU_TEST_TIMING` JSON line:

```text
EQEMU_TEST_TIMING {"version":1,"name":"RunnerControl::Check","status":"passed","completed":true,"elapsed_ns":1234}
```

- `name` is the registered suite and function joined by `::`, bounded to 256
  bytes before JSON escaping. Control and non-ASCII bytes are escaped so records
  remain bounded, single-line JSON. Baseline names are ASCII and fit this bound.
- `elapsed_ns` is a nonnegative `std::chrono::steady_clock` duration measured
  from `test_start` (before setup) through `test_end` (after teardown), independent
  of the framework's existing body-only text duration.
- `status` is `passed` or `failed`. Assertions and caught body exceptions report
  failure with `completed:true`, matching the framework's `test_end` behavior.
- Escaping setup or teardown exceptions report failure with `completed:false`
  and elapsed time up to the runner catch. They stop the run as before; no
  completed or failed summary counts are invented for the interrupted function.

Timing records have their own prefix and do not extend the summary schema.
Consumers of only `EQEMU_TEST_RESULT` should continue to ignore other output.
These durations identify expensive functions; they are not performance claims.

## Focused controls

Two opt-in, non-installed targets link only the small existing test framework:

```sh
cmake --build <build-dir> --target tests_runner_controls tests_reporting_controls
<build-dir>/bin/tests_reporting_controls
<build-dir>/bin/tests_runner_controls pass
```

`tests_runner_controls` preserves the five original modes and outcomes:
`pass` exits 0; `fail`, `empty`, `setup-exception`, and `body-exception` exit 1.
The additional `teardown-exception` mode exits 1 with the same incomplete summary
shape as `setup-exception` (selected/started 1, completed/failed 0, not finalized).
Invalid arguments still exit 2.

`tests_reporting_controls` checks qualified attribution across two suites,
pass/assertion/body/setup/teardown status and exact summary output, partial
completion, single-record emission, bounded JSON-escaped names, and duration
coverage of an interval sampled inside setup and teardown. It uses no sleeps,
fixture reductions, or timing thresholds. These checks do not run the ordinary
utility fixtures or replace the caller-owned build-unit-v1 producer/consumer
validation and owned teardown.
