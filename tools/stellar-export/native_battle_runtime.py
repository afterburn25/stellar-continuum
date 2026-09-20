"""Observer-safe native tactical battle validation against a real campaign.

The reviewed Player17 fixture carries eight fleets and four civilizations but
no active combat. The validator authors a synthetic two-front engagement: an
engaged hostile formation whose detail stays inexact, and a third unengaged
hostile picket that must remain entirely outside the player observer snapshot.
It then drives the packaged native client through the tactical workspace:
formation selection, a live order, pause/resume and a mid-battle manual save.
The replay pass reloads that save and proves the encounter round-trips while
the unobserved picket formation persists in the authoritative state.
"""
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
import struct

from native_client_runtime import _validate_capture

TICKS_PER_SIMULATION_DAY = 1000


def _source_row(fixture: Path) -> dict:
    root = json.loads(fixture.read_text(encoding="utf-8"))
    rows = root.get("Rows")
    if not isinstance(rows, list):
        raise RuntimeError("Player17 battle fixture has no source-authored rows")
    matches = [row for row in rows if row.get("Name") == "valid-current17"]
    if len(matches) != 1 or not isinstance(matches[0].get("InputJson"), str):
        raise RuntimeError("Player17 battle fixture has no unique valid-current17 input")
    return json.loads(matches[0]["InputJson"])


def _point(x: float, y: float) -> dict:
    return {"X": x, "Y": y, "Vector": {}, "IsFinite": True}


def _loadout() -> dict:
    return {"MassPerShip": 100, "Acceleration": 18, "MaximumSpeed": 120,
            "ShieldPerShip": 35, "ArmorPerShip": 45, "HullPerShip": 95,
            "ReactorOutputPerShip": 100, "CoolingPerShip": 28,
            "WarpStabilization": 50, "WarpSpoolSeconds": 12,
            "ModuleSlotCapacity": 12, "MaximumModuleMass": 420,
            "Weapons": [{"Id": "beam_battery", "Kind": 0, "MountsPerShip": 1,
                         "DamagePerShot": 28, "ShotsPerSecond": 0.16666667,
                         "Range": 700, "Accuracy": 0.65, "PowerPerSecond": 3,
                         "HeatPerSecond": 2}],
            "Modules": [{"Id": "reactor", "Kind": 0, "InstalledCount": 1,
                         "MassEach": 18, "PowerPerSecondEach": 0,
                         "HeatPerSecondEach": 0, "Condition": 1,
                         "Enabled": True, "EffectiveRange": 0,
                         "FieldStrength": 100, "DetectionSignature": 0,
                         "Slots": 1},
                        {"Id": "warp_drive", "Kind": 3, "InstalledCount": 1,
                         "MassEach": 22, "PowerPerSecondEach": 12,
                         "HeatPerSecondEach": 0, "Condition": 1,
                         "Enabled": True, "EffectiveRange": 0,
                         "FieldStrength": 0, "DetectionSignature": 0,
                         "Slots": 1}]}


def _vessel(vessel_id: int, name: str, flagship: bool = False) -> dict:
    return {"Id": vessel_id, "Name": name, "DesignId": "warp_scout",
            "IsFlagship": flagship, "IsCarrier": False, "IsInterdictor": False,
            "IsStoryShip": False, "HullFraction": 1.0, "EngineFraction": 1.0,
            "SensorFraction": 1.0, "WarpDriveFraction": 1.0,
            "ReactorFraction": 1.0, "InterdictorFraction": 1.0,
            "BattlesFought": 0, "ConfirmedKills": 0, "Destroyed": False,
            "Escaped": False}


def _formation(formation_id: int, civilization: int, fleet_id: int,
               task_force_id: int, name: str, x: float, y: float,
               hx: float, hy: float, shape: int, vessels: list) -> dict:
    return {"Id": formation_id, "CivilizationId": civilization,
            "FleetId": fleet_id, "TaskForceId": task_force_id, "Name": name,
            "Position": _point(x, y), "Velocity": _point(6.0, 0.0),
            "Heading": _point(hx, hy), "Objective": _point(0, 0),
            "Shape": shape, "Order": 0, "TargetFormationId": None,
            "ProtectedFormationId": None, "InterdictorProtection": 1,
            "Cohesion": 1.0, "Morale": 1.0,
            "ShieldPool": 35.0 * len(vessels),
            "ArmorPool": 45.0 * len(vessels),
            "HullPool": 95.0 * len(vessels),
            "HullLossThresholdPerShip": 95.0, "Heat": 0.0,
            "PowerReserve": 1.0, "WarpSpoolProgress": 0.0,
            "WarpBlocked": False, "Escaped": False, "Surrendered": False,
            "InitialShipCount": len(vessels), "DestroyedShips": 0,
            "HullDamageRemainder": 0.0, "Loadout": _loadout(),
            "Cohorts": [], "ImportantVessels": vessels}


def _author_battle_source(source: dict) -> dict:
    authored = copy.deepcopy(source)
    galaxy = authored.get("Galaxy", {})
    diplomacy = authored.get("Diplomacy", {})
    days = authored.get("SimulationDays")
    player = galaxy.get("PlayerCivilizationId")
    fleets = galaxy.get("Fleets")
    civilizations = {civ.get("Id") for civ in galaxy.get("Civilizations", [])
                     if isinstance(civ, dict)}
    if (authored.get("FormatVersion") != 17 or not galaxy.get("Systems") or
            not isinstance(diplomacy, dict) or
            isinstance(days, bool) or not isinstance(days, (int, float)) or
            not math.isfinite(days) or
            isinstance(player, bool) or not isinstance(player, int) or
            not isinstance(fleets, list) or len(fleets) < 8 or
            player not in civilizations or 3 not in civilizations or
            not isinstance(diplomacy.get("Contacts"), list) or
            not isinstance(diplomacy.get("Relationships"), list)):
        raise RuntimeError("Battle source row is not a Player17 campaign")
    fleet_ids = {fleet.get("Id") for fleet in fleets
                 if isinstance(fleet, dict)}
    for required in (0, 1, 6, 7):
        if required not in fleet_ids:
            raise RuntimeError("Battle fixture lacks a bindable fleet")
    hostile = 3
    picket_fleet_id = max(fleet_ids) + 1
    tick = round(days * TICKS_PER_SIMULATION_DAY)
    own_fleet = next(fleet for fleet in fleets if fleet.get("Id") == 0)
    own_fleet["Role"] = 3
    own_fleet["DesignId"] = "patrol_corvette"
    fleet_one = next(fleet for fleet in fleets if fleet.get("Id") == 1)
    fleet_one["DesignId"] = "colony_ship"

    # The identified contact gives the war relationship its legitimate basis.
    diplomacy["Contacts"].append({
        "ObserverCivilizationId": player,
        "ContactId": "vask-hostile-signal", "TargetCivilizationId": hostile,
        "FirstObservedTick": tick - 8, "LastObservedTick": tick,
        "LastObservedSystemId": 1, "Awareness": 2, "Condition": 1,
        "CommunicationAvailable": False, "Confidence": 0.9})
    diplomacy["Relationships"].append({
        "CivilizationAId": player, "CivilizationBId": hostile,
        "PoliticalState": 3, "Trust": 0.0, "Hostility": 0.9, "Fear": 0.2,
        "Respect": 0.0, "Cooperation": 0.0,
        "Grievances": [{"CreatedAtTick": 12, "SourceCivilizationId": hostile,
                        "Severity": 0.8, "Reason": "border incursion"}]})

    # The third hostile formation stays unengaged: it must never enter the
    # player observer snapshot while remaining in the authoritative save.
    picket = copy.deepcopy(fleets[6])
    picket["Id"] = picket_fleet_id
    picket["Name"] = "Vask Picket"
    picket["CurrentSystemId"] = 0
    for fleet in fleets:
        if fleet["Id"] in (0, 1, 6, 7):
            fleet["CurrentSystemId"] = 0
            fleet["DestinationSystemId"] = None
    fleets.append(picket)

    # Use the native reserved important-vessel identity for strategic fleet 0.
    # Fleet 1 remains a colony ship, without an invented military sprite.
    galaxy["ActiveCombatEncounter"] = {
        "SystemId": 0, "StartedDay": days,
        "Battle": {
            "BattleId": "0a0b0c0d-0000-4011-8000-1234567890ab",
            "Seed": 4616471093031469151, "Tick": 0, "SimulatedSeconds": 0.0,
            "PendingSeconds": 0.0, "NextEventSequence": 1, "NextSalvoId": 1,
            "Formations": [
                {**_formation(1, player, 1, 1, "Home Guard", -420.0, -120.0,
                              .6, .8, 2,
                              [{**_vessel(4294967296, "Home Guard Corvette", True), "DesignId": "patrol_corvette"},
                               {**_vessel(1, "Pioneer One"), "DesignId": "colony_ship"}])},
                _formation(2, hostile, 6, 6, "Vask Vanguard", 420.0, 120.0,
                           -1.0, 0.0, 0,
                           [_vessel(6, "Vask Dominion Scout", True),
                            _vessel(7, "Vask Dominion Pioneer")]),
                {**_formation(3, hostile, picket_fleet_id, picket_fleet_id,
                              "Vask Picket Line", 3000.0, -3600.0, -1.0, 0.0,
                              3, []),
                 "Cohorts": [{"Id": 31, "DesignId": "warp_scout",
                              "InitialCount": 1, "ActiveCount": 1,
                              "Experience": 0.5}]},
            ],
            "Events": [], "ActiveSalvos": []},
        "Vessels": [{"FleetId": 0, "FormationId": 1},
                    {"FleetId": 1, "FormationId": 1},
                    {"FleetId": 6, "FormationId": 2},
                    {"FleetId": 7, "FormationId": 2},
                    {"FleetId": picket_fleet_id, "FormationId": 3}],
        "EngagedFormationPairs": [{"FirstFormationId": 1,
                                   "SecondFormationId": 2}],
        "LastObservedEventSequence": 0, "Reconciled": False}
    return authored


_BOOL_KEYS = {"reload", "paused_speed", "menu_pause", "canonical_unchanged",
              "day_unchanged", "order_accepted"}
_INT_KEYS = {"own", "foreign", "foreign_exact", "hidden_formations", "tick", "tokens"}
_POINT_KEYS = {"sample_x", "sample_y"}


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate battle evidence key: {key}")
        result[key] = value
    return result


def battle_proof(stdout: str, replay: bool, width: int, height: int) -> dict:
    markers = re.findall(r"(?:^|\s)battle=([^\r\n]*)", stdout)
    if len(markers) != 1:
        raise RuntimeError("Native battle evidence is missing or duplicated")
    try:
        state = json.loads(markers[0], object_pairs_hook=_unique)
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native battle evidence is malformed") from error
    if (not isinstance(state, dict) or set(state) != _BOOL_KEYS | _INT_KEYS | _POINT_KEYS or
            any(type(state[key]) is not bool for key in _BOOL_KEYS) or
            any(type(state[key]) is not int for key in _INT_KEYS) or
            any(type(state[key]) not in (int, float) or not math.isfinite(state[key])
                for key in _POINT_KEYS)):
        raise RuntimeError("Native battle evidence has an invalid schema")
    if (state["reload"] != replay or state["order_accepted"] != (not replay) or
            not all(state[key] for key in _BOOL_KEYS - {"reload", "order_accepted"}) or
            state["own"] < 1 or state["foreign"] < 1 or state["foreign_exact"] != 0 or
            state["hidden_formations"] < 1 or state["tick"] <= 0 or
            not 1 <= state["tokens"] <= 4096 or
            not 60 < state["sample_x"] < width - 60 or
            not 100 < state["sample_y"] < height - 100):
        raise RuntimeError("Native battle replay failed its observation or timing contract")
    return state

def battle_art_proof(stdout: str) -> dict:
    rows = re.findall(r"(?m)^battle_art=(\{[^\n]+\})$", stdout)
    if len(rows) != 1: raise RuntimeError("Native battle art evidence is missing or duplicated")
    try: state = json.loads(rows[0], object_pairs_hook=_unique)
    except (TypeError, ValueError) as error: raise RuntimeError("Native battle art evidence is malformed") from error
    fields = {"sprites", "foreign_sprites", "source_width", "source_height", "source_bytes", "alpha_pixels", "center_x", "center_y", "size", "heading_degrees", "moving", "paused_unchanged", "ship_selected"}
    if not isinstance(state, dict) or set(state) != fields: raise RuntimeError("Native battle art evidence has an invalid schema")
    if any(type(state[k]) is not int or state[k] < 0 for k in ("sprites", "foreign_sprites", "source_width", "source_height", "source_bytes", "alpha_pixels")):
        raise RuntimeError("Native battle art counters are invalid")
    if any(type(state[k]) not in (int, float) or isinstance(state[k], bool) for k in ("center_x", "center_y", "size", "heading_degrees")):
        raise RuntimeError("Native battle art geometry is invalid")
    if (not 1 <= state["sprites"] <= 32 or state["foreign_sprites"] != 0 or
            not 1 <= state["source_width"] <= 2048 or not 1 <= state["source_height"] <= 2048 or
            state["source_bytes"] != state["source_width"] * state["source_height"] * 4 or state["source_bytes"] > 16 * 1024 * 1024 or
            not .2 * state["source_width"] * state["source_height"] <= state["alpha_pixels"] <= .95 * state["source_width"] * state["source_height"] or
            not all(math.isfinite(state[k]) and abs(state[k]) <= 100000 for k in ("center_x", "center_y", "size", "heading_degrees")) or
            state["size"] <= 0 or state["size"] > 512 or state["paused_unchanged"] is not True or state["ship_selected"] is not True or type(state["moving"]) is not bool):
        raise RuntimeError("Native battle art evidence violates its bounded contract")
    return state

def _art_rows(path: Path, width: int, height: int):
    # Keep two bounded byte buffers, not millions of per-pixel Python objects.
    _validate_capture(path, width, height)
    data = path.read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    signed_height = struct.unpack_from("<i", data, 22)[0]
    channels = struct.unpack_from("<H", data, 28)[0] // 8
    stride = ((width * channels + 3) // 4) * 4
    pixels = memoryview(data)
    return channels, [pixels[offset + (y if signed_height < 0 else height - y - 1) * stride:
                             offset + (y if signed_height < 0 else height - y - 1) * stride + width * channels]
                      for y in range(height)]


def _validate_art_difference(base_path: Path, suppressed_path: Path, width: int, height: int, art: dict):
    channels_a, base_rows = _art_rows(base_path, width, height)
    channels_b, suppressed_rows = _art_rows(suppressed_path, width, height)
    side = art["size"]
    radius = side * .75
    cx, cy = art["center_x"], art["center_y"]
    if not (0 < side <= 512 and all(math.isfinite(v) for v in (side, cx, cy)) and
            cx + radius >= 0 and cy + radius >= 0 and cx - radius < width and cy - radius < height):
        raise RuntimeError("Native battle art sprite is outside the viewport")
    angle = math.radians(art["heading_degrees"])
    cosine, sine = math.cos(angle), math.sin(angle)
    inside = outside = corners = 0
    for y, (base, suppressed) in enumerate(zip(base_rows, suppressed_rows)):
        if channels_a == channels_b and base == suppressed:
            continue
        for x in range(width):
            if base[x * channels_a:x * channels_a + 3] != suppressed[x * channels_b:x * channels_b + 3]:
                dx, dy = x - cx, y - cy
                if abs(dx) <= radius and abs(dy) <= radius:
                    inside += 1
                    local_x, local_y = dx * cosine + dy * sine, -dx * sine + dy * cosine
                    if abs(local_x) > side * .44 and abs(local_y) > side * .44:
                        corners += 1
                else:
                    outside += 1
    if inside <= 25 or outside or corners:
        raise RuntimeError("Native battle ship sidecar did not isolate transparent sprite pixels")
    return inside


def _visible_owned_token(path: Path, width: int, height: int, state: dict) -> None:
    _validate_capture(path, width, height)
    data = path.read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    signed_height = struct.unpack_from("<i", data, 22)[0]
    pixel_bytes = struct.unpack_from("<H", data, 28)[0] // 8
    stride = ((width * pixel_bytes + 3) // 4) * 4
    x, y = round(state["sample_x"]), round(state["sample_y"])
    green_pixels = 0
    # The local patch excludes selection rings (radius 25) and nearby labels.
    # A counter alone cannot catch a token hidden by an opaque overlay.
    for py in range(max(0, y - 14), min(height, y + 15)):
        row = py if signed_height < 0 else height - py - 1
        for px in range(max(0, x - 14), min(width, x + 15)):
            at = offset + row * stride + px * pixel_bytes
            b, g, r = data[at:at + 3]
            if g >= 120 and g > r * 1.25 and g > b * 1.05:
                green_pixels += 1
    if green_pixels < 8:
        raise RuntimeError("Native battle formation tokens are hidden or missing in the captured frame")


def _payload(path: Path, systems: int) -> dict:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_unique)
    except (OSError, ValueError) as error:
        raise RuntimeError("Battle replay did not produce a valid save") from error
    if (not isinstance(payload, dict) or payload.get("FormatVersion") != 17 or
            not payload.get("SavedAtUtc") or
            len(payload.get("Galaxy", {}).get("Systems", [])) != systems):
        raise RuntimeError("Battle replay changed its campaign format or systems")
    encounter = payload.get("Galaxy", {}).get("ActiveCombatEncounter")
    if (not isinstance(encounter, dict) or encounter.get("Reconciled") is not False or
            len(encounter.get("Battle", {}).get("Formations", [])) != 3 or
            len(encounter.get("Vessels", [])) != 5 or
            not encounter.get("EngagedFormationPairs")):
        raise RuntimeError("Battle replay lost its active encounter, bindings or hidden formation")
    payload.pop("SavedAtUtc")
    return payload


def _same_paused_payload(before: dict, after: dict) -> None:
    if before != after:
        raise RuntimeError("Paused battle reload changed canonical campaign state")


def validate_native_battle_export(folder: Path, env: dict[str, str], player17_fixture: Path):
    folder = folder.resolve()
    source = _source_row(player17_fixture)
    authored = _author_battle_source(source)
    count = len(source["Galaxy"]["Systems"])
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics, art_diagnostics, art_captures = [], [], [], []
    prior = None
    with tempfile.TemporaryDirectory(prefix="stellar-native-battle-") as temporary:
        work = Path(temporary)
        save = work / "battle.player17.json"
        save.write_text(json.dumps(authored, ensure_ascii=False), encoding="utf-8")
        for replay, width, height in ((False, 1280, 720), (True, 1920, 1080)):
            mode = "battle-reload" if replay else "battle-ordered"
            capture = work / f"{mode}.bmp"
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--load", "--width", str(width), "--height", str(height),
                    "--battle-reload-smoke" if replay else "--battle-smoke", str(capture)]
            result = subprocess.run(args, cwd=work, env=clean_env, capture_output=True,
                                    text=True, encoding="utf-8", errors="strict", timeout=120)
            evidence = folder.parent / f"{folder.name}-{mode}"
            evidence.with_name(evidence.name + ".log").write_text(result.stdout + "\n" + result.stderr, encoding="utf-8")
            if result.returncode != 0:
                raise RuntimeError(f"Native battle replay failed ({result.returncode}):\n"
                                   f"{result.stdout}\n{result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", f"systems={count} ", "save=ok ")):
                raise RuntimeError("Native battle did not confirm Vulkan, campaign and save")
            proof = battle_proof(result.stdout, replay, width, height)
            art = battle_art_proof(result.stdout)
            if art["sprites"] != 1:
                raise RuntimeError("Native battle art fixture must contain exactly one authored sprite")
            sidecar = capture.with_name(capture.stem + "-without-ships.bmp")
            _validate_capture(sidecar, width, height)
            _validate_art_difference(capture, sidecar, width, height, art)
            _visible_owned_token(capture, width, height, proof)
            payload = _payload(save, count)
            battle = payload["Galaxy"]["ActiveCombatEncounter"]["Battle"]
            if battle["Tick"] != proof["tick"] or payload["SimulationDays"] != source["SimulationDays"]:
                raise RuntimeError("Battle evidence disagrees with persisted time")
            if prior is not None:
                _same_paused_payload(prior, payload)
            prior = payload
            shutil.copy2(save, evidence.with_name(evidence.name + ".player17.json"))
            shutil.copy2(capture, evidence.with_name(evidence.name + ".bmp"))
            shutil.copy2(sidecar, evidence.with_name(evidence.name + "-without-ships.bmp"))
            captures.append(str(evidence.with_name(evidence.name + ".bmp")))
            diagnostics.append(proof)
            art_diagnostics.append(art)
            art_captures.append(str(evidence.with_name(evidence.name + "-without-ships.bmp")))
    return {"nativeBattleWorkspace": True, "nativeBattleObserverRedaction": True,
            "nativeBattleVisibleTokens": True, "nativeBattleOrder": True,
            "nativeBattlePausedSpeed": True, "nativeBattlePausedCanonicalReload": True,
            "nativeBattleShipArtwork": True, "battleArtDiagnostics": art_diagnostics,
            "battleArtSuppressedCaptures": art_captures,
            "battleCaptures": captures, "battleDiagnostics": diagnostics}
