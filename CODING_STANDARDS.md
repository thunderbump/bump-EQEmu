# CODING_STANDARDS

## Scope

This document covers C/C++ source in this repository.

Primary baseline:

- C++20 (`set(CMAKE_CXX_STANDARD 20)` in top-level `CMakeLists.txt`)
- CMake build system
- `.editorconfig` defines tab indentation for C/C++ (`*.cpp`, `*.h`)

## Code style

- Use tabs for indentation in C/C++ files.
- Prefer existing local style patterns:
  - Keep changes focused and idiomatic to surrounding files.
  - Keep names explicit, avoid cryptic abbreviations.
  - Limit line length pragmatically for readability; wrap long expressions where needed.
- Do not reformat untouched blocks to avoid non-functional churn.

## Formatting (mandatory before review)

- Source formatting standard is `clang-format`.
- Formatting must run on these file patterns:
  - `*.c`, `*.cc`, `*.cpp`, `*.cxx`, `*.h`, `*.hpp`, `*.hh`, `*.hxx`
- Use the repo settings: `clang-format --style=file`.

Suggested command:

```sh
clang-format --style=file -i <changed-cpp-or-h-files>
```

## Static analysis policy

- Phase 1 (now): formatter + build validation are the required quality gates.
- Phase 2 (manual/advisory): `clang-tidy`, `cppcheck`, and `include-what-you-use`.
- Keep these advisory in CI initially; run by command or hook when `compile_commands.json` is reliable.
- Do not enforce advisory check failures as merge blockers until teams are calibrated on remaining churn.

## Install prerequisites

Recommended local install:

```sh
sudo apt-get install -y clang-format clang-tidy cppcheck include-what-you-use pre-commit
```

If pre-commit is unavailable in the environment, use local tooling only; this repo can still be validated with formatter/build/test commands below.

## Exclusions (always skip for formatter + future analyzers)

Skip generated, third-party, and build artifacts:

- `build/`
- `build-*`
- `submodules/`
- `dependencies/`
- `libs/`
- `vendor/` if present
- `graphify-out/`
- `.case/`
- `utils/**/deprecated/`

## Validation commands

Use this order for a minimum validation loop:

1. Formatting + lint-safe hygiene

```sh
git diff --check
clang-format --style=file -i <changed-cpp-or-h-files>
```

2. Configure + build (from repo root)

```sh
cmake --preset linux-debug
cmake --build build --parallel
```

3. Tests (existing project baseline)

```sh
./build/bin/tests
```

4. Manual advisory checks (when `build/compile_commands.json` exists)

```sh
clang-tidy -p build <changed-cpp-files>
cppcheck --project=build/compile_commands.json --enable=warning,performance,portability --error-exitcode=1 <changed-cpp-or-h-files>
```

Use concrete command wrappers for project-level runs to keep analyzer noise low and to keep exclusions consistent.

## Pre-commit direction

- Blocking hooks:
  - `clang-format` for tracked C/C++ files
  - lightweight hygiene checks (`trailing-whitespace`, `end-of-file-fixer`,
    `check-merge-conflict`, and case-conflict checks)
- Advisory/manual hooks:
  - `clang-tidy`
  - `cppcheck`
  - `include-what-you-use`

## Migration/rollout risks (large existing C++ repo)

- First-wave reformat churn is expected if strict formatting is applied broadly.
- Substantial existing third-party/vendor code should stay excluded to avoid vendor drift.
- `cppcheck`/`clang-tidy` can produce many false positives in legacy modules.
- IWYU and deep analyzers require consistent `compile_commands.json`; missing or stale compile databases reduce signal.
- Analyzer performance can be heavy on large touched sets; run incrementally on changed files first.

## Current rollout decisions

- Advisory analyzers remain manual-only for now. Revisit CI/pre-push enforcement
  in `central-ni6t` after collecting signal/noise from representative changes.
- Generated, third-party, deprecated, and build paths listed above are explicit
  exceptions for formatter and analyzer hooks.
- Advisory analyzer warnings are non-blocking until a future task explicitly
  promotes a check to CI or pre-push enforcement.
