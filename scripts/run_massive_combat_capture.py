#!/usr/bin/env python3
"""Run the maintained native massive-combat renderer capture with bounded logs."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--godot", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--visible", action="store_true", help="Start as a visible, restorable 720p window.")
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    tracked = subprocess.check_output(["git", "status", "--short", "--untracked-files=no"], cwd=repo, text=True)
    command = [
        str(args.godot.resolve()), "--path", str(repo),
        "--resolution", "1280x720", "--windowed",
        "res://tools/MassiveCombatCapture.tscn",
    ]
    if args.visible:
        command[command.index("--resolution"):command.index("--resolution")] = ["--position", "100,100"]
    else:
        command[command.index("--resolution"):command.index("--resolution")] = ["--position", "5000,5000"]
    environment = os.environ.copy()
    isolated_profile = output / "isolated-profile"
    (isolated_profile / "Roaming").mkdir(parents=True, exist_ok=True)
    (isolated_profile / "Local").mkdir(parents=True, exist_ok=True)
    environment["APPDATA"] = str(isolated_profile / "Roaming")
    environment["LOCALAPPDATA"] = str(isolated_profile / "Local")
    environment["STELLAR_MASSIVE_CAPTURE_DIR"] = str(output)
    environment["STELLAR_SOURCE_REVISION"] = revision
    environment["STELLAR_CAPTURE_RESOLUTION"] = "1280x720"
    if args.visible:
        environment["STELLAR_CAPTURE_VISIBLE"] = "1"
    else:
        environment.pop("STELLAR_CAPTURE_VISIBLE", None)
    started = datetime.now(timezone.utc).isoformat()
    try:
        completed = subprocess.run(command, cwd=repo, env=environment, text=True,
                                   capture_output=True, timeout=args.timeout)
        exit_code = completed.returncode
        stdout, stderr = completed.stdout, completed.stderr
    except subprocess.TimeoutExpired as error:
        exit_code = 124
        stdout = error.stdout or ""
        stderr = (error.stderr or "") + f"\nCapture exceeded {args.timeout} seconds.\n"
    (output / "godot-capture.log").write_text(stdout, encoding="utf-8")
    (output / "godot-stderr.log").write_text(stderr, encoding="utf-8")
    (output / "exit-code.txt").write_text(str(exit_code) + "\n", encoding="ascii")
    (output / "source-revision.txt").write_text(revision + "\n", encoding="ascii")
    receipt = {
        "schema": "stellar-massive-combat-run-v1",
        "sourceRevision": revision,
        "startedUtc": started,
        "finishedUtc": datetime.now(timezone.utc).isoformat(),
        "mode": "visible" if args.visible else "offscreen",
        "timeoutSeconds": args.timeout,
        "exitCode": exit_code,
        "trackedChangesAtLaunch": tracked.splitlines(),
        "command": command,
    }
    (output / "run-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    sys.stdout.write(stdout)
    sys.stderr.write(stderr)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
