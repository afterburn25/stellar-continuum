"""Local fleet movement and lane browsing through the exported native client."""
from __future__ import annotations

import copy
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_fleet_runtime import _source_row, _fleet, _diagnostic


def _finite(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise RuntimeError(f"Native local travel has invalid {label}")
    return value


def _state(stdout, moving=True):
    match = re.search(r"(?:^|\s)system_travel=(\{[^\n]+\})\s*$", stdout)
    if not match:
        raise RuntimeError("Native local travel did not report its movement and navigation evidence")
    try:
        state = json.loads(match.group(1))
    except (ValueError, TypeError) as error:
        raise RuntimeError("Native local travel diagnostic is malformed") from error
    required = ("selected", "paused_stable",
                "pause_retained", "known_arrow", "unknown_denied", "knowledge_unchanged",
                "lanes_connected")
    if any(state.get(key) is not True for key in required):
        raise RuntimeError("Native local travel did not prove input, connected lanes, movement and pause")
    if state.get("canonical_moved") is not moving or state.get("rendered_moved") is not moving:
        raise RuntimeError("Native local travel did not prove the expected motion or paused state")
    for key in ("fleet_id", "system_id", "destination_id", "order_revision"):
        if type(state.get(key)) is not int or state[key] < 0:
            raise RuntimeError(f"Native local travel has invalid {key}")
    for key in ("before_x", "before_y", "after_x", "after_y", "before_days", "after_days"):
        _finite(state.get(key), key)
    if moving and (state["after_days"] <= state["before_days"] or
            (state["before_x"], state["before_y"]) == (state["after_x"], state["after_y"])):
        raise RuntimeError("Native local travel did not advance actual time and local position")
    if not moving and (state["after_days"] != state["before_days"] or
            (state["before_x"], state["before_y"]) != (state["after_x"], state["after_y"])):
        raise RuntimeError("Native paused local travel changed time or position")
    return state


def _normalized(payload):
    result = copy.deepcopy(payload)
    result.pop("SavedAtUtc", None)
    return result


def validate_native_system_travel_export(folder: Path, env: dict[str, str], fixture: Path):
    source = _source_row(fixture)
    count = len(source.get("Galaxy", {}).get("Systems", []))
    if source.get("FormatVersion") != 17 or not count:
        raise RuntimeError("Native local travel needs an explicit Player17 source fixture")
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics = [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-local-travel-") as temporary:
        work = Path(temporary)
        save = work / "local-travel.player17.json"
        save.write_text(json.dumps(source, ensure_ascii=False), encoding="utf-8")

        def launch(mode, label, width=1280, height=720):
            capture = work / (label + ".bmp")
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--load", "--width", str(width), "--height", str(height),
                    mode, str(capture)]
            result = subprocess.run(args, cwd=work, env=clean, capture_output=True, text=True, timeout=120)
            if result.returncode != 0:
                raise RuntimeError(f"Native local travel {label} failed ({result.returncode}): {result.stderr}")
            if any(token not in result.stdout for token in ("gpu_driver=vulkan ", f"systems={count} ", "save=ok ")):
                raise RuntimeError("Native local travel did not confirm its renderer, campaign and save")
            if not capture.is_file() or capture.stat().st_size < 54 or capture.read_bytes()[:2] != b"BM":
                raise RuntimeError("Native local travel did not capture its rendered window")
            if not save.is_file():
                raise RuntimeError("Native local travel did not save the isolated campaign")
            payload = json.loads(save.read_text(encoding="utf-8"))
            if payload.get("FormatVersion") != 17 or not payload.get("SavedAtUtc"):
                raise RuntimeError("Native local travel did not save a timestamped Player17 campaign")
            if (payload.get("Galaxy", {}).get("PlayerCivilizationId") != source["Galaxy"]["PlayerCivilizationId"] or
                    len(payload.get("Galaxy", {}).get("Systems", [])) != count):
                raise RuntimeError("Native local travel changed campaign identity")
            evidence = folder.parent / f"{folder.name}-local-travel-{label}.bmp"
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
            return result.stdout, payload

        # Existing actual-input proof issues the route through Core, not a fake
        # presentation-only motion. Its source fixture is explicitly test-authored.
        prepared_stdout, prepared = launch("--fleet-smoke", "prepared")
        fleet_id, destination_id, revision, _, _, _ = _diagnostic(prepared_stdout)
        routed, player_id = _fleet(prepared, fleet_id)
        route = routed.get("PlannedRouteSystemIds")
        if (not route or routed.get("CurrentSystemId") is None or
                routed.get("DestinationSystemId") != destination_id or
                routed.get("MissionOrderRevision") != revision or
                routed.get("TransitPhase") != 1):
            raise RuntimeError("Native local travel preparation did not establish local departure")
        # Author only the first connected hop's reconnaissance so one real exit
        # can be browsed. Other destinations retain their existing unknown state.
        first_hop = route[0]
        observers = [row for row in prepared["Galaxy"].get("Knowledge", []) if row.get("CivilizationId") == player_id]
        if len(observers) != 1:
            raise RuntimeError("Native local travel source lacks a unique observer")
        observer = observers[0]
        if first_hop not in observer["KnownSystemIds"]:
            observer["KnownSystemIds"].append(first_hop)
        surveys = [row for row in observer["SystemSurveys"] if row.get("SystemId") == first_hop]
        if len(surveys) > 1:
            raise RuntimeError("Native local travel source has duplicate survey records")
        if surveys:
            surveys[0]["Level"] = max(surveys[0]["Level"], 2)
        else:
            observer["SystemSurveys"].append({"SystemId": first_hop, "Level": 2, "Progress": .35})
        save.write_text(json.dumps(prepared, ensure_ascii=False), encoding="utf-8")

        stdout, moved = launch("--system-travel-smoke", "moved")
        state = _state(stdout)
        actual, _ = _fleet(moved, fleet_id)
        if (state["fleet_id"] != fleet_id or state["system_id"] != routed["CurrentSystemId"] or
                state["destination_id"] != destination_id or state["order_revision"] != revision or
                actual.get("DestinationSystemId") != destination_id or actual.get("MissionOrderRevision") != revision):
            raise RuntimeError("Native local travel changed the selected fleet or issued an unintended order")
        for key, expected in (("before_x", routed.get("LocalTransitPositionX")),
                              ("before_y", routed.get("LocalTransitPositionY")),
                              ("after_x", actual.get("LocalTransitPositionX")),
                              ("after_y", actual.get("LocalTransitPositionY")),
                              ("before_days", prepared.get("SimulationDays")),
                              ("after_days", moved.get("SimulationDays"))):
            if not math.isclose(state[key], _finite(expected, key), rel_tol=1e-6, abs_tol=1e-6):
                raise RuntimeError("Native local travel diagnostic differs from the saved canonical state")
        reload_stdout, reloaded = launch("--system-travel-reload-smoke", "paused-reload", 1920, 1080)
        restored = _state(reload_stdout, moving=False)
        for key in ("fleet_id", "system_id", "destination_id", "order_revision"):
            if restored[key] != state[key]:
                raise RuntimeError("Native paused local travel selected a different fleet or destination")
        for before, after in (("before_x", "after_x"), ("before_y", "after_y"), ("before_days", "after_days")):
            if not math.isclose(restored[before], state[after], rel_tol=1e-6, abs_tol=1e-6):
                raise RuntimeError("Native paused local travel restored different coordinates or time")
        if _normalized(moved) != _normalized(reloaded):
            raise RuntimeError("Native local travel paused reload changed the campaign")
    return {"nativeSystemTravelInput": True, "nativeSystemLocalTransit": True,
            "nativeSystemTravelPausedReload": True, "systemTravelCaptures": captures,
            "systemTravelDiagnostics": diagnostics}
