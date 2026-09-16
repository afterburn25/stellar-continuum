"""Native ECONOMY panel validation against a real campaign.

The validator loads the authored Player17 fixture and drives the packaged
native client through the treasury flow: the top-rail ECONOMY button opens the
toggleable panel, the third INDUSTRIAL PRIORITY toggle issues a
Shipbuilding-first order through the real dispatch path, and the smoke
captures the rendered treasury (six cards, cash-flow rows) plus an
``economy={...}`` diagnostic that must show a ready view, all nine flow rows
and the persisted priority change.
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
    match = re.search(r"(?:^|\s)economy=(\{[^{}]*\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native economy smoke did not report its evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native economy diagnostic is malformed") from error
    for key in ("panel", "ready", "cards", "rows", "priority", "toggled"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, int):
            raise RuntimeError(f"Native economy reported invalid {key}")
    if state["panel"] != 1:
        raise RuntimeError("Native economy panel did not open")
    if state["ready"] != 1 or state["cards"] != 6 or state["rows"] != 9:
        raise RuntimeError(
            "Native economy did not build the full treasury view")
    if state["priority"] != 2 or state["toggled"] != 1:
        raise RuntimeError(
            "Native economy did not apply the Shipbuilding-first order")
    return state


def _capture(path: Path) -> None:
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"Native economy did not capture a frame: {path}")
    if min(data[54:]) == max(data[54:]):
        raise RuntimeError("Native economy capture contains no rendered variation")


def _persisted_priority(save: Path) -> int:
    payload = json.loads(save.read_text(encoding="utf-8"))
    economies = payload.get("Galaxy", {}).get("Economies", [])
    player = payload.get("Galaxy", {}).get("PlayerCivilizationId")
    matches = [row for row in economies
               if row.get("CivilizationId") == player]
    if len(matches) != 1:
        raise RuntimeError("Economy smoke save lost its player economy")
    priority = matches[0].get("IndustryPriority")
    if isinstance(priority, bool) or not isinstance(priority, int):
        raise RuntimeError(
            "Economy smoke save did not persist the industry priority")
    return priority


def validate_native_economy_export(folder: Path, env: dict[str, str],
                                   player17_fixture: Path):
    source = _source_row(player17_fixture)
    systems = source.get("Galaxy", {}).get("Systems", [])
    if source.get("FormatVersion") != 17 or not systems:
        raise RuntimeError("Economy source row is not a Player17 campaign")
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") +
                     os.pathsep + str(system_root))
    captures, diagnostics = [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-economy-") as temporary:
        work = Path(temporary)
        save = work / "economy.player17.json"
        save.write_text(json.dumps(source, ensure_ascii=False),
                        encoding="utf-8")
        for replay in (False, True):
            capture = work / ("economy-loaded.bmp" if replay else
                              "economy-ordered.bmp")
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--load", "--economy-smoke", str(capture)]
            result = subprocess.run(args, cwd=work, env=clean_env,
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="strict",
                                    timeout=120)
            if result.returncode != 0:
                raise RuntimeError(
                    f"Native economy smoke failed ({result.returncode}):\n"
                    f"{result.stdout}\n{result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", f"systems={len(systems)} ",
                    "save=ok ")):
                raise RuntimeError(
                    "Native economy did not confirm Vulkan, campaign and save")
            _diagnostic(result.stdout)
            _capture(capture)
            if not save.is_file():
                raise RuntimeError(
                    "Economy smoke did not write its isolated save")
            payload = json.loads(save.read_text(encoding="utf-8"))
            days = payload.get("SimulationDays")
            if (payload.get("FormatVersion") != 17 or
                    len(payload.get("Galaxy", {}).get("Systems", [])) !=
                    len(systems) or
                    isinstance(days, bool) or
                    not isinstance(days, (int, float)) or
                    not math.isfinite(days)):
                raise RuntimeError(
                    "Economy smoke damaged the campaign payload")
            if _persisted_priority(save) != 2:
                raise RuntimeError(
                    "Economy smoke did not round-trip the stored priority")
            evidence = folder.parent / f"{folder.name}-{capture.name}"
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
    return {"nativeEconomyPanel": True,
            "nativeEconomyPriorityOrder": True,
            "economyCaptures": captures,
            "economyDiagnostics": diagnostics}
