"""Relocated native freight dispatch and paused-reload proof."""
from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_bmp import validate_bmp
from native_settlement_runtime import _fleet_template


_PROOF_FIELDS = {"mode", "player_id", "colony_id", "body_id", "system_id", "fleet_id",
                 "home_colony_id", "reviewed", "cancelled", "dispatched", "paused",
                 "day_unchanged"}


def _proof(stdout: str, mode: str) -> dict:
    rows = re.findall(r"(?m)^outpost_freight=(\{[^\n]+\})$", stdout)
    if len(rows) != 1:
        raise RuntimeError("Native freight proof is missing or duplicated")
    try:
        def unique_object(pairs):
            result = {}
            for key, item in pairs:
                if key in result:
                    raise ValueError("duplicate JSON key")
                result[key] = item
            return result
        value = json.loads(rows[0], object_pairs_hook=unique_object)
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native freight proof is malformed") from error
    if not isinstance(value, dict) or set(value) != _PROOF_FIELDS or value.get("mode") != mode:
        raise RuntimeError("Native freight proof has an invalid schema")
    for key in ("player_id", "colony_id", "body_id", "system_id", "fleet_id", "home_colony_id"):
        if type(value[key]) is not int or value[key] < 0:
            raise RuntimeError("Native freight proof has invalid identity")
    expected = {"reviewed": mode == "dispatch", "cancelled": mode == "dispatch",
                "dispatched": True, "paused": True, "day_unchanged": True}
    if any(type(value[key]) is not bool or value[key] != expected[key] for key in expected):
        raise RuntimeError("Native freight proof does not prove the requested paused dispatch")
    return value


def _normalized(payload):
    result = copy.deepcopy(payload)
    result.pop("SavedAtUtc", None)
    return result


def _player(payload):
    galaxy = payload.get("Galaxy", {})
    player_id = galaxy.get("PlayerCivilizationId")
    player = next((row for row in galaxy.get("Civilizations", []) if row.get("Id") == player_id), None)
    home = next((row for row in galaxy.get("Colonies", [])
                 if row.get("CivilizationId") == player_id and row.get("Kind") == 0), None)
    if not isinstance(player_id, int) or player is None or home is None:
        raise RuntimeError("Freight fixture lacks a player and developed home colony")
    return galaxy, player_id, player, home


def _author(base):
    result = copy.deepcopy(base)
    galaxy, player_id, player, home = _player(result)
    home_system = home.get("SystemId")
    knowledge = next((row for row in galaxy.get("Knowledge", [])
                      if row.get("CivilizationId") == player_id), None)
    known = set(knowledge.get("KnownSystemIds", [])) if knowledge else set()
    surveyed = {row.get("SystemId") for row in (knowledge or {}).get("SystemSurveys", [])
                if isinstance(row, dict) and row.get("Level", 0) == 3}
    occupied = {row.get("PlanetaryBodyId") for row in galaxy.get("Colonies", [])}
    # Validation-only deposit on an otherwise unoccupied, fully surveyed home body.
    # No live generation or save migration code authors this state.
    body = next((body for body in galaxy.get("PlanetaryBodies", [])
                 if body.get("SystemId") == home_system and home_system in known and
                 home_system in surveyed and body.get("Id") not in occupied and
                 body.get("Environment", {}).get("HasSolidSurface") is True), None)
    if body is None:
        raise RuntimeError("Freight fixture has no unoccupied surveyed solid home-system body")
    body["HasRareResource"] = True
    colony_ids = [row.get("Id") for row in galaxy.get("Colonies", []) if type(row.get("Id")) is int]
    outpost_id = max(colony_ids, default=-1) + 1
    home_template = copy.deepcopy(home)
    outpost = home_template
    outpost.update({
        "Id": outpost_id, "CivilizationId": player_id, "SystemId": body["SystemId"],
        "PlanetaryBodyId": body["Id"], "Name": "Authored Freight Outpost", "Kind": 1,
        "PopulationSpeciesId": player.get("SpeciesId", "terran_baseline"),
        "PopulationMillions": 0.8, "Infrastructure": 0.15, "Stability": 0.85,
        "StoredFoodPopulationDaysMillions": 0.8 * 30.0,
        "StoredWaterPopulationDaysMillions": 0.8 * 30.0,
        "StoredExtractedMaterials": 20.0, "RemainingExtractableMaterials": 900.0,
        "SurfaceHubLevel": 1, "SurfaceHubUpgradeDaysRemaining": 0,
        "SurfaceBuildings": [],
    })
    galaxy["Colonies"] = [outpost] + [row for row in galaxy.get("Colonies", []) if row.get("Id") != outpost_id]
    fleets = galaxy.setdefault("Fleets", [])
    if any(fleet.get("CivilizationId") == player_id and fleet.get("DesignId") == "bulk_freighter"
           for fleet in fleets):
        raise RuntimeError("Freight fixture already contains a player bulk freighter")
    fleet_id = max((row.get("Id", -1) for row in fleets if type(row.get("Id")) is int), default=-1) + 1
    home_star = next((row for row in galaxy.get("Systems", []) if row.get("Id") == home_system), None)
    if home_star is None:
        raise RuntimeError("Freight fixture has no home system")
    fleet_template = _fleet_template(fleet_id, player_id, player["SpeciesId"], home_system,
                                     home_star["X"], home_star["Y"], "outpost")
    fleet_template.update({"Name": "Authored Bulk Freighter", "Role": 4,
                           "DesignId": "bulk_freighter", "CargoMaterialCapacity": 1000.0,
                           "EmbarkedPopulationMillions": 0.0,
                           "EmbarkedPopulationSpeciesId": None})
    fleets.insert(0, fleet_template)
    return result, {"player_id": player_id, "colony_id": outpost_id, "body_id": body["Id"],
                    "system_id": body["SystemId"], "fleet_id": fleet_id, "home_colony_id": home["Id"]}


def _run(args, work, clean, label):
    result = subprocess.run(args, cwd=work, env=clean, capture_output=True, text=True, encoding="utf-8", timeout=120)
    if result.returncode != 0:
        raise RuntimeError(f"Native freight {label} failed ({result.returncode}): {result.stdout}\n{result.stderr}")
    if any(token not in result.stdout for token in ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
        raise RuntimeError(f"Native freight {label} lacked renderer/campaign/save proof")
    return result


def _validate_identity(payload, ids, proof):
    galaxy = payload.get("Galaxy", {})
    if any(proof[key] != ids[key] for key in ids):
        raise RuntimeError("Native freight proof identity differs from the authored fixture")
    outposts = [row for row in galaxy.get("Colonies", []) if row.get("Id") == ids["colony_id"]]
    fleets = [row for row in galaxy.get("Fleets", []) if row.get("Id") == ids["fleet_id"]]
    bodies = [row for row in galaxy.get("PlanetaryBodies", []) if row.get("Id") == ids["body_id"]]
    homes = [row for row in galaxy.get("Colonies", []) if row.get("Id") == ids["home_colony_id"]]
    if (len(outposts) != 1 or outposts[0].get("Kind") != 1 or
            outposts[0].get("CivilizationId") != ids["player_id"] or
            outposts[0].get("SystemId") != ids["system_id"] or
            outposts[0].get("PlanetaryBodyId") != ids["body_id"] or
            len(bodies) != 1 or bodies[0].get("HasRareResource") is not True or
            bodies[0].get("Environment", {}).get("HasSolidSurface") is not True or
            not isinstance(outposts[0].get("StoredExtractedMaterials"), (int, float)) or
            outposts[0]["StoredExtractedMaterials"] <= 0 or
            not isinstance(outposts[0].get("RemainingExtractableMaterials"), (int, float)) or
            outposts[0]["RemainingExtractableMaterials"] <= 0 or
            len(homes) != 1 or homes[0].get("Kind") != 0 or homes[0].get("Id") == outposts[0].get("Id")):
        raise RuntimeError("Native freight proof does not identify the owned resource outpost")
    if len(fleets) != 1 or fleets[0].get("CivilizationId") != ids["player_id"] or fleets[0].get("DesignId") != "bulk_freighter":
        raise RuntimeError("Native freight proof does not identify the owned bulk freighter")


def _assert_dispatch_delta(before, after, ids):
    before_view, after_view = _normalized(before), _normalized(after)
    allowed = {"DestinationSystemId", "TransitPhase", "TransitOriginSystemId",
               "TransitTargetSystemId", "TransitProgress", "LocalTransitStartX",
               "LocalTransitStartY", "LocalTransitPositionX", "LocalTransitPositionY",
               "LocalTransitTargetX", "LocalTransitTargetY", "PlannedRouteSystemIds",
               "MissionOrderRevision", "FreightTargetOutpostId", "FreightHomeColonyId",
            }
    for payload in (before_view, after_view):
        fleet = next((row for row in payload.get("Galaxy", {}).get("Fleets", [])
                      if row.get("Id") == ids["fleet_id"]), None)
        if fleet is None:
            raise RuntimeError("Native freight payload lost the authored freighter")
        for key in allowed:
            fleet.pop(key, None)
    if before_view != after_view:
        raise RuntimeError("Native freight dispatch changed unauthorized campaign state")
    fleet = next(row for row in after.get("Galaxy", {}).get("Fleets", [])
                 if row.get("Id") == ids["fleet_id"])
    if (fleet.get("FreightTargetOutpostId") != ids["colony_id"] or
            fleet.get("FreightHomeColonyId") != ids["home_colony_id"] or
            fleet.get("CargoMaterials") != 0):
        raise RuntimeError("Native freight dispatch has invalid route or cargo state")


def validate_native_freight_export(folder: Path, env: dict[str, str]):
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics = [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-freight-") as temporary:
        work = Path(temporary)
        base_save = work / "base.player17.json"
        base_capture = work / "base.bmp"
        base = _run([str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                     "--save-path", str(base_save), "--width", "1280", "--height", "720",
                     "--colony-smoke", str(base_capture)], work, clean, "base")
        if not base_save.is_file():
            raise RuntimeError("Native freight base launch wrote no Player17 save")
        base_payload = json.loads(base_save.read_text(encoding="utf-8-sig"))
        for width, height in ((1280, 720), (1920, 1080)):
            authored, ids = _author(base_payload)
            save = work / f"freight-{width}x{height}.player17.json"
            save.write_text(json.dumps(authored, ensure_ascii=False), encoding="utf-8")
            capture = work / f"freight-{width}x{height}.bmp"
            result = _run([str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                           "--save-path", str(save), "--load", "--width", str(width), "--height", str(height),
                           "--colony-smoke", str(capture)], work, clean, "dispatch")
            proof = _proof(result.stdout, "dispatch")
            _validate_identity(json.loads(save.read_text(encoding="utf-8-sig")), ids, proof)
            validate_bmp(capture, width, height, "freight", stdout=result.stdout)
            review = capture.with_name(capture.stem + "-freight-review.bmp")
            validate_bmp(review, width, height, "freight review", stdout=result.stdout)
            after_dispatch = json.loads(save.read_text(encoding="utf-8-sig"))
            _assert_dispatch_delta(authored, after_dispatch, ids)
            if after_dispatch.get("SimulationDays") != 0:
                raise RuntimeError("Native freight dispatch advanced simulation time")
            review_evidence = folder.parent / f"{folder.name}-freight-{width}x{height}-review.bmp"
            shutil.copy2(review, review_evidence); captures.append(str(review_evidence))
            evidence = folder.parent / f"{folder.name}-freight-{width}x{height}.bmp"
            shutil.copy2(capture, evidence); captures.append(str(evidence)); diagnostics.append(result.stdout.strip())
            reload_before = copy.deepcopy(after_dispatch)
            reload_capture = work / f"freight-{width}x{height}-reload.bmp"
            loaded = _run([str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                           "--save-path", str(save), "--load", "--width", str(width), "--height", str(height),
                           "--colony-reload-smoke", str(reload_capture)], work, clean, "reload")
            reload_proof = _proof(loaded.stdout, "reload")
            _validate_identity(json.loads(save.read_text(encoding="utf-8-sig")), ids, reload_proof)
            validate_bmp(reload_capture, width, height, "freight reload", stdout=loaded.stdout)
            reload_evidence = folder.parent / f"{folder.name}-freight-{width}x{height}-reload.bmp"
            shutil.copy2(reload_capture, reload_evidence)
            captures.append(str(reload_evidence))
            reloaded = json.loads(save.read_text(encoding="utf-8-sig"))
            if _normalized(reloaded) != _normalized(reload_before):
                raise RuntimeError("Native freight paused reload changed the authored payload")
            diagnostics.append(loaded.stdout.strip())
    return {"nativeFreightDispatch": True, "nativeFreightPausedReload": True,
            "freightCaptures": captures, "freightDiagnostics": diagnostics}
