"""Relocated native player-input fleet travel and durable reload checks."""
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


def _source_row(fixture: Path) -> dict:
    root = json.loads(fixture.read_text(encoding="utf-8"))
    rows = root.get("Rows")
    if not isinstance(rows, list):
        raise RuntimeError("Player17 fleet fixture has no source-authored rows")
    matches = [row for row in rows if row.get("Name") == "valid-current17"]
    if len(matches) != 1 or not isinstance(matches[0].get("InputJson"), str):
        raise RuntimeError("Player17 fleet fixture has no unique valid-current17 input")
    return json.loads(matches[0]["InputJson"])


def _fleet(payload: dict, fleet_id: int) -> tuple[dict, int]:
    galaxy = payload.get("Galaxy", {})
    player_id = galaxy.get("PlayerCivilizationId")
    matches = [fleet for fleet in galaxy.get("Fleets", [])
               if fleet.get("Id") == fleet_id]
    if len(matches) != 1 or matches[0].get("CivilizationId") != player_id:
        raise RuntimeError("Fleet smoke did not identify a unique player-owned fleet")
    if not matches[0].get("IsActive", False):
        raise RuntimeError("Fleet smoke selected an inactive player fleet")
    return matches[0], player_id


def _diagnostic(stdout: str) -> tuple[int, int, int, float]:
    match = re.search(r" fleet=(\d+):(\d+):(\d+):([0-9.]+)", stdout)
    if not match:
        raise RuntimeError("Native fleet did not report its selected route state")
    fleet_id, destination, revision = map(int, match.groups()[:3])
    progress = float(match.group(4))
    if not math.isfinite(progress):
        raise RuntimeError("Native fleet reported non-finite transit progress")
    return fleet_id, destination, revision, progress


def validate_native_fleet_export(folder: Path, env: dict[str, str],
                                 player17_fixture: Path):
    source = _source_row(player17_fixture)
    systems = source.get("Galaxy", {}).get("Systems", [])
    source_player_id = source.get("Galaxy", {}).get("PlayerCivilizationId")
    source_days = source.get("SimulationDays")
    if source.get("FormatVersion") != 17 or not systems:
        raise RuntimeError("Fleet source row is not a Player17 campaign")
    if (not isinstance(source_days, (int, float)) or
            not math.isfinite(source_days)):
        raise RuntimeError("Fleet source row has invalid simulation time")

    with tempfile.TemporaryDirectory(prefix="stellar-native-fleet-") as temporary:
        work = Path(temporary)
        save = work / "fleet.player17.json"
        save.write_text(json.dumps(source, ensure_ascii=False), encoding="utf-8")
        system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
        clean_env = dict(env, PATH=str(system_root / "System32") +
                         os.pathsep + str(system_root))
        captures = []
        diagnostics = []
        ordered_payload = None
        ordered_state = None
        for replay in (False, True):
            capture = work / ("fleet-loaded.bmp" if replay else
                              "fleet-ordered.bmp")
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--load", "--fleet-smoke", str(capture)]
            result = subprocess.run(args, cwd=work, env=clean_env,
                                    capture_output=True, text=True, timeout=90)
            if result.returncode != 0:
                raise RuntimeError(
                    f"Native fleet input replay failed ({result.returncode}): "
                    f"{result.stderr}")
            if ("gpu_driver=vulkan" not in result.stdout or
                    f"systems={len(systems)} " not in result.stdout):
                raise RuntimeError(
                    f"Native fleet did not confirm renderer/campaign: {result.stdout}")
            if "save=ok " not in result.stdout:
                raise RuntimeError("Native fleet did not confirm an actual manual save")
            if not re.search(r"(?:^|\s)civilian_recovery=1(?:\s|$)", result.stdout):
                raise RuntimeError("Native fleet did not prove civilian hold/resume input")
            if (not capture.is_file() or capture.stat().st_size < 54 or
                    capture.read_bytes()[:2] != b"BM"):
                raise RuntimeError("Native fleet did not capture its rendered workspace")
            if not save.is_file():
                raise RuntimeError("Native fleet did not save its campaign")
            payload = json.loads(save.read_text(encoding="utf-8"))
            if not payload.get("SavedAtUtc") or payload.get("FormatVersion") != 17:
                raise RuntimeError("Native fleet save is not a timestamped Player17 campaign")
            saved_galaxy = payload.get("Galaxy", {})
            if saved_galaxy.get("PlayerCivilizationId") != source_player_id:
                raise RuntimeError("Native fleet save changed the player civilization")
            if len(saved_galaxy.get("Systems", [])) != len(systems):
                raise RuntimeError("Native fleet save changed the campaign system count")
            state = _diagnostic(result.stdout)
            fleet, _ = _fleet(payload, state[0])
            if (fleet.get("DestinationSystemId") != state[1] or
                    fleet.get("MissionOrderRevision") != state[2]):
                raise RuntimeError("Fleet diagnostic does not match the saved owned fleet")

            if not replay:
                saved_days = payload.get("SimulationDays")
                if (not isinstance(saved_days, (int, float)) or
                        not math.isfinite(saved_days) or
                        saved_days <= source_days):
                    raise RuntimeError("Fleet order did not advance finite simulation time")
                source_fleet, _ = _fleet(source, state[0])
                if (source_fleet.get("DestinationSystemId") is not None or
                        source_fleet.get("PlannedRouteSystemIds")):
                    raise RuntimeError("Fleet source fixture was already routed")
                if (fleet.get("MissionOrderRevision", 0) <=
                        source_fleet.get("MissionOrderRevision", 0) or
                        fleet.get("DestinationSystemId") is None or
                        not fleet.get("PlannedRouteSystemIds") or
                        fleet["PlannedRouteSystemIds"][-1] !=
                        fleet["DestinationSystemId"]):
                    raise RuntimeError("Player input did not assign a new canonical route")
                progress = fleet.get("TransitProgress")
                if (not isinstance(progress, (int, float)) or
                        not math.isfinite(progress) or
                        progress <= source_fleet.get("TransitProgress", 0)):
                    raise RuntimeError("Fleet order did not produce positive real transit advancement")
                ordered_payload = copy.deepcopy(payload)
                ordered_state = state
            else:
                if ordered_state is None or state[:3] != ordered_state[:3]:
                    raise RuntimeError("Fleet route identity changed during paused reload")
                before = copy.deepcopy(ordered_payload)
                after = copy.deepcopy(payload)
                before.pop("SavedAtUtc", None)
                after.pop("SavedAtUtc", None)
                if before != after:
                    raise RuntimeError("Fleet campaign changed during paused load/recapture")

            evidence = folder.parent / (
                folder.name + ("-fleet-loaded.bmp" if replay else
                               "-fleet-ordered.bmp"))
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())

        return {"nativeFleetPlayerInput": True,
                "nativeFleetTransitAdvanced": True,
                "nativeFleetProgressReload": True,
                "fleetCaptures": captures,
                "fleetDiagnostics": diagnostics}
