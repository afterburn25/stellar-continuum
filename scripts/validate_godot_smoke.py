#!/usr/bin/env python3
"""Reject Godot's exit-zero startup failures and require real runtime initialization."""

import argparse
from pathlib import Path
import re
import sys


RUNTIME_READY = "STELLAR_RUNTIME_READY IntegratedMain"
ANSI_ESCAPE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
ERROR_LINE = re.compile(
    r"^\s*(?:(?:SCRIPT |USER |FATAL )?ERROR:|Unhandled [Ee]xception\b|"
    r"(?:[A-Za-z_]\w*\.)*[A-Za-z_]\w*Exception:)",
)


def validate_log(content: str, require_runtime_ready: bool = False) -> list[str]:
    lines = ANSI_ESCAPE.sub("", content).splitlines()
    failures = []
    if not any(line.strip() for line in lines):
        failures.append("Godot produced no output.")

    for number, line in enumerate(lines, start=1):
        if (ERROR_LINE.match(line) or "Cannot instantiate C# script" in line
                or "Scan thread aborted" in line):
            failures.append(f"Godot error at line {number}: {line.strip()}")

    if require_runtime_ready and RUNTIME_READY not in (line.strip() for line in lines):
        failures.append("IntegratedMain did not complete campaign startup and its first frame.")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--require-runtime-ready", action="store_true")
    args = parser.parse_args()
    try:
        content = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        print(f"Godot smoke failed: cannot read {args.log}: {error}", file=sys.stderr)
        return 1

    failures = validate_log(content, args.require_runtime_ready)
    if failures:
        for failure in failures:
            print(f"Godot smoke failed: {failure}", file=sys.stderr)
        return 1
    print(f"Godot smoke output validated: {args.log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
