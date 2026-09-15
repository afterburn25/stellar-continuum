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
    picket["CurrentSystemId"] = 1
    fleets.append(picket)

    # Fleet 0 cannot be an important vessel (id 0 is invalid); bind it through
    # a matching warp_scout cohort instead.
    galaxy["ActiveCombatEncounter"] = {
        "SystemId": 0, "StartedDay": days,
        "Battle": {
            "BattleId": "0a0b0c0d-0000-4011-8000-1234567890ab",
            "Seed": 4616471093031469151, "Tick": 0, "SimulatedSeconds": 0.0,
            "PendingSeconds": 0.0, "NextEventSequence": 1, "NextSalvoId": 1,
            "Formations": [
                {**_formation(1, player, 1, 1, "Home Guard", -420.0, -120.0,
                              1.0, 0.0, 2,
                              [_vessel(1, "Pioneer One", True)]),
                 "InitialShipCount": 2,
                 "Cohorts": [{"Id": 11, "DesignId": "warp_scout",
                              "InitialCount": 1, "ActiveCount": 1,
                              "Experience": 0.5}]},
                _formation(2, hostile, 6, 6, "Vask Vanguard", 420.0, 120.0,
                           -1.0, 0.0, 0,
                           [_vessel(6, "Vask Dominion Scout", True),
                            _vessel(7, "Vask Dominion Pioneer")]),
                {**_formation(3, hostile, picket_fleet_id, picket_fleet_id,
                              "Vask Picket Line", 140.0, -360.0, -1.0, 0.0,
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


def _diagnostic(stdout: str) -> dict:
    match = re.search(r"(?:^|\s)battle=(\{[^{}]*\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native battle smoke did not report its evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native battle diagnostic is malformed") from error
    for key in ("formations", "own", "foreign", "redacted", "vessels_hidden",
                "own_inexact", "selected", "tokens", "events", "salvos",
                "tick", "order_accepted"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, int):
            raise RuntimeError(f"Native battle reported invalid {key}")
    if state["formations"] < 2 or state["own"] < 1 or state["foreign"] < 1:
        raise RuntimeError("Native battle did not observe both sides")
    if state["redacted"] < 1:
        raise RuntimeError("Foreign formation detail leaked as exact")
    if state["own_inexact"] != 0:
        raise RuntimeError("Own formation detail must stay exact")
    if state["selected"] < 1:
        raise RuntimeError("Battle workspace did not retain its selection")
    if state["tokens"] < 1:
        raise RuntimeError("Battle workspace rendered no formation tokens")
    if state["events"] < 1 or state["tick"] <= 0:
        raise RuntimeError("Tactical simulation did not advance")
    if state["order_accepted"] != 1:
        raise RuntimeError("Battle order was rejected by the engine")
    return state


def _capture(path: Path) -> bytes:
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"Native battle did not capture a frame: {path}")
    if min(data[54:]) == max(data[54:]):
        raise RuntimeError("Native battle capture contains no rendered variation")
    return data


def _encounter(payload: dict, systems: int) -> dict:
    days = payload.get("SimulationDays")
    if (payload.get("FormatVersion") != 17 or
            len(payload.get("Galaxy", {}).get("Systems", [])) != systems or
            isinstance(days, bool) or not isinstance(days, (int, float)) or
            not math.isfinite(days)):
        raise RuntimeError("Battle smoke damaged the campaign payload")
    encounter = payload.get("Galaxy", {}).get("ActiveCombatEncounter")
    if not isinstance(encounter, dict):
        raise RuntimeError("Battle smoke did not persist the active encounter")
    if encounter.get("Reconciled"):
        raise RuntimeError("Persisted encounter reconciled mid-smoke")
    battle = encounter.get("Battle", {})
    formations = battle.get("Formations", [])
    if len(formations) < 3:
        raise RuntimeError("Unobserved picket formation left the authoritative state")
    if not encounter.get("EngagedFormationPairs"):
        raise RuntimeError("Persisted encounter lost its engagement pairs")
    if not encounter.get("Vessels"):
        raise RuntimeError("Persisted encounter lost its fleet bindings")
    return encounter


def validate_native_battle_export(folder: Path, env: dict[str, str],
                                  player17_fixture: Path):
    source = _source_row(player17_fixture)
    systems = source.get("Galaxy", {}).get("Systems", [])
    if source.get("FormatVersion") != 17 or not systems:
        raise RuntimeError("Battle source row is not a Player17 campaign")
    authored = _author_battle_source(source)
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") +
                     os.pathsep + str(system_root))
    captures, diagnostics = [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-battle-") as temporary:
        work = Path(temporary)
        save = work / "battle.player17.json"
        save.write_text(json.dumps(authored, ensure_ascii=False),
                        encoding="utf-8")
        for replay in (False, True):
            capture = work / ("battle-reload.bmp" if replay else
                              "battle-ordered.bmp")
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--load", "--battle-smoke", str(capture)]
            result = subprocess.run(args, cwd=work, env=clean_env,
                                    capture_output=True, text=True,
                                    encoding="utf-8", errors="strict",
                                    timeout=120)
            if result.returncode != 0:
                raise RuntimeError(
                    f"Native battle smoke failed ({result.returncode}):\n"
                    f"{result.stdout}\n{result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", f"systems={len(systems)} ",
                    "save=ok ")):
                raise RuntimeError(
                    "Native battle did not confirm Vulkan, campaign and save")
            state = _diagnostic(result.stdout)
            _capture(capture)
            if not save.is_file():
                raise RuntimeError("Battle smoke did not write its isolated save")
            payload = json.loads(save.read_text(encoding="utf-8"))
            _encounter(payload, len(systems))
            evidence = folder.parent / f"{folder.name}-{capture.name}"
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
    return {"nativeBattleWorkspace": True,
            "nativeBattleObserverRedaction": True,
            "nativeBattleOrder": True,
            "nativeBattleReload": True,
            "battleCaptures": captures,
            "battleDiagnostics": diagnostics}
