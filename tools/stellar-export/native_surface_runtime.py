"""Export proof for pointer-driven native surface construction."""
from __future__ import annotations

import copy
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

_FIELDS = {
    "mode", "system_id", "body_id", "colony_id", "type_id", "site_id",
    "x", "z", "rotation", "authorization", "refund", "treasury_before",
    "treasury_after_cancel", "treasury_after_place", "treasury_after_refund",
    "treasury_saved", "site_count_before", "site_count_saved", "progress",
    "before_days", "saved_days", "palette_selected", "ghost_previewed",
    "placement_cancelled", "cancel_no_change", "placement_confirmed",
    "removal_previewed", "removal_confirmed", "refund_exact",
    "persisted_site", "paused", "scene_sprites", "managed",
}


def _finite(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise RuntimeError(f"Native surface reported invalid {label}")
    return float(value)


def _close(left, right, tolerance=5e-7):
    return math.isclose(_finite(left, "numeric value"), _finite(right, "numeric value"),
                        rel_tol=0, abs_tol=tolerance)


def _diagnostic(stdout: str, expected_mode: str):
    match = re.search(r"(?:^|\s)surface=(\{[^\n]+\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native surface did not report its construction evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native surface diagnostic is malformed") from error
    if set(state) != _FIELDS:
        raise RuntimeError("Native surface diagnostic exposed an unexpected field set")
    if state.get("mode") != expected_mode:
        raise RuntimeError("Native surface reported the wrong mode")
    for key in ("system_id", "body_id", "colony_id", "site_id",
                "site_count_before", "site_count_saved"):
        if type(state.get(key)) is not int or state[key] < 0:
            raise RuntimeError(f"Native surface reported invalid {key}")
    if not isinstance(state.get("type_id"), str) or not state["type_id"]:
        raise RuntimeError("Native surface reported no building type")
    for key in ("x", "z", "rotation", "authorization", "refund",
                "treasury_before", "treasury_after_cancel",
                "treasury_after_place", "treasury_after_refund",
                "treasury_saved", "progress", "before_days", "saved_days"):
        _finite(state.get(key), key)
    if state.get("persisted_site") is not True or state.get("paused") is not True:
        raise RuntimeError("Native surface did not prove a persisted paused site")
    if type(state.get("scene_sprites")) is not int or state["scene_sprites"] < 2:
        raise RuntimeError("Native surface did not rasterize hub and site sprites")
    interaction = ("palette_selected", "ghost_previewed", "placement_cancelled",
                   "cancel_no_change", "placement_confirmed", "removal_previewed",
                   "removal_confirmed", "refund_exact", "managed")
    if expected_mode == "ordered":
        for key in interaction:
            if state.get(key) is not True:
                raise RuntimeError(f"Native surface did not prove {key}")
        if state["authorization"] <= 0 or state["refund"] <= 0:
            raise RuntimeError("Native surface reported nonpositive canonical money terms")
        if not _close(state["treasury_after_cancel"], state["treasury_before"], 1e-8):
            raise RuntimeError("Cancelling a placement changed treasury")
        if not _close(state["treasury_before"] - state["treasury_after_place"],
                      state["authorization"], 1e-8):
            raise RuntimeError("Confirmed placement did not apply its quoted charge once")
        if not _close(state["treasury_after_refund"] - state["treasury_after_place"],
                      state["refund"], 1e-8):
            raise RuntimeError("Construction cancellation did not apply its quoted refund")
        if not _close(state["refund"], state["authorization"] * .5, 1e-8):
            raise RuntimeError("Incomplete construction did not use the canonical half refund")
        if state["site_count_saved"] != state["site_count_before"] + 1:
            raise RuntimeError("Surface flow did not leave exactly one new site")
        if not state["saved_days"] > state["before_days"] or state["progress"] <= 0:
            raise RuntimeError("Surface smoke did not advance canonical construction")
    else:
        if any(state.get(key) is not False for key in interaction):
            raise RuntimeError("Paused surface reload claimed fresh edit interactions")
        if state["saved_days"] != state["before_days"]:
            raise RuntimeError("Paused surface reload advanced canonical time")
    if state["progress"] < 0:
        raise RuntimeError("Native surface reported negative progress")
    return state


def _bmp(path: Path, width: int, height: int):
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError("Native surface did not capture a BMP frame")
    declared = struct.unpack_from("<I", data, 2)[0]
    offset = struct.unpack_from("<I", data, 10)[0]
    header = struct.unpack_from("<I", data, 14)[0]
    actual_width, actual_height, planes, bits = struct.unpack_from("<iiHH", data, 18)
    compression = struct.unpack_from("<I", data, 30)[0]
    row = ((actual_width * bits + 31) // 32) * 4 if actual_width > 0 else 0
    required = row * abs(actual_height)
    if (declared != len(data) or offset < 54 or header < 40 or
            actual_width != width or abs(actual_height) != height or planes != 1 or
            bits not in (24, 32) or compression not in (0, 3) or required <= 0 or
            offset + required > len(data)):
        raise RuntimeError("Native surface capture has invalid renderer geometry")
    pixels = data[offset:offset + required]
    if not pixels or min(pixels) == max(pixels):
        raise RuntimeError("Native surface capture contains no rendered variation")


def _player_parts(payload):
    galaxy = payload.get("Galaxy", {})
    player_id = galaxy.get("PlayerCivilizationId")
    player = next((x for x in galaxy.get("Civilizations", [])
                   if x.get("Id") == player_id and x.get("IsPlayer") is True), None)
    economy = next((x for x in galaxy.get("Economies", [])
                    if x.get("CivilizationId") == player_id), None)
    knowledge = next((x for x in galaxy.get("Knowledge", [])
                      if x.get("CivilizationId") == player_id), None)
    colonies = [x for x in galaxy.get("Colonies", [])
                if x.get("CivilizationId") == player_id and
                type(x.get("PlanetaryBodyId")) is int]
    if player is None or economy is None or knowledge is None or not colonies:
        raise RuntimeError("Player17 lacks a complete owned surface colony")
    return galaxy, player_id, economy, knowledge, colonies


def _base_colony(payload):
    galaxy, player_id, economy, knowledge, colonies = _player_parts(payload)
    if payload.get("FormatVersion") != 17 or len(galaxy.get("Systems", [])) != 500:
        raise RuntimeError("Native surface base is not current 500-system Player17")
    home_id = next(x for x in galaxy["Civilizations"] if x.get("Id") == player_id).get("HomeSystemId")
    colony = next((x for x in colonies if x.get("SystemId") == home_id), colonies[0])
    system_id, body_id = colony.get("SystemId"), colony.get("PlanetaryBodyId")
    known = knowledge.get("KnownSystemIds", [])
    surveys = [x for x in knowledge.get("SystemSurveys", []) if x.get("SystemId") == system_id]
    body = next((x for x in galaxy.get("PlanetaryBodies", [])
                 if x.get("Id") == body_id and x.get("SystemId") == system_id), None)
    if (type(system_id) is not int or type(body_id) is not int or system_id not in known or
            not surveys or max(x.get("Level", 0) for x in surveys) < 2 or
            body is None or body.get("Name") != "Earth"):
        raise RuntimeError("Fresh owned surface colony is not observer-visible")
    if _finite(economy.get("Credits"), "fresh treasury") <= 0:
        raise RuntimeError("Fresh owned surface colony has no construction treasury")
    if not isinstance(colony.get("SurfaceBuildings"), list):
        raise RuntimeError("Fresh owned surface colony has no current surface state")
    return colony


def _normalized(payload):
    result = copy.deepcopy(payload)
    result.pop("SavedAtUtc", None)
    return result


def _verify_ordered(before, after, state):
    before_colony = _base_colony(before)
    galaxy, player_id, economy, knowledge, colonies = _player_parts(after)
    if len(galaxy.get("Systems", [])) != 500:
        raise RuntimeError("Surface save changed the 500-system campaign")
    colony = next((x for x in colonies if x.get("Id") == state["colony_id"]), None)
    if (colony is None or colony.get("CivilizationId") != player_id or
            colony.get("SystemId") != state["system_id"] or
            colony.get("PlanetaryBodyId") != state["body_id"] or
            before_colony.get("Id") != state["colony_id"]):
        raise RuntimeError("Surface diagnostic is not bound to the owned fresh colony")
    if (state["system_id"] not in knowledge.get("KnownSystemIds", []) or
            not any(x.get("SystemId") == state["system_id"] and x.get("Level", 0) >= 2
                    for x in knowledge.get("SystemSurveys", []))):
        raise RuntimeError("Surface command bypassed observer knowledge")
    sites = colony.get("SurfaceBuildings", [])
    if len(before_colony.get("SurfaceBuildings", [])) != state["site_count_before"] or len(sites) != state["site_count_saved"]:
        raise RuntimeError("Surface diagnostic site counts differ from Player17")
    site = next((x for x in sites if x.get("Id") == state["site_id"]), None)
    if (site is None or site.get("TypeId") != state["type_id"] or
            site.get("IsComplete") is not False or
            not _close(site.get("X"), state["x"]) or
            not _close(site.get("Z"), state["z"]) or
            not _close(site.get("RotationDegrees"), state["rotation"]) or
            not _close(site.get("IndustryProgress"), state["progress"])):
        raise RuntimeError("Surface diagnostic differs from the saved unfinished site")
    if not _close(after.get("SimulationDays"), state["saved_days"]):
        raise RuntimeError("Surface diagnostic day differs from Player17")
    if not _close(economy.get("Credits"), state["treasury_saved"]):
        raise RuntimeError("Surface diagnostic treasury differs from Player17")


def _launch(args, cwd, env, label):
    result = subprocess.run(args, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=120)
    if result.returncode != 0:
        details = "\n".join(x for x in (result.stdout, result.stderr) if x)
        raise RuntimeError(f"Native surface {label} failed ({result.returncode}): {details}")
    if any(token not in result.stdout for token in
           ("gpu_driver=vulkan ", "systems=500 ", "save=ok")):
        raise RuntimeError("Native surface launch lacked Vulkan, campaign or save proof")
    uploads = re.search(r"\bimage_uploads=(\d+)(?:\s|$)", result.stdout)
    if not uploads:
        raise RuntimeError("Native surface launch lacked image-upload diagnostics")
    return result, int(uploads.group(1))


def validate_native_surface_export(folder: Path, env: dict[str, str]):
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics = [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-surface-") as temporary:
        work = Path(temporary)
        save = work / "fresh.player17.json"
        base_capture = work / "surface-base-1280x720.bmp"
        exe = str(folder / "stellar-continuum-native.exe")
        common = [exe, "--asset-root", str(folder), "--save-path", str(save)]
        result, _ = _launch(common + ["--width", "1280", "--height", "720",
                                      "--smoke", str(base_capture)], work, clean,
                            "fresh-base launch")
        _bmp(base_capture, 1280, 720)
        if not save.is_file():
            raise RuntimeError("Native surface fresh-base launch wrote no Player17 save")
        base = json.loads(save.read_text(encoding="utf-8-sig"))
        _base_colony(base)
        evidence = folder.parent / f"{folder.name}-surface-base-1280x720.bmp"
        shutil.copy2(base_capture, evidence)
        captures.append(str(evidence)); diagnostics.append(result.stdout.strip())

        ordered_payload = ordered_state = None
        for width, height, flag, mode in (
                (1280, 720, "--surface-smoke", "ordered"),
                (1920, 1080, "--surface-reload-smoke", "paused_reload")):
            capture = work / f"surface-{mode}-{width}x{height}.bmp"
            result, uploads = _launch(common + ["--width", str(width), "--height", str(height),
                                                flag, str(capture), "--load"],
                                      work, clean, mode)
            if uploads < 1:
                raise RuntimeError("Native surface workspace did not prove image uploads")
            state = _diagnostic(result.stdout, mode)
            _bmp(capture, width, height)
            payload = json.loads(save.read_text(encoding="utf-8-sig"))
            if payload.get("FormatVersion") != 17:
                raise RuntimeError("Native surface wrote a noncurrent Player17 save")
            if mode == "ordered":
                _verify_ordered(base, payload, state)
                ordered_payload, ordered_state = payload, state
            else:
                if _normalized(payload) != _normalized(ordered_payload):
                    raise RuntimeError("Paused surface reload changed the Player17 payload")
                for key in ("system_id", "body_id", "colony_id", "type_id", "site_id",
                            "x", "z", "rotation", "treasury_saved", "site_count_saved",
                            "progress", "saved_days"):
                    if state[key] != ordered_state[key]:
                        raise RuntimeError("Paused surface reload changed site identity or state")
            evidence = folder.parent / f"{folder.name}-surface-{width}x{height}.bmp"
            shutil.copy2(capture, evidence)
            captures.append(str(evidence)); diagnostics.append(result.stdout.strip())
    return {"nativeSurfaceFreshOwnedEarth": True,
            "nativeSurfacePlayerInput": True,
            "nativeSurfacePausedReload": True,
            "surfaceCaptures": captures,
            "surfaceDiagnostics": diagnostics,
            "surfaceFixture": "unaltered standard fresh 500-system Player17 campaign",
            "surfaceDemolition": "completed-site demolition remains controller-test evidence"}
