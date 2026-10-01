"""Live native quick-find palette proof on a loaded campaign."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_client_runtime import _validate_capture
from native_new_game_runtime import _source_payload
from native_support_runtime import _clean_environment


def _proof(stdout: str) -> dict:
    matches = re.findall(r"quick_find=(\{[^{}]*\})", stdout)
    if len(matches) != 1:
        raise RuntimeError("Quick-find smoke did not report one palette state")
    try:
        value = json.loads(matches[0])
    except (ValueError, TypeError) as error:
        raise RuntimeError("Quick-find evidence is malformed") from error
    if set(value) != {"opened", "entries", "matches", "query_length", "highlighted"}:
        raise RuntimeError("Quick-find evidence schema changed")
    if value["opened"] is not True:
        raise RuntimeError("Quick-find palette did not open on the Ctrl+K chord")
    if any(type(value[key]) is not int
           for key in ("entries", "matches", "query_length", "highlighted")):
        raise RuntimeError("Quick-find palette counts are not integers")
    if (value["entries"] <= 0 or value["matches"] <= 0 or
            value["matches"] > value["entries"] or value["query_length"] != 3 or
            not 0 <= value["highlighted"] < value["matches"]):
        raise RuntimeError("Quick-find palette did not filter and highlight real entries")
    return value


def validate_native_quick_find_export(folder: Path, env: dict[str, str], fixture: Path):
    folder = folder.resolve()
    captures = []
    with tempfile.TemporaryDirectory(prefix="stellar-native-quick-find-") as temporary:
        work = Path(temporary)
        save = work / "campaign.player17.json"
        save.write_text(json.dumps(_source_payload(fixture), ensure_ascii=False),
                        encoding="utf-8")
        capture = work / "quick-find-1280x720.bmp"
        args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                "--save-path", str(save), "--load", "--width", "1280", "--height", "720",
                "--windowed", "--quick-find-smoke", str(capture)]
        result = subprocess.run(args, cwd=work, env=_clean_environment(env),
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=120)
        log = folder.parent / (folder.name + "-quick-find.stdout.txt")
        log.write_text(result.stdout + "\nSTDERR\n" + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(
                f"Native quick-find smoke failed ({result.returncode}): {result.stderr}")
        proof = _proof(result.stdout)
        _validate_capture(capture, 1280, 720)
        destination = folder.parent / (folder.name + "-quick-find-1280x720.bmp")
        shutil.copy2(capture, destination)
        captures.append(str(destination))
    return {"nativeQuickFind": True, "quickFindProof": proof,
            "quickFindCaptures": captures, "quickFindDiagnostics": str(log)}
