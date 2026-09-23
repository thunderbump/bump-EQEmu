#!/usr/bin/env python3
"""Read the rehearsal's required database targets from the Candidate header."""
import re
import sys
from pathlib import Path


def target_versions(header):
    values = []
    for name in ("CURRENT_BINARY_DATABASE_VERSION", "CURRENT_BINARY_BOTS_DATABASE_VERSION",
                 "CUSTOM_BINARY_DATABASE_VERSION"):
        matches = re.findall(r"^\s*#define\s+" + name + r"\s+([0-9]+)\s*$", header, re.MULTILINE)
        if len(matches) != 1:
            raise ValueError(f"expected one numeric definition for {name}")
        values.append(str(int(matches[0])))
    return ":".join(values)


if __name__ == "__main__":
    try:
        print(target_versions(Path(sys.argv[1]).read_text()))
    except (OSError, ValueError) as error:
        sys.exit(f"error: cannot read Candidate database targets: {error}")
