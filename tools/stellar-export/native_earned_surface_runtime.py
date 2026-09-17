"""Durable proof for building the earned colony's fabricator through native UI."""
from __future__ import annotations

import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_bmp import validate_bmp
from native_first_exploration_runtime import _environment, _read_json, _same_paused, _unique_json


_FIELDS = {"mode", "player_id", "system_id", "body_id", "colony_id", "building_id",
           "type_id", "x", "z", "rotation", "before_days", "after_days", "steps",
           "step_days", "authorization", "treasury_before", "treasury_after",
           "industry_cost", "industry_progress", "complete", "powered", "staffed",
           "enabled", "efficiency", "industry_before", "industry_after",
           "cancel_unchanged", "opened_surface", "roundtrip"}
_STEP = 1 / 64
_MAX_STEPS = 1024 * 64
_IDENTITY_FIELDS = ("player_id", "system_id", "body_id", "colony_id")


def _colony_identity(colony):
    return tuple(_integer(colony.get(key), key) for key in
                 ("CivilizationId", "SystemId", "PlanetaryBodyId", "Id"))


def _construction_multiplier(payload, colony):
    """Independently verify the existing surface authorization terms from save data."""
    bodies = payload.get("Galaxy", {}).get("PlanetaryBodies", [])
    body = next((b for b in bodies if isinstance(b, dict) and
                 b.get("Id") == colony.get("PlanetaryBodyId") and
                 b.get("SystemId") == colony.get("SystemId")), None)
    if body is None:
        return 1.0  # Canonical legacy behavior when there is no body record.
    environment = body.get("Environment", {})
    gravity, pressure, temperature, radiation = (
        _number(environment.get(key), key) for key in
        ("GravityG", "PressureKPa", "TemperatureKelvin", "RadiationHazard"))
    factor = 1 + min(.35, abs(gravity - 1) * .20)
    factor += .20 if environment.get("Atmosphere") == 0 else 0
    factor += .15 if pressure < 20 or pressure > 300 else 0
    factor += .15 if temperature < 240 or temperature > 330 else 0
    factor += min(.15, (radiation - .10) * .30) if radiation > .10 else 0
    return math.floor(min(2., max(1., factor)) * 100 + .5) / 100


def _number(value, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise RuntimeError(f"Earned surface has invalid {name}")
    return float(value)


def _integer(value, name: str, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise RuntimeError(f"Earned surface has invalid {name}")
    return value


def _proof(stdout: str, mode: str, authorization: float = 50.0) -> dict:
    rows = re.findall(r"(?m)^earned_surface=(\{[^\n]+\})$", stdout)
    if len(rows) != 1:
        raise RuntimeError("Earned surface did not report exactly one proof")
    try:
        proof = _unique_json(rows[0])
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Earned surface proof is malformed") from error
    if not isinstance(proof, dict) or set(proof) != _FIELDS or proof.get("mode") != mode:
        raise RuntimeError("Earned surface proof has the wrong schema")
    for key in _IDENTITY_FIELDS:
        _integer(proof.get(key), key)
    if proof["player_id"] != 0 or proof["colony_id"] != 9:
        raise RuntimeError("Earned surface proof has the wrong player or earned colony")
    _integer(proof.get("building_id"), "building_id")
    if not isinstance(proof.get("type_id"), str) or proof["type_id"] != "fabricator":
        raise RuntimeError("Earned surface proof has the wrong building type")
    for key in ("x", "z", "rotation", "before_days", "after_days", "step_days",
                "authorization", "treasury_before", "treasury_after", "industry_cost",
                "industry_progress", "efficiency", "industry_before", "industry_after"):
        _number(proof.get(key), key)
    steps = _integer(proof.get("steps"), "steps")
    if (proof["before_days"] < 0 or proof["after_days"] < proof["before_days"] or
            steps > _MAX_STEPS or not math.isclose(proof["step_days"], _STEP, rel_tol=0, abs_tol=1e-15) or
            not math.isclose(proof["after_days"] - proof["before_days"], steps * _STEP, rel_tol=0, abs_tol=1e-7)):
        raise RuntimeError("Earned surface proof has invalid time evidence")
    if any(type(proof.get(key)) is not bool for key in
           ("complete", "powered", "staffed", "enabled", "cancel_unchanged", "opened_surface", "roundtrip")):
        raise RuntimeError("Earned surface proof has invalid boolean evidence")
    if proof["industry_cost"] != 450 or proof["industry_progress"] < 0 or proof["efficiency"] < 0:
        raise RuntimeError("Earned surface proof has invalid fabricator economics")
    if any(proof[key] is not True for key in ("cancel_unchanged", "opened_surface", "roundtrip")):
        raise RuntimeError("Earned surface proof omitted UI or roundtrip evidence")
    if mode == "paused":
        valid = (proof["building_id"] >= 0 and proof["steps"] == 0 and
                 proof["authorization"] == 0 and proof["treasury_before"] == proof["treasury_after"] and
                 math.isclose(proof["industry_progress"], proof["industry_cost"], abs_tol=1e-7) and
                 proof["complete"] is True and proof["powered"] is True and proof["staffed"] is True and
                 proof["enabled"] is True and proof["efficiency"] > 0 and
                 proof["industry_after"] > 0 and proof["industry_before"] == proof["industry_after"])
    else:
        valid = (proof["building_id"] >= 0 and proof["steps"] > 0 and proof["authorization"] == authorization and
                 proof["treasury_before"] >= proof["authorization"] and
                 math.isclose(proof["treasury_before"] - proof["treasury_after"], proof["authorization"], abs_tol=1e-7) and
                 math.isclose(proof["industry_progress"], proof["industry_cost"], abs_tol=1e-7) and
                 proof["complete"] is True and proof["powered"] is True and proof["staffed"] is True and
                 proof["enabled"] is True and proof["efficiency"] > 0 and proof["industry_after"] > proof["industry_before"])
    if not valid:
        raise RuntimeError(f"Earned surface {mode} proof is invalid")
    return proof


def _source_state(payload: dict) -> dict:
    galaxy = payload.get("Galaxy")
    if (payload.get("FormatVersion") != 17 or not isinstance(galaxy, dict) or
            galaxy.get("Seed") != 115501 or galaxy.get("PlayerCivilizationId") != 0):
        raise RuntimeError("Earned surface source is not Player17 seed 115501 player 0")
    colonies = galaxy.get("Colonies")
    economies = galaxy.get("Economies")
    if not isinstance(colonies, list) or not isinstance(economies, list):
        raise RuntimeError("Earned surface source has malformed colonies or economies")
    colony = [x for x in colonies if isinstance(x, dict) and x.get("Id") == 9 and x.get("CivilizationId") == 0]
    if len(colony) != 1 or not isinstance(colony[0].get("SurfaceBuildings"), list) or colony[0]["SurfaceBuildings"]:
        raise RuntimeError("Earned surface source is not the empty earned colony")
    if _number(colony[0].get("PopulationMillions"), "source population") < 250:
        raise RuntimeError("Earned surface source lacks the earned 250M colony")
    economy = [x for x in economies if isinstance(x, dict) and x.get("CivilizationId") == 0]
    if len(economy) != 1 or _number(economy[0].get("Credits"), "source treasury") < 50 or _number(economy[0].get("Industry"), "source industry") < 0:
        raise RuntimeError("Earned surface source cannot pay for the fabricator")
    arrays = {key: value for key, value in galaxy.items() if isinstance(value, list)}
    return {"identity": _colony_identity(colony[0]),
            "authorization": 50.0 * _construction_multiplier(payload, colony[0]),
            "days": _number(payload.get("SimulationDays"), "source days"),
            "treasury": _number(economy[0].get("Credits"), "source treasury"),
            "colony_ids": [(x.get("Id"), x.get("CivilizationId"), x.get("SystemId"), x.get("PlanetaryBodyId")) for x in colonies],
            "entity_counts": {key: len(value) for key, value in arrays.items()},
            "entity_identities": {key: [(row.get("Id"), row.get("CivilizationId"))
                                         for row in value if isinstance(row, dict)]
                                  for key, value in arrays.items()}}


def _saved_site(payload: dict, proof: dict, before: dict) -> None:
    galaxy = payload.get("Galaxy", {})
    colonies = galaxy.get("Colonies")
    if not isinstance(colonies, list):
        raise RuntimeError("Earned surface save has malformed colonies")
    colony = [x for x in colonies if isinstance(x, dict) and x.get("Id") == proof["colony_id"] and x.get("CivilizationId") == proof["player_id"]]
    if len(colony) != 1 or colony[0].get("SystemId") != proof["system_id"] or colony[0].get("PlanetaryBodyId") != proof["body_id"]:
        raise RuntimeError("Earned surface save changed its target colony")
    sites = colony[0].get("SurfaceBuildings")
    if not isinstance(sites, list):
        raise RuntimeError("Earned surface save has malformed surface buildings")
    if len(sites) != 1:
        raise RuntimeError("Earned surface save created an unexpected surface site")
    found = [x for x in sites if isinstance(x, dict) and x.get("Id") == proof["building_id"]]
    if len(found) != 1:
        raise RuntimeError("Earned surface save lacks the proved site")
    site = found[0]
    if (site.get("TypeId") != "fabricator" or site.get("IsComplete") is not True or
            site.get("IsEnabled", True) is not True or
            any(not math.isclose(_number(site.get(key), key), proof[field], abs_tol=1e-6)
                for key, field in (("X", "x"), ("Z", "z"), ("RotationDegrees", "rotation"), ("IndustryProgress", "industry_progress")))):
        raise RuntimeError("Earned surface proof does not bind the completed saved fabricator")
    if not math.isclose(_number(payload.get("SimulationDays"), "saved days"), proof["after_days"], rel_tol=0, abs_tol=1e-6):
        raise RuntimeError("Earned surface proof does not bind saved time")
    previous = _source_state(before)
    if tuple(proof[key] for key in _IDENTITY_FIELDS) != previous["identity"]:
        raise RuntimeError("Earned surface proof changed the source colony identity")
    if [(x.get("Id"), x.get("CivilizationId"), x.get("SystemId"), x.get("PlanetaryBodyId")) for x in colonies] != previous["colony_ids"]:
        raise RuntimeError("Earned surface changed colony identities")
    arrays = {key: value for key, value in galaxy.items() if isinstance(value, list)}
    if {key: len(value) for key, value in arrays.items()} != previous["entity_counts"]:
        raise RuntimeError("Earned surface created unrelated entities")
    identities = {key: [(row.get("Id"), row.get("CivilizationId"))
                        for row in value if isinstance(row, dict)]
                  for key, value in arrays.items()}
    if identities != previous["entity_identities"]:
        raise RuntimeError("Earned surface changed persistent entity identities")
    if (payload.get("FormatVersion") != 17 or galaxy.get("Seed") != 115501 or
            galaxy.get("PlayerCivilizationId") != 0):
        raise RuntimeError("Earned surface save changed Player17 campaign identity")


def _launch(package: Path, work: Path, env: dict[str, str], save: Path, capture: Path, flag: str, width: int, height: int):
    args = [str(package / "stellar-continuum-native.exe"), "--asset-root", str(package),
            "--save-path", str(save), "--load", "--seed", "115501", "--width", str(width),
            "--height", str(height), flag, str(capture)]
    result = subprocess.run(args, cwd=work, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="strict", timeout=330)
    if result.returncode:
        raise RuntimeError(f"Earned surface {flag} failed ({result.returncode}): {result.stdout[-2000:]}\n{result.stderr[-2000:]}")
    if any(token not in result.stdout for token in ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
        raise RuntimeError("Earned surface launch lacked renderer, campaign, or save proof")
    return result


def _bind_resume_source(proof: dict, source: dict) -> None:
    if tuple(proof[key] for key in _IDENTITY_FIELDS) != source["identity"]:
        raise RuntimeError("Earned surface proof changed the source colony identity")
    if proof["authorization"] != source["authorization"]:
        raise RuntimeError("Earned surface proof changed the source construction cost")
    if (not math.isclose(proof["before_days"], source["days"], rel_tol=0, abs_tol=1e-7) or
            not math.isclose(proof["treasury_before"], source["treasury"], rel_tol=0, abs_tol=1e-7)):
        raise RuntimeError("Earned surface resume proof does not bind source time or treasury")


def _bind_paused(proof: dict, resume: dict, payload: dict) -> None:
    """Bind the read-only reload proof to the exact completed saved site."""
    if any(proof[key] != resume[key] for key in
           ("player_id", "system_id", "body_id", "colony_id", "building_id", "type_id", "x", "z", "rotation",
            "industry_cost", "industry_progress", "complete", "powered", "staffed", "enabled", "efficiency",
            "industry_after")):
        raise RuntimeError("Earned surface paused proof changed persisted site identity or output")
    galaxy = payload.get("Galaxy", {})
    colonies = galaxy.get("Colonies") if isinstance(galaxy, dict) else None
    colony = next((row for row in colonies if isinstance(row, dict) and row.get("Id") == proof["colony_id"]), None) if isinstance(colonies, list) else None
    sites = colony.get("SurfaceBuildings") if isinstance(colony, dict) else None
    site = next((row for row in sites if isinstance(row, dict) and row.get("Id") == proof["building_id"]), None) if isinstance(sites, list) else None
    if site is None or site.get("TypeId") != "fabricator" or site.get("IsComplete") is not True:
        raise RuntimeError("Earned surface paused save lacks the persisted fabricator")
    economies = galaxy.get("Economies") if isinstance(galaxy, dict) else None
    economy = next((row for row in economies if isinstance(row, dict) and row.get("CivilizationId") == proof["player_id"]), None) if isinstance(economies, list) else None
    if (economy is None or not math.isclose(_number(payload.get("SimulationDays"), "paused saved days"), proof["before_days"], rel_tol=0, abs_tol=1e-7) or
            not math.isclose(proof["after_days"], proof["before_days"], rel_tol=0, abs_tol=1e-7) or
            not math.isclose(_number(economy.get("Credits"), "paused saved treasury"), proof["treasury_before"], rel_tol=0, abs_tol=1e-7)):
        raise RuntimeError("Earned surface paused proof does not bind completed save time or treasury")


def validate_native_earned_surface_export(package: Path, env: dict[str, str], source_save: Path):
    package, source = Path(package), Path(source_save)
    if not source.is_absolute() or not source.is_file():
        raise RuntimeError("Earned surface source save must be an existing absolute file")
    source_bytes = source.read_bytes()
    before = _read_json(source, "Earned surface source")
    source_state = _source_state(before)
    captures: list[str] = []; saves: list[str] = []; proofs: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="stellar-native-earned-surface-") as temporary:
        work = Path(temporary); save = work / "earned-surface.player17.json"; shutil.copy2(source, save)
        resume_capture = work / "earned-surface-1280x720.bmp"
        result = _launch(package, work, _environment(env), save, resume_capture, "--earned-surface-smoke", 1280, 720)
        resume = _proof(result.stdout, "resume", source_state["authorization"]); after = _read_json(save, "Earned surface completed save")
        _bind_resume_source(resume, source_state)
        _saved_site(after, resume, before); validate_bmp(resume_capture, 1280, 720, "earned surface", stdout=result.stdout)
        for capture in (resume_capture, resume_capture.with_name(resume_capture.stem + "-colony.bmp"), resume_capture.with_name(resume_capture.stem + "-review.bmp"), resume_capture.with_name(resume_capture.stem + "-construction.bmp")):
            validate_bmp(capture, 1280, 720, "earned surface", stdout=result.stdout)
            target = package.parent / f"{package.name}-{capture.name}"; shutil.copy2(capture, target); captures.append(str(target))
        target = package.parent / f"{package.name}-earned-surface-completed.player17.json"; shutil.copy2(save, target); saves.append(str(target)); proofs.append(resume)
        paused_capture = work / "earned-surface-paused-1920x1080.bmp"
        result = _launch(package, work, _environment(env), save, paused_capture, "--earned-surface-paused-smoke", 1920, 1080)
        paused = _proof(result.stdout, "paused"); paused_payload = _read_json(save, "Earned surface paused save")
        if not _same_paused(after, paused_payload):
            raise RuntimeError("Earned surface paused reload changed the full Player17 payload")
        _bind_paused(paused, resume, paused_payload)
        validate_bmp(paused_capture, 1920, 1080, "earned surface paused", stdout=result.stdout)
        target = package.parent / f"{package.name}-{paused_capture.name}"; shutil.copy2(paused_capture, target); captures.append(str(target))
        target = package.parent / f"{package.name}-earned-surface-paused.player17.json"; shutil.copy2(save, target); saves.append(str(target)); proofs.append(paused)
        if source.read_bytes() != source_bytes:
            raise RuntimeError("Earned surface source save was modified")
    return {"nativeEarnedSurface": True, "earnedSurfaceCaptures": captures,
            "earnedSurfaceSaveCaptures": saves, "earnedSurfaceProofs": proofs}
