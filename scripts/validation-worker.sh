#!/usr/bin/env bash
set -euo pipefail
[[ "${1:-}" == "run" ]]
[[ "${2:-}" == "--request" ]]
python3 - "$3" <<'PY'
import json
import sys
from pathlib import Path

request = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
evidence_dir = Path(request["evidence_dir"])
evidence_dir.mkdir(parents=True, exist_ok=True)
(evidence_dir / "result.json").write_text(
    json.dumps(
        {
            "profile": request["profile"],
            "status": "pass",
            "repo": request["repo"],
            "checkout": {
                "requestedRef": request["ref"],
                "requestedCommit": request["commit"],
                "resolvedCommit": request["commit"],
            },
            "steps": [],
        }
    ),
    encoding="utf-8",
)
PY
