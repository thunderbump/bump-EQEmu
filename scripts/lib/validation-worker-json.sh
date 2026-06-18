#!/usr/bin/env bash

json_get() {
  local request="$1" expr="$2" default_value="${3:-}"
  python3 - "$request" "$expr" "$default_value" <<'PY'
import json, sys
path, expr, default = sys.argv[1:4]
with open(path, encoding="utf-8") as f:
    data = json.load(f)
cur = data
for part in expr.split('.'):
    if not isinstance(cur, dict) or part not in cur:
        print(default)
        sys.exit(0)
    cur = cur[part]
if cur is None:
    print(default)
elif isinstance(cur, bool):
    print("true" if cur else "false")
else:
    print(cur)
PY
}

json_bool() {
  local value="$1" default_value="$2"
  case "$value" in
    true|1|yes) printf 'true\n' ;;
    false|0|no) printf 'false\n' ;;
    "") printf '%s\n' "$default_value" ;;
    *) printf 'error: expected boolean value, got %s\n' "$value" >&2; exit 2 ;;
  esac
}

json_int() {
  local value="$1" default_value="$2"
  if [[ -z "$value" ]]; then
    printf '%s\n' "$default_value"
  elif [[ "$value" =~ ^[0-9]+$ ]]; then
    printf '%s\n' "$value"
  else
    printf 'error: expected integer value, got %s\n' "$value" >&2
    exit 2
  fi
}

json_host_ports_tsv() {
  local request="$1" kind="$2"
  python3 - "$request" "$kind" <<'PY'
import json, sys

path, kind = sys.argv[1:3]
with open(path, encoding="utf-8") as f:
    data = json.load(f)

items = data.get("checks", {}).get("hostPorts", {}).get(kind, [])
for item in items:
    if isinstance(item, int):
        print(f"{kind}_{item}\t{item}\t\t")
        continue
    if not isinstance(item, dict):
        continue
    name = str(item.get("name") or f"{kind}_{item.get('port', '')}")
    port = item.get("port")
    service = str(item.get("service") or "")
    container_port = str(item.get("containerPort") or "")
    if port is None:
        continue
    print(f"{name}\t{port}\t{service}\t{container_port}")
PY
}
