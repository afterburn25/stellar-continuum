"""Exact approved native ship artwork; no implicit directory-wide asset packaging."""
from __future__ import annotations

import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


NATIVE_SHIP_ART_SOURCES = {
    "pathfinder-scout": (
        "assets/visual/ships/pathfinder-scout.jpg",
        "assets/visual/ships/pathfinder-scout.jpg"),
    "deep-space-science-vessel": (
        "assets/visual/ships/deep-space-science-vessel.jpg",
        "assets/visual/ships/deep-space-science-vessel.jpg"),
    "patrol-corvette": (
        "assets/visual/ships/patrol-corvette.jpg",
        "assets/visual/ships/patrol-corvette.jpg"),
    "interstellar-colony-ship": (
        "assets/visual/ships/interstellar-colony-ship.jpg",
        "assets/visual/ships/interstellar-colony-ship.jpg"),
    "resource-outpost-ship": (
        "assets/visual/ships/resource-outpost-ship.png",
        "assets/visual/ships/resource-outpost-ship.png"),
    "interstellar-bulk-freighter": (
        "assets/visual/ships/interstellar-bulk-freighter.png",
        "assets/visual/ships/interstellar-bulk-freighter.png"),
    "patrol-corvette-tactical-v1": (
        "assets/visual/ships/patrol-corvette-tactical-v1.png",
        "assets/visual/ships/patrol-corvette-tactical-v1.png"),
    "credits": (
        "docs/engine/NATIVE_SHIP_ART_SOURCES.md",
        "Licenses/Ship-art-sources.md"),
}


def native_ship_art_asset_files(root):
    declaration = json.loads((root / "export/native-ship-art-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native ship art asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_SHIP_ART_SOURCES):
        raise RuntimeError("Native ship art asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_SHIP_ART_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native ship art {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native ship art {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native ship art {key} differs from reviewed content: {path}")
        files[destination] = path
    return files


def _source_row(fixture: Path) -> dict:
    root = json.loads(fixture.read_text(encoding="utf-8"))
    rows = root.get("Rows")
    if not isinstance(rows, list):
        raise RuntimeError("Player17 ship art fixture has no source-authored rows")
    matches = [row for row in rows if row.get("Name") == "valid-current17"]
    if len(matches) != 1 or not isinstance(matches[0].get("InputJson"), str):
        raise RuntimeError("Player17 ship art fixture has no unique valid-current17 input")
    return json.loads(matches[0]["InputJson"])


def _player_entry(payload: dict, collection: str) -> dict:
    galaxy = payload.get("Galaxy", {})
    player_id = galaxy.get("PlayerCivilizationId")
    matches = [row for row in galaxy.get(collection, [])
               if row.get("CivilizationId") == player_id]
    if len(matches) != 1:
        raise RuntimeError(f"Ship art save has no unique player-owned {collection} row")
    return matches[0]


def _research_core(payload: dict) -> dict:
    player_id = payload.get("Galaxy", {}).get("PlayerCivilizationId")
    matches = [row for row in payload.get("AdaptiveResearch", {}).get("Civilizations", [])
               if row.get("CivilizationId") == player_id]
    if len(matches) != 1:
        raise RuntimeError("Ship art fixture has no unique player research row")
    return matches[0]["Research"]["Research"]["Research"]["Research"]["Core"]


def _author_ship_art_source(source: dict) -> dict:
    # The reviewed fleet fixture carries no unlocked designs. Grant the same
    # shipyard prerequisites the production validator authors so the smoke
    # exercises real design rows without changing gameplay rules.
    authored = copy.deepcopy(source)
    systems = authored.get("Galaxy", {}).get("Systems", [])
    if authored.get("FormatVersion") != 17 or not systems:
        raise RuntimeError("Ship art source row is not a Player17 campaign")
    construction = _player_entry(authored, "ConstructionStates")
    construction["CompletedProjectIds"] = ["orbital_shipyard"]
    core = _research_core(authored)
    core["Capabilities"] = [
        {"CapabilityId": "spacecraft_construction", "ContextId": None},
        {"CapabilityId": "experimental_interstellar_transit", "ContextId": None},
    ]
    return authored


def _diagnostic(stdout: str):
    match = re.search(r"(?:^|\s)ship_art=(\{[^\n]+\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native ship art did not report its render evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native ship art diagnostic is malformed") from error
    for key in ("shipyard_art_rows", "fleet_art_rows", "decoded_sources",
                "cached_entries", "cache_bytes", "routed_fleets",
                "drawn_legs", "chevron_segments", "trail_strokes",
                "position_circles", "route_lines", "routed_fleet_id",
                "destination"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, int):
            raise RuntimeError(f"Native ship art reported invalid {key}")
    if state["shipyard_art_rows"] < 1:
        raise RuntimeError("Native shipyard did not render approved design artwork")
    if state["fleet_art_rows"] < 1:
        raise RuntimeError("Native fleet panel did not render owned ship artwork")
    if not 1 <= state["decoded_sources"] <= 6:
        raise RuntimeError("Native ship artwork decoded an unexpected source count")
    if state["decoded_sources"] != state["cached_entries"]:
        raise RuntimeError("Native ship artwork decoded sources more than once")
    if not 0 < state["cache_bytes"] <= 4 * 1024 * 1024:
        raise RuntimeError("Native ship artwork cache exceeded its bounded budget")
    if state["routed_fleets"] < 1 or state["drawn_legs"] < 1 or \
            state["route_lines"] < 1:
        raise RuntimeError("Native map did not render an active fleet route")
    if state["trail_strokes"] != 3 * state["position_circles"] or \
            state["position_circles"] < 1:
        raise RuntimeError("Native fleet trail and position markers are inconsistent")
    if state["routed_fleet_id"] <= 0 or state["destination"] <= 0:
        raise RuntimeError("Native ship art did not identify the routed owned fleet")
    return state


def _capture(path: Path):
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"Native ship art did not capture a rendered frame: {path}")
    if min(data[54:]) == max(data[54:]):
        raise RuntimeError("Native ship art capture contains no rendered variation")
    return data


def _saved_fleet(payload: dict, fleet_id: int):
    galaxy = payload.get("Galaxy", {})
    matches = [fleet for fleet in galaxy.get("Fleets", [])
               if fleet.get("Id") == fleet_id]
    if len(matches) != 1:
        raise RuntimeError("Ship art smoke did not identify a unique routed fleet")
    if matches[0].get("CivilizationId") != galaxy.get("PlayerCivilizationId") or \
            not matches[0].get("IsActive", False):
        raise RuntimeError("Ship art smoke routed a foreign or inactive fleet")
    return matches[0]


def validate_native_ship_art_export(folder: Path, env: dict[str, str],
                                    player17_fixture: Path):
    source = _source_row(player17_fixture)
    systems = source.get("Galaxy", {}).get("Systems", [])
    if source.get("FormatVersion") != 17 or not systems:
        raise RuntimeError("Ship art source row is not a Player17 campaign")
    authored = _author_ship_art_source(source)
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") +
                     os.pathsep + str(system_root))
    captures, diagnostics = [], []
    ordered_payload = None
    ordered_identity = None
    with tempfile.TemporaryDirectory(prefix="stellar-native-ship-art-") as temporary:
        work = Path(temporary)
        save = work / "ship-art.player17.json"
        save.write_text(json.dumps(authored, ensure_ascii=False), encoding="utf-8")
        for replay in (False, True):
            capture = work / ("ship-art-loaded.bmp" if replay else
                              "ship-art-ordered.bmp")
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--load", "--ship-art-smoke", str(capture)]
            result = subprocess.run(args, cwd=work, env=clean_env,
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="strict",
                                    timeout=120)
            if result.returncode != 0:
                raise RuntimeError(
                    f"Native ship art smoke failed ({result.returncode}):\n"
                    f"{result.stdout}\n{result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", f"systems={len(systems)} ",
                    "save=ok ")):
                raise RuntimeError(
                    "Native ship art did not confirm Vulkan, campaign and save")
            state = _diagnostic(result.stdout)
            map_capture = capture.with_name(capture.stem + "-map" + capture.suffix)
            shipyard_pixels = _capture(capture)
            map_pixels = _capture(map_capture)
            if hashlib.sha256(shipyard_pixels).digest() == \
                    hashlib.sha256(map_pixels).digest():
                raise RuntimeError("Native ship art captures do not show two distinct views")
            if not save.is_file():
                raise RuntimeError("Native ship art smoke did not write its isolated save")
            payload = json.loads(save.read_text(encoding="utf-8"))
            days = payload.get("SimulationDays")
            if (payload.get("FormatVersion") != 17 or
                    len(payload.get("Galaxy", {}).get("Systems", [])) != len(systems) or
                    isinstance(days, bool) or not isinstance(days, (int, float)) or
                    not math.isfinite(days)):
                raise RuntimeError("Native ship art smoke damaged the campaign payload")
            fleet = _saved_fleet(payload, state["routed_fleet_id"])
            if fleet.get("DestinationSystemId") != state["destination"]:
                raise RuntimeError("Ship art route evidence does not match the saved fleet")
            identity = (state["routed_fleet_id"], state["destination"])
            if not replay:
                if not fleet.get("PlannedRouteSystemIds"):
                    raise RuntimeError("Player input did not assign a canonical route")
                ordered_payload = copy.deepcopy(payload)
                ordered_identity = identity
            else:
                if identity != ordered_identity:
                    raise RuntimeError("Ship art route identity changed during paused reload")
                before = copy.deepcopy(ordered_payload)
                after = copy.deepcopy(payload)
                before.pop("SavedAtUtc", None)
                after.pop("SavedAtUtc", None)
                if before != after:
                    raise RuntimeError("Ship art campaign changed during paused reload")
            for path in (capture, map_capture):
                evidence = folder.parent / f"{folder.name}-{path.name}"
                shutil.copy2(path, evidence)
                captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
    return {"nativeShipArtRows": True,
            "nativeShipyardArtwork": True,
            "nativeFleetRouteEffects": True,
            "nativeShipArtReload": True,
            "shipArtCaptures": captures,
            "shipArtDiagnostics": diagnostics}
