#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$repo_root"

if ! command -v graphify >/dev/null 2>&1; then
	echo "graphify is not installed; skipping deterministic graph update." >&2
	exit 0
fi

if [[ ! -f graphify-out/graph.json ]]; then
	echo "graphify-out/graph.json is missing; skipping deterministic graph update." >&2
	exit 0
fi

graphify update "$repo_root" --no-cluster
