"""Native Developer-mode validation against a real campaign.

The validator loads the authored Player17 fixture and drives the packaged
native client through the reference mode switch: the pause menu's
DEVELOPMENT row checkpoints the player campaign and replaces the session
with the Developer slot, the Developer campaign resumes at the 24x Demo
speed, the DEV TOOLS menu row opens the tools panel, the grant_resources
card runs through the session command boundary, and the smoke captures the
rendered panel plus a ``developer={...}`` diagnostic. The Developer
envelope must land on ``developer-autosave.json`` beside the untouched
player save with ``ToolsUsed`` persisted.
"""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_diplomacy_runtime import _source_row


def _diagnostic(stdout: str) -> dict:
    match = re.search(r"(?:^|\s)developer=(\{[^{}]*\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native developer smoke did not report its evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native developer diagnostic is malformed") from error
    for key in ("mode", "demo_speed", "tools_panel", "command",
                "envelope_save"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, int):
            raise RuntimeError(f"Native developer reported invalid {key}")
        if value != 1:
            raise RuntimeError(
                f"Native developer smoke failed its {key} step")
    return state


def _capture(path: Path) -> None:
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"Native developer did not capture a frame: {path}")
    if min(data[54:]) == max(data[54:]):
        raise RuntimeError(
            "Native developer capture contains no rendered variation")


def _developer_envelope(save: Path) -> dict:
    envelope = save.parent / "developer-autosave.json"
    if not envelope.is_file():
        raise RuntimeError("Developer smoke wrote no developer-autosave.json")
    try:
        payload = json.loads(envelope.read_text(encoding="utf-8"))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Developer envelope is not valid JSON") from error
    if (payload.get("DeveloperFormatVersion") != 1 or
            payload.get("Mode") != "Developer"):
        raise RuntimeError("Developer envelope lost its mode markers")
    if payload.get("ToolsUsed") is not True:
        raise RuntimeError(
            "Developer envelope did not persist the ToolsUsed flag")
    campaign = payload.get("Campaign")
    if not isinstance(campaign, dict) or campaign.get("FormatVersion") != 17:
        raise RuntimeError("Developer envelope lost its Player17 payload")
    return payload


def validate_native_developer_export(folder: Path, env: dict[str, str],
                                     player17_fixture: Path):
    source = _source_row(player17_fixture)
    systems = source.get("Galaxy", {}).get("Systems", [])
    if source.get("FormatVersion") != 17 or not systems:
        raise RuntimeError("Developer source row is not a Player17 campaign")
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") +
                     os.pathsep + str(system_root))
    captures, diagnostics = [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-developer-") \
            as temporary:
        work = Path(temporary)
        save = work / "developer.player17.json"
        save.write_text(json.dumps(source, ensure_ascii=False),
                        encoding="utf-8")
        for replay in (False, True):
            capture = work / ("developer-loaded.bmp" if replay else
                              "developer-tools.bmp")
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--load", "--developer-smoke", str(capture)]
            result = subprocess.run(args, cwd=work, env=clean_env,
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="strict",
                                    timeout=120)
            if result.returncode != 0:
                raise RuntimeError(
                    f"Native developer smoke failed ({result.returncode}):\n"
                    f"{result.stdout}\n{result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", "save=ok ")):
                raise RuntimeError(
                    "Native developer did not confirm Vulkan and save")
            _diagnostic(result.stdout)
            _capture(capture)
            # The player slot must remain an ordinary Player17 campaign.
            player = json.loads(save.read_text(encoding="utf-8"))
            days = player.get("SimulationDays")
            if (player.get("FormatVersion") != 17 or
                    "Mode" in player or
                    len(player.get("Galaxy", {}).get("Systems", [])) !=
                    len(systems) or
                    isinstance(days, bool) or
                    not isinstance(days, (int, float)) or
                    not math.isfinite(days)):
                raise RuntimeError(
                    "Developer smoke damaged the player campaign payload")
            _developer_envelope(save)
            evidence = folder.parent / f"{folder.name}-{capture.name}"
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
    return {"nativeDeveloperMode": True,
            "nativeDeveloperTools": True,
            "developerCaptures": captures,
            "developerDiagnostics": diagnostics}
