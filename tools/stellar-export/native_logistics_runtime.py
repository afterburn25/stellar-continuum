"""Native SUPPLY NETWORK panel validation against a real campaign.

The validator loads the authored Player17 fixture and drives the packaged
native client through the logistics flow: the top-rail SUPPLY button opens the
toggleable panel, the smoke captures the rendered home-system network (metric
tiles, guidance, node rows) and reports a ``logistics={...}`` diagnostic that
must show a ready network with at least one node row.
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

from native_fleet_runtime import _source_row


def _diagnostic(stdout: str) -> dict:
    match = re.search(r"(?:^|\s)logistics=(\{[^{}]*\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native logistics smoke did not report its evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native logistics diagnostic is malformed") from error
    for key in ("panel", "ready", "nodes", "corridors"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, int):
            raise RuntimeError(f"Native logistics reported invalid {key}")
    for key in ("supply", "demand", "delivered", "shortfall"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or \
                not math.isfinite(value):
            raise RuntimeError(f"Native logistics reported invalid {key}")
    if state["panel"] != 1:
        raise RuntimeError("Native logistics panel did not open")
    if state["ready"] != 1 or state["nodes"] < 1:
        raise RuntimeError(
            "Native logistics did not build the home-system network")
    return state


def _capture(path: Path) -> None:
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"Native logistics did not capture a frame: {path}")
    if min(data[54:]) == max(data[54:]):
        raise RuntimeError("Native logistics capture contains no rendered variation")


def validate_native_logistics_export(folder: Path, env: dict[str, str],
                                     player17_fixture: Path):
    source = _source_row(player17_fixture)
    systems = source.get("Galaxy", {}).get("Systems", [])
    if source.get("FormatVersion") != 17 or not systems:
        raise RuntimeError("Logistics source row is not a Player17 campaign")
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") +
                     os.pathsep + str(system_root))
    captures, diagnostics = [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-logistics-") as temporary:
        work = Path(temporary)
        save = work / "logistics.player17.json"
        save.write_text(json.dumps(source, ensure_ascii=False),
                        encoding="utf-8")
        for replay in (False, True):
            capture = work / ("logistics-loaded.bmp" if replay else
                              "logistics-ordered.bmp")
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--load", "--logistics-smoke", str(capture)]
            result = subprocess.run(args, cwd=work, env=clean_env,
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="strict",
                                    timeout=120)
            if result.returncode != 0:
                raise RuntimeError(
                    f"Native logistics smoke failed ({result.returncode}):\n"
                    f"{result.stdout}\n{result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", f"systems={len(systems)} ",
                    "save=ok ")):
                raise RuntimeError(
                    "Native logistics did not confirm Vulkan, campaign and save")
            _diagnostic(result.stdout)
            _capture(capture)
            if not save.is_file():
                raise RuntimeError(
                    "Logistics smoke did not write its isolated save")
            payload = json.loads(save.read_text(encoding="utf-8"))
            days = payload.get("SimulationDays")
            if (payload.get("FormatVersion") != 17 or
                    len(payload.get("Galaxy", {}).get("Systems", [])) !=
                    len(systems) or
                    isinstance(days, bool) or
                    not isinstance(days, (int, float)) or
                    not math.isfinite(days)):
                raise RuntimeError(
                    "Logistics smoke damaged the campaign payload")
            evidence = folder.parent / f"{folder.name}-{capture.name}"
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
    return {"nativeLogisticsPanel": True,
            "nativeLogisticsHomeNetwork": True,
            "logisticsCaptures": captures,
            "logisticsDiagnostics": diagnostics}
