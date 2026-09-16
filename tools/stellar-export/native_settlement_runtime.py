"""Export proof for pointer-driven native colony and outpost mission ordering."""
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

_COST = {"colony": 120.0, "outpost": 90.0}
_DURATION = {"colony": 30.0, "outpost": 20.0}
_FIELDS = {
    "mode", "kind", "fleet_id", "system_id", "body_id", "mission_revision",
    "before_days", "saved_days", "settlement_days", "authorization",
    "treasury_before", "treasury_after", "requires_authorization", "selected",
    "previewed", "cancelled", "cancel_no_charge", "accepted",
    "no_instant_colony", "paused",
}


def _finite(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise RuntimeError(f"Native settlement reported invalid {label}")
    return float(value)


def _diagnostic(stdout: str, expected_mode: str, expected_kind: str):
    match = re.search(r"(?:^|\s)settlement=(\{[^\n]+\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native settlement did not report its mission evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native settlement diagnostic is malformed") from error
    if set(state) != _FIELDS:
        raise RuntimeError("Native settlement diagnostic exposed an unexpected field set")
    if state.get("mode") != expected_mode or state.get("kind") != expected_kind:
        raise RuntimeError("Native settlement reported the wrong mode or mission kind")
    for key in ("fleet_id", "system_id", "body_id", "mission_revision"):
        if type(state.get(key)) is not int or state[key] < 0:
            raise RuntimeError(f"Native settlement reported invalid {key}")
    for key in ("before_days", "saved_days", "settlement_days", "authorization",
                "treasury_before", "treasury_after"):
        _finite(state.get(key), key)
    for key in ("selected", "no_instant_colony", "paused"):
        if state.get(key) is not True:
            raise RuntimeError(f"Native settlement did not prove {key}")
    if expected_mode == "ordered":
        for key in ("previewed", "cancelled", "cancel_no_charge", "accepted",
                    "requires_authorization"):
            if state.get(key) is not True:
                raise RuntimeError(f"Native settlement did not prove {key}")
        if state["authorization"] != _COST[expected_kind]:
            raise RuntimeError("Native settlement quoted the wrong initial authorization")
        if not math.isclose(state["treasury_before"] - state["treasury_after"],
                            _COST[expected_kind], rel_tol=0, abs_tol=1e-8):
            raise RuntimeError("Native settlement command charged the wrong authorization")
        if not state["saved_days"] > state["before_days"]:
            raise RuntimeError("Native settlement smoke did not advance canonical time")
    else:
        if state["saved_days"] != state["before_days"]:
            raise RuntimeError("Paused settlement reload advanced canonical time")
    if state["settlement_days"] < 0 or state["settlement_days"] >= _DURATION[expected_kind]:
        raise RuntimeError("Native settlement did not preserve an incomplete timed mission")
    return state


def _bmp(path: Path, stdout: str, width: int, height: int):
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError("Native settlement did not capture a BMP frame")
    # The capture is at drawable-pixel size; on high-DPI displays that is a
    # multiple of the requested window size. The smoke reports the actual
    # drawable so the BMP geometry is checked against the real render surface.
    drawable = re.search(r"(?:^|\s)drawable=(\d+)x(\d+)(?:\s|$)", stdout)
    if drawable:
        width, height = int(drawable.group(1)), int(drawable.group(2))
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
        raise RuntimeError("Native settlement capture has invalid renderer geometry")
    pixels = data[offset:]
    if not pixels or min(pixels) == max(pixels):
        raise RuntimeError("Native settlement capture contains no rendered variation")


def _player_parts(payload):
    galaxy = payload.get("Galaxy", {})
    player_id = galaxy.get("PlayerCivilizationId")
    player = next((x for x in galaxy.get("Civilizations", [])
                   if x.get("Id") == player_id and x.get("IsPlayer") is True), None)
    economy = next((x for x in galaxy.get("Economies", [])
                    if x.get("CivilizationId") == player_id), None)
    knowledge = next((x for x in galaxy.get("Knowledge", [])
                      if x.get("CivilizationId") == player_id), None)
    if player is None or economy is None or knowledge is None:
        raise RuntimeError("Player17 lacks complete player settlement state")
    return galaxy, player_id, player, economy, knowledge


def _fleet_template(fleet_id, player_id, species_id, home_id, x, y, kind):
    return {
        "Id": fleet_id, "CivilizationId": player_id,
        "Name": "Authored Native Colony Vessel" if kind == "colony" else "Authored Native Resource Outpost",
        "Role": 2, "DesignId": "colony_ship" if kind == "colony" else "resource_outpost_ship",
        "X": x, "Y": y, "CurrentSystemId": home_id, "DestinationSystemId": None,
        "TransitPhase": 0, "TransitOriginSystemId": None, "TransitTargetSystemId": None,
        "TransitProgress": 0.0, "LocalTransitStartX": 0.0, "LocalTransitStartY": 0.0,
        "LocalTransitPositionX": 0.0, "LocalTransitPositionY": 0.0,
        "LocalTransitTargetX": 0.0, "LocalTransitTargetY": 0.0,
        "PlannedRouteSystemIds": [], "HoldRequested": False,
        "ReturnToBaseRequested": False, "ReturnToBaseFailureReason": None,
        "MissionOrderRevision": 0, "DestinationPlanetaryBodyId": None,
        "PreventAutomaticSettlement": False, "SettlementBodyId": None,
        "SettlementDaysCompleted": 0.0, "ReconnaissanceSystemId": None,
        "ReconnaissanceDaysCompleted": 0.0, "FreightTargetOutpostId": None,
        "FreightHomeColonyId": None, "CargoMaterialCapacity": 0.0,
        "CargoMaterials": 0.0, "StrategicSpeed": 1_000_000.0,
        "MaximumLegRangeLightYears": 1_000_000.0,
        "FuelCapacityLightYears": 1_000_000.0,
        "FuelRemainingLightYears": 1_000_000.0, "SensorRange": 140.0,
        "IsActive": True, "EmbarkedPopulationMillions": 4.5 if kind == "colony" else 0.8,
        "EmbarkedPopulationSpeciesId": species_id,
        "Combat": {"ProfileId": "civilian_light_v1", "Shields": 0.0,
                   "Armor": 10.0, "Hull": 45.0,
                   "WeaponCooldownRemainingDays": 0.0, "Order": 0,
                   "TargetFleetId": None, "DefendSystemId": None,
                   "RetreatProgressDays": 0.0, "RetreatStarted": False,
                   "IsDisengaged": False, "DisengagedSystemId": None},
    }


def author_settlement_fixture(base_payload, kind):
    if kind not in _COST:
        raise RuntimeError("Unknown authored settlement fixture kind")
    result = copy.deepcopy(base_payload)
    if result.get("FormatVersion") != 17:
        raise RuntimeError("Settlement fixture base is not current Player17")
    galaxy, player_id, player, economy, knowledge = _player_parts(result)
    if _finite(economy.get("Credits"), "fixture treasury") + 0.0001 < _COST[kind]:
        raise RuntimeError("Settlement fixture base cannot fund the canonical expedition")
    if any(f.get("CivilizationId") == player_id and f.get("Role") == 2 and
           f.get("IsActive") is True and _finite(f.get("EmbarkedPopulationMillions", 0), "existing passengers") > 0
           for f in galaxy.get("Fleets", [])):
        raise RuntimeError("Settlement fixture base already contains an eligible player vessel")
    systems = galaxy.get("Systems", [])
    home_id = player.get("HomeSystemId")
    home = next((x for x in systems if x.get("Id") == home_id), None)
    if home is None or not isinstance(player.get("SpeciesId"), str):
        raise RuntimeError("Settlement fixture base lacks a player home/species")
    ids = [x.get("Id") for x in systems]
    if len(ids) != 500 or any(type(value) is not int for value in ids):
        raise RuntimeError("Settlement fixture base is not the deterministic 500-system campaign")
    knowledge["KnownSystemIds"] = list(ids)
    knowledge["SystemSurveys"] = [
        {"SystemId": value, "Level": 3, "Progress": 1.0} for value in ids]
    fleets = galaxy.setdefault("Fleets", [])
    fleet_id = max((x.get("Id", -1) for x in fleets), default=-1) + 1
    fleets.append(_fleet_template(fleet_id, player_id, player["SpeciesId"], home_id,
                                  _finite(home.get("X"), "home X"),
                                  _finite(home.get("Y"), "home Y"), kind))
    return result


def _normalized(payload):
    result = copy.deepcopy(payload)
    result.pop("SavedAtUtc", None)
    return result


def _verify_payload(before, after, state, kind):
    before_galaxy, player_id, _, before_economy, _ = _player_parts(before)
    galaxy, after_player, _, _, knowledge = _player_parts(after)
    if after_player != player_id:
        raise RuntimeError("Settlement save changed the player owner")
    if len(galaxy.get("Systems", [])) != 500:
        raise RuntimeError("Settlement save changed the 500-system campaign")
    if not math.isclose(_finite(before_economy.get("Credits"), "initial treasury"),
                        state["treasury_before"], rel_tol=0, abs_tol=1e-8):
        raise RuntimeError("Settlement command treasury does not match the authored Player17 input")
    fleet = next((x for x in galaxy.get("Fleets", [])
                  if x.get("Id") == state["fleet_id"]), None)
    initial = next((x for x in before_galaxy.get("Fleets", [])
                    if x.get("Id") == state["fleet_id"]), None)
    if (initial is None or initial.get("CivilizationId") != player_id or
            initial.get("Role") != 2 or
            not _finite(initial.get("EmbarkedPopulationMillions"),
                        "authored passengers") > 0 or
            not isinstance(initial.get("EmbarkedPopulationSpeciesId"), str)):
        raise RuntimeError("Settlement vessel was not present in the authored input")
    if fleet is None or fleet.get("CivilizationId") != player_id or fleet.get("Role") != 2:
        raise RuntimeError("Settlement diagnostic does not identify an owned colony-role vessel")
    expected_design = "colony_ship" if kind == "colony" else "resource_outpost_ship"
    if (fleet.get("DesignId") != expected_design or fleet.get("IsActive") is not True or
            fleet.get("EmbarkedPopulationMillions") !=
                initial.get("EmbarkedPopulationMillions") or
            fleet.get("EmbarkedPopulationSpeciesId") !=
                initial.get("EmbarkedPopulationSpeciesId")):
        raise RuntimeError("Settlement mission has the wrong authored vessel kind or lifecycle")
    destination_bound = (fleet.get("DestinationSystemId") == state["system_id"] or
                         (fleet.get("DestinationSystemId") is None and
                          fleet.get("CurrentSystemId") == state["system_id"]))
    if (not destination_bound or
            fleet.get("DestinationPlanetaryBodyId") != state["body_id"] or
            fleet.get("MissionOrderRevision") != state["mission_revision"] or
            type(fleet.get("MissionOrderRevision")) is not int or
            fleet["MissionOrderRevision"] <= initial.get("MissionOrderRevision", -1) or
            not math.isclose(_finite(fleet.get("SettlementDaysCompleted"), "saved settlement progress"),
                             state["settlement_days"], rel_tol=0, abs_tol=5e-7)):
        raise RuntimeError("Settlement diagnostic differs from canonical saved mission state")
    if (state["settlement_days"] > 0 and
            fleet.get("SettlementBodyId") != state["body_id"]):
        raise RuntimeError("Settlement progress is not bound to the saved target body")
    known = knowledge.get("KnownSystemIds", [])
    surveys = [x for x in knowledge.get("SystemSurveys", [])
               if x.get("SystemId") == state["system_id"]]
    if state["system_id"] not in known or not surveys or max(x.get("Level", 0) for x in surveys) < 3:
        raise RuntimeError("Settlement command bypassed observer knowledge")
    if not any(x.get("Id") == state["body_id"] and x.get("SystemId") == state["system_id"]
               for x in galaxy.get("PlanetaryBodies", [])):
        raise RuntimeError("Settlement target body is absent from its saved system")
    before_count = len(before_galaxy.get("Colonies", []))
    if len(galaxy.get("Colonies", [])) != before_count or any(
            x.get("CivilizationId") == player_id and
            x.get("SystemId") == state["system_id"] and
            x.get("PlanetaryBodyId") == state["body_id"]
            for x in galaxy.get("Colonies", [])):
        raise RuntimeError("Settlement order created an instant colony/outpost")


def validate_native_settlement_export(folder: Path, env: dict[str, str]):
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics, kinds = [], [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-settlement-") as temporary:
        work = Path(temporary)
        base_save = work / "fresh.player17.json"
        base_capture = work / "settlement-base-1280x720.bmp"
        base_args = [str(folder / "stellar-continuum-native.exe"),
                     "--asset-root", str(folder), "--save-path", str(base_save),
                     "--width", "1280", "--height", "720", "--smoke",
                     str(base_capture)]
        base_result = subprocess.run(base_args, cwd=work, env=clean,
                                     capture_output=True, text=True, timeout=120)
        if base_result.returncode != 0:
            details = "\n".join(x for x in (base_result.stdout,
                                              base_result.stderr) if x)
            raise RuntimeError(
                f"Native settlement fresh-base launch failed ({base_result.returncode}): {details}")
        if any(token not in base_result.stdout for token in
               ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
            raise RuntimeError("Native settlement fresh-base launch lacked renderer/campaign/save proof")
        base_uploads = re.search(r"\bimage_uploads=(\d+)(?:\s|$)",
                                 base_result.stdout)
        if not base_uploads:
            raise RuntimeError("Native settlement fresh-base launch lacked image-upload diagnostics")
        _bmp(base_capture, base_result.stdout, 1280, 720)
        if not base_save.is_file():
            raise RuntimeError("Native settlement fresh-base launch wrote no Player17 save")
        base = json.loads(base_save.read_text(encoding="utf-8-sig"))
        if base.get("FormatVersion") != 17 or len(base.get("Galaxy", {}).get("Systems", [])) != 500:
            raise RuntimeError("Native settlement fresh-base save is not current 500-system Player17")
        # Validate suitability for authoring before any mission launch.
        author_settlement_fixture(base, "colony")
        author_settlement_fixture(base, "outpost")
        base_evidence = folder.parent / f"{folder.name}-settlement-base-1280x720.bmp"
        shutil.copy2(base_capture, base_evidence)
        captures.append(str(base_evidence))
        diagnostics.append(base_result.stdout.strip())
        for kind in ("colony", "outpost"):
            authored = author_settlement_fixture(base, kind)
            save = work / f"authored-{kind}.player17.json"
            save.write_text(json.dumps(authored, ensure_ascii=False), encoding="utf-8")
            ordered_payload = ordered_state = None
            for width, height, mode, label in (
                    (1280, 720, "--settlement-smoke", "ordered"),
                    (1920, 1080, "--settlement-reload-smoke", "paused_reload")):
                capture = work / f"settlement-{kind}-{width}x{height}.bmp"
                args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                        "--save-path", str(save), "--width", str(width), "--height", str(height),
                        mode, str(capture), "--load"]
                result = subprocess.run(args, cwd=work, env=clean, capture_output=True,
                                        text=True, timeout=120)
                if result.returncode != 0:
                    details = "\n".join(x for x in (result.stdout, result.stderr) if x)
                    raise RuntimeError(f"Native settlement {kind} {label} failed ({result.returncode}): {details}")
                if any(token not in result.stdout for token in
                       ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
                    raise RuntimeError("Native settlement did not confirm Vulkan, campaign and save")
                uploads = re.search(r"\bimage_uploads=(\d+)(?:\s|$)", result.stdout)
                if not uploads or int(uploads.group(1)) < 1:
                    raise RuntimeError("Native settlement did not prove orbital image uploads")
                state = _diagnostic(result.stdout, label, kind)
                _bmp(capture, result.stdout, width, height)
                payload = json.loads(save.read_text(encoding="utf-8-sig"))
                payload_day = _finite(payload.get("SimulationDays"), "saved day")
                if (payload.get("FormatVersion") != 17 or
                        not math.isclose(payload_day, state["saved_days"],
                                         rel_tol=0, abs_tol=5e-7)):
                    raise RuntimeError("Settlement diagnostic day differs from Player17")
                if label == "ordered":
                    _verify_payload(authored, payload, state, kind)
                    ordered_payload, ordered_state = payload, state
                else:
                    if _normalized(payload) != _normalized(ordered_payload):
                        raise RuntimeError("Paused settlement reload changed the Player17 payload")
                    for key in ("kind", "fleet_id", "system_id", "body_id",
                                "mission_revision", "saved_days", "settlement_days"):
                        if state[key] != ordered_state[key]:
                            raise RuntimeError("Paused settlement reload changed its mission identity/state")
                evidence = folder.parent / f"{folder.name}-settlement-{kind}-{width}x{height}.bmp"
                shutil.copy2(capture, evidence)
                captures.append(str(evidence))
                diagnostics.append(result.stdout.strip())
            kinds.append(kind)
    return {"nativeSettlementPlayerInput": True,
            "nativeSettlementFreshBase": True,
            "nativeSettlementPausedReload": True,
            "settlementKinds": kinds,
            "settlementCaptures": captures,
            "settlementDiagnostics": diagnostics,
            "settlementFixture": "authored funded populated high-speed vessels; not normal fresh progression"}
