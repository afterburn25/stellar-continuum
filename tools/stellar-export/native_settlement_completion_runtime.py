"""Validate earned settlement completion and paused founded-colony reload."""
from __future__ import annotations

import copy
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_bmp import validate_bmp
from native_first_exploration_runtime import _environment, _read_json, _same_paused, _unique_json

_STEP = 1 / 64
_MAX_STEPS = 65536
_DURATION = {"colony": 30.0, "outpost": 20.0}
_FIELDS = {"mode", "player_id", "fleet_id", "system_id", "body_id", "colony_id", "kind",
           "revision", "before_days", "after_days", "steps", "step_days", "colonies_before",
           "colonies_after", "consumed", "opened_colony", "read_only", "feedback", "roundtrip"}


def _number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise RuntimeError(f"Native settlement completion has invalid {name}")
    return float(value)


def _proof(stdout: str, mode: str) -> dict:
    rows = [line for line in stdout.splitlines() if "settlement_completion=" in line]
    if len(rows) != 1:
        raise RuntimeError("Native settlement completion did not report exactly one proof")
    match = re.fullmatch(r"settlement_completion=(\{.*\})", rows[0])
    if not match:
        raise RuntimeError("Native settlement completion proof is malformed")
    try:
        value = _unique_json(match.group(1))
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Native settlement completion proof is malformed") from error
    if not isinstance(value, dict) or set(value) != _FIELDS or value.get("mode") != mode:
        raise RuntimeError("Native settlement completion proof has the wrong schema")
    for key in ("player_id", "fleet_id", "system_id", "body_id", "colony_id", "revision",
                "colonies_before", "colonies_after"):
        if type(value.get(key)) is not int or value[key] < 0:
            raise RuntimeError(f"Native settlement completion has invalid {key}")
    if value["kind"] not in _DURATION:
        raise RuntimeError("Native settlement completion has invalid kind")
    for key in ("before_days", "after_days", "steps", "step_days"):
        _number(value.get(key), key)
    if (not math.isclose(value["step_days"], _STEP, rel_tol=0, abs_tol=1e-15) or
            value["steps"] != round(value["steps"]) or value["steps"] < 0 or
            value["steps"] > _MAX_STEPS):
        raise RuntimeError("Native settlement completion has invalid step evidence")
    elapsed = value["after_days"] - value["before_days"]
    if (elapsed < 0 or elapsed > 1024 or
            not math.isclose(elapsed, value["steps"] * _STEP, rel_tol=0, abs_tol=1e-8)):
        raise RuntimeError("Native settlement completion has invalid clock evidence")
    expected_feedback = mode == "resume"
    for key in ("consumed", "opened_colony", "read_only", "roundtrip", "feedback"):
        if type(value.get(key)) is not bool or value[key] is not (expected_feedback if key == "feedback" else True):
            raise RuntimeError(f"Native settlement completion has invalid {key}")
    if mode == "resume" and (value["steps"] <= 0 or value["colonies_after"] != value["colonies_before"] + 1):
        raise RuntimeError("Native settlement resume did not found exactly one colony")
    if mode == "paused" and (value["steps"] != 0 or value["after_days"] != value["before_days"] or
                               value["colonies_after"] != value["colonies_before"]):
        raise RuntimeError("Native settlement paused reload advanced or reordered state")
    return value


def _player_state(payload):
    galaxy = payload.get("Galaxy")
    if not isinstance(galaxy, dict) or payload.get("FormatVersion") != 17 or galaxy.get("Seed") != 115501:
        raise RuntimeError("Settlement completion source is not Player17 seed 115501")
    player = galaxy.get("PlayerCivilizationId")
    if type(player) is not int:
        raise RuntimeError("Settlement completion source has invalid player")
    players = [x for x in galaxy.get("Civilizations", []) if isinstance(x, dict) and x.get("Id") == player and x.get("IsPlayer") is True]
    if len(players) != 1:
        raise RuntimeError("Settlement completion source has no unique player civilization")
    return galaxy, player


def _surveyed(galaxy, player, system_id):
    knowledge = [x for x in galaxy.get("Knowledge", []) if isinstance(x, dict) and x.get("CivilizationId") == player]
    rows = [x for x in (knowledge[0].get("SystemSurveys", []) if len(knowledge) == 1 else [])
            if isinstance(x, dict) and x.get("SystemId") == system_id]
    if len(rows) != 1 or rows[0].get("Level") != 3 or rows[0].get("Progress") != 1:
        raise RuntimeError("Settlement completion target is not fully surveyed")


def _source_state(payload):
    galaxy, player = _player_state(payload)
    _number(payload.get("SimulationDays"), "simulation days")
    if not isinstance(payload.get("SavedAtUtc"), str):
        raise RuntimeError("Settlement completion source lacks save timestamp")
    fleets = galaxy.get("Fleets", [])
    candidates = [f for f in fleets if isinstance(f, dict) and type(f.get("Id")) is int and
                  f.get("CivilizationId") == player and f.get("IsActive") is True and f.get("Role") == 2 and
                  ((f.get("DestinationSystemId") is not None and f.get("DestinationPlanetaryBodyId") is not None) or
                   (f.get("DestinationSystemId") is None and f.get("CurrentSystemId") is not None and
                    f.get("SettlementBodyId") is not None and f.get("DestinationPlanetaryBodyId") == f.get("SettlementBodyId"))) and
                  _number(f.get("EmbarkedPopulationMillions"), "passengers") > 0]
    if len(candidates) != 1:
        raise RuntimeError("Settlement completion source lacks one active authorized vessel")
    fleet = candidates[0]
    kind = "colony" if fleet.get("DesignId") == "colony_ship" else "outpost" if fleet.get("DesignId") == "resource_outpost_ship" else None
    if kind is None or type(fleet.get("MissionOrderRevision")) is not int:
        raise RuntimeError("Settlement completion source has invalid vessel kind or revision")
    system_id = fleet["DestinationSystemId"] if fleet["DestinationSystemId"] is not None else fleet["CurrentSystemId"]
    body_id = fleet["DestinationPlanetaryBodyId"]
    _surveyed(galaxy, player, system_id)
    if not any(x.get("Id") == body_id and x.get("SystemId") == system_id for x in galaxy.get("PlanetaryBodies", [])):
        raise RuntimeError("Settlement completion source lacks target body")
    if any(x.get("CivilizationId") == player and x.get("PlanetaryBodyId") == body_id for x in galaxy.get("Colonies", [])):
        raise RuntimeError("Settlement completion source target is already owned")
    remaining = _DURATION[kind] - _number(fleet.get("SettlementDaysCompleted"), "settlement progress")
    if remaining < 5 or remaining > _DURATION[kind] or remaining < 0:
        raise RuntimeError("Settlement completion source lacks at least five workdays remaining")
    return {"player_id": player, "fleet_id": fleet["Id"], "system_id": system_id, "body_id": body_id,
            "kind": kind, "revision": fleet["MissionOrderRevision"],
            "colonies": copy.deepcopy(galaxy.get("Colonies", []))}


def _launch(package, work, env, save, capture, flag, width, height):
    args = [str(package / "stellar-continuum-native.exe"), "--asset-root", str(package), "--save-path", str(save),
            "--load", "--seed", "115501", "--width", str(width), "--height", str(height), flag, str(capture)]
    result = subprocess.run(args, cwd=work, env=env, capture_output=True, text=True, encoding="utf-8", errors="strict", timeout=300)
    if result.returncode:
        raise RuntimeError(f"Native settlement completion failed ({result.returncode}):\n{result.stdout[-2000:]}\n{result.stderr[-2000:]}")
    if any(token not in result.stdout for token in ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
        raise RuntimeError("Native settlement completion did not confirm renderer, campaign and save")
    return result


def _bind(before, after, proof, source, mode):
    galaxy, player = _player_state(after)
    if (proof["player_id"], proof["fleet_id"], proof["system_id"], proof["body_id"], proof["kind"]) != \
       (source["player_id"], source["fleet_id"], source["system_id"], source["body_id"], source["kind"]):
        raise RuntimeError("Settlement completion proof changed mission identity")
    fleet = next((f for f in galaxy.get("Fleets", []) if f.get("Id") == source["fleet_id"]), None)
    if not isinstance(fleet, dict) or fleet.get("CivilizationId") != player or fleet.get("Role") != 2 or fleet.get("EmbarkedPopulationMillions") != 0:
        raise RuntimeError("Settlement completion did not consume the colony vessel")
    before_fleet = next((f for f in before.get("Galaxy", {}).get("Fleets", []) if f.get("Id") == source["fleet_id"]), None)
    expected_revision = source["revision"] + 1 if mode == "resume" else (before_fleet or {}).get("MissionOrderRevision")
    if fleet.get("IsActive") is not False or fleet.get("MissionOrderRevision") != expected_revision or proof["revision"] != expected_revision:
        raise RuntimeError("Settlement completion has invalid consumed vessel state")
    colonies = galaxy.get("Colonies", [])
    owned = [c for c in colonies if isinstance(c, dict) and c.get("CivilizationId") == player and c.get("PlanetaryBodyId") == source["body_id"]]
    if len(owned) != 1 or owned[0].get("Id") != proof["colony_id"] or owned[0].get("SystemId") != source["system_id"] or owned[0].get("Kind") != (0 if source["kind"] == "colony" else 1) or _number(owned[0].get("PopulationMillions"), "founded population") <= 0:
        raise RuntimeError("Settlement completion lacks the founded owned settlement")
    if len(colonies) != proof["colonies_after"] or len(before.get("Galaxy", {}).get("Colonies", [])) != proof["colonies_before"]:
        raise RuntimeError("Settlement completion reported incorrect colony counts")
    identity = lambda row: (row.get("Id"), row.get("CivilizationId"), row.get("SystemId"), row.get("PlanetaryBodyId"), row.get("Kind"))
    if mode == "resume" and [identity(x) for x in colonies[:-1]] != [identity(x) for x in source["colonies"]]:
        raise RuntimeError("Settlement completion reordered existing colonies")
    if not math.isclose(proof["before_days"], _number(before.get("SimulationDays"), "before days"), rel_tol=0, abs_tol=1e-8) or not math.isclose(proof["after_days"], _number(after.get("SimulationDays"), "after days"), rel_tol=0, abs_tol=1e-8):
        raise RuntimeError("Settlement completion proof does not match saved time")
    if mode == "paused" and not _same_paused(before, after):
        raise RuntimeError("Paused founded settlement reload changed Player17 payload")


def validate_native_settlement_completion_export(package: Path, env: dict[str, str], source_save: Path):
    source_path = Path(source_save)
    if not source_path.is_file():
        raise RuntimeError("Active settlement source save is missing")
    source_bytes = source_path.read_bytes(); source_payload = _read_json(source_path, "Active settlement source save")
    source = _source_state(source_payload); captures=[]; saves=[]; proofs=[]
    with tempfile.TemporaryDirectory(prefix="stellar-native-settlement-completion-") as temporary:
        work=Path(temporary); save=work / "completion.player17.json"
        shutil.copy2(source_path, save)
        result=_launch(package, work, _environment(env), save, work / "settlement-completion-1280x720.bmp", "--settlement-completion-smoke", 1280, 720)
        proof=_proof(result.stdout,"resume"); after=_read_json(save,"Settlement completion save"); _bind(source_payload,after,proof,source,"resume")
        capture=work / "settlement-completion-1280x720.bmp"; validate_bmp(capture,1280,720,"settlement completion",stdout=result.stdout)
        sidecar=capture.with_name(capture.stem+"-establishment.bmp"); validate_bmp(sidecar,1280,720,"settlement establishment",stdout=result.stdout)
        for artifact in (capture,sidecar):
            dest=package.parent / f"{package.name}-{artifact.name}"; shutil.copy2(artifact,dest); captures.append(str(dest))
        dest=package.parent / f"{package.name}-settlement-completion-1280x720.player17.json"; shutil.copy2(save,dest); saves.append(str(dest)); proofs.append(proof)
        before_paused=after
        result=_launch(package,work,_environment(env),save,work / "settlement-founded-1920x1080.bmp","--settlement-founded-smoke",1920,1080)
        proof=_proof(result.stdout,"paused"); final=_read_json(save,"Founded settlement save"); _bind(before_paused,final,proof,source,"paused")
        capture=work / "settlement-founded-1920x1080.bmp"; validate_bmp(capture,1920,1080,"settlement founded",stdout=result.stdout)
        dest=package.parent / f"{package.name}-{capture.name}"; shutil.copy2(capture,dest); captures.append(str(dest))
        dest=package.parent / f"{package.name}-settlement-founded-1920x1080.player17.json"; shutil.copy2(save,dest); saves.append(str(dest)); proofs.append(proof)
        if source_path.read_bytes()!=source_bytes: raise RuntimeError("Active settlement source save was modified")
    return {"nativeSettlementCompletion":True,"settlementCompletionCaptures":captures,"settlementCompletionSaveCaptures":saves,"settlementCompletionProofs":proofs}
