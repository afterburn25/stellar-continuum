"""Durable native proof for the earned scout's first reconnaissance journey."""

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

from native_bmp import validate_bmp
from native_fresh_progression_runtime import _validate_payload as _validate_fresh_payload


_FIELDS = {
    "mode", "seed", "player_id", "fleet_id", "origin_id", "target_id",
    "before_days", "after_days", "input_orders", "steps", "step_days",
    "revision_before", "revision_after", "phase_before", "phase_after",
    "survey_before", "survey_after", "survey_progress_before",
    "survey_progress_after", "transit_progress_before",
    "transit_progress_after", "seen_phases", "selected",
    "selection_read_only", "preview_read_only", "lane_connected", "paused",
    "save_roundtrip",
}
_STEP = 1 / 64
_RECON_DAYS = 2.0
_RECON_PROGRESS = 0.35
_MAX_STEPS = 256 * 64
_RUNS = (
    ("--first-exploration-smoke", "depart", "depart", 1280, 720),
    ("--first-exploration-paused-smoke", "paused", "paused-warp", 1920, 1080),
    ("--first-exploration-resume-smoke", "resume", "resume", 1280, 720),
    ("--first-exploration-paused-smoke", "paused", "paused-recon", 1920, 1080),
)


def _unique_json(text: str):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result

    def reject_constant(value):
        raise ValueError(f"invalid JSON constant {value}")

    return json.loads(text, object_pairs_hook=pairs, parse_constant=reject_constant)


def _read_json(path: Path, label: str) -> dict:
    try:
        value = _unique_json(path.read_bytes().decode("utf-8-sig"))
    except (OSError, UnicodeDecodeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError(f"{label} is not strict JSON") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"{label} is not a JSON object")
    return value


def _number(value, name: str) -> float:
    if (isinstance(value, bool) or not isinstance(value, (int, float)) or
            not math.isfinite(value)):
        raise RuntimeError(f"Native exploration has invalid {name}")
    return float(value)


def _integer(value, name: str, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise RuntimeError(f"Native exploration has invalid {name}")
    return value


def _optional_integer(value, name: str) -> int | None:
    if value is None:
        return None
    return _integer(value, name)


def _proof(stdout: str, mode: str) -> dict:
    rows = [line for line in stdout.splitlines()
            if "first_exploration=" in line]
    if len(rows) != 1:
        raise RuntimeError("Native exploration did not report exactly one proof")
    match = re.fullmatch(r"first_exploration=(\{.*\})", rows[0])
    if not match:
        raise RuntimeError("Native exploration proof is malformed")
    try:
        proof = _unique_json(match.group(1))
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Native exploration proof is malformed") from error
    if (not isinstance(proof, dict) or set(proof) != _FIELDS or
            proof.get("mode") != mode):
        raise RuntimeError("Native exploration proof has the wrong schema")
    if type(proof.get("seed")) is not int or proof["seed"] != 115501:
        raise RuntimeError("Native exploration proof has the wrong seed")

    for key in (
            "player_id", "fleet_id", "origin_id", "target_id", "input_orders",
            "steps", "revision_before", "revision_after", "survey_before",
            "survey_after"):
        _integer(proof.get(key), key)
    for key in ("phase_before", "phase_after"):
        if _integer(proof.get(key), key) not in (0, 1, 2, 3):
            raise RuntimeError(f"Native exploration has invalid {key}")
    for key in ("before_days", "after_days", "step_days",
                "survey_progress_before", "survey_progress_after",
                "transit_progress_before", "transit_progress_after"):
        _number(proof.get(key), key)

    elapsed = proof["after_days"] - proof["before_days"]
    if (proof["origin_id"] == proof["target_id"] or
            proof["steps"] > _MAX_STEPS or proof["before_days"] < 0 or
            elapsed < 0 or elapsed > 256 or
            not math.isclose(proof["step_days"], _STEP,
                             rel_tol=0, abs_tol=1e-15) or
            not math.isclose(elapsed, proof["steps"] * _STEP,
                             rel_tol=0, abs_tol=1e-8)):
        raise RuntimeError("Native exploration proof has invalid journey bounds")

    phases = proof.get("seen_phases")
    if (not isinstance(phases, list) or not phases or
            any(type(value) is not int or value not in (0, 1, 2, 3)
                for value in phases) or
            len(phases) != len(set(phases)) or
            phases[0] != proof["phase_before"] or
            phases[-1] != proof["phase_after"]):
        raise RuntimeError("Native exploration proof has invalid phase evidence")
    for key in ("selected", "selection_read_only", "preview_read_only",
                "lane_connected", "paused", "save_roundtrip"):
        if type(proof.get(key)) is not bool:
            raise RuntimeError(f"Native exploration proof has invalid {key}")
    if any(proof[key] is not True for key in
           ("selected", "selection_read_only", "lane_connected", "paused",
            "save_roundtrip")):
        raise RuntimeError("Native exploration proof flags are invalid")

    if mode == "depart":
        valid = (
            proof["phase_before"] == 0 and proof["phase_after"] == 2 and
            {1, 2}.issubset(phases) and proof["input_orders"] == 1 and
            proof["revision_after"] == proof["revision_before"] + 1 and
            proof["steps"] > 0 and proof["preview_read_only"] is True and
            proof["survey_after"] == proof["survey_before"] and
            math.isclose(proof["survey_progress_after"],
                         proof["survey_progress_before"], abs_tol=1e-9) and
            0 < proof["transit_progress_after"] < 1)
    elif mode == "resume":
        valid = (
            proof["phase_before"] == 2 and proof["phase_after"] == 0 and
            {2, 3, 0}.issubset(phases) and proof["input_orders"] == 0 and
            proof["revision_after"] == proof["revision_before"] and
            proof["steps"] > 0 and proof["preview_read_only"] is False and
            proof["survey_before"] < 2 <= proof["survey_after"] and
            proof["survey_progress_after"] >= _RECON_PROGRESS and
            proof["transit_progress_before"] > 0 and
            proof["transit_progress_after"] == 0)
    else:
        valid = (
            proof["input_orders"] == 0 and proof["steps"] == 0 and
            elapsed == 0 and
            proof["revision_after"] == proof["revision_before"] and
            proof["phase_after"] == proof["phase_before"] and
            proof["survey_after"] == proof["survey_before"] and
            math.isclose(proof["survey_progress_after"],
                         proof["survey_progress_before"], abs_tol=1e-12) and
            math.isclose(proof["transit_progress_after"],
                         proof["transit_progress_before"], abs_tol=1e-12) and
            proof["preview_read_only"] is False)
    if not valid:
        raise RuntimeError(f"Native exploration {mode} proof is invalid")
    return proof


def _target_survey(galaxy: dict, proof: dict) -> tuple[int, float]:
    knowledge_rows = galaxy.get("Knowledge")
    if not isinstance(knowledge_rows, list):
        raise RuntimeError("Exploration save has malformed knowledge")
    knowledge = [row for row in knowledge_rows
                 if isinstance(row, dict) and
                 type(row.get("CivilizationId")) is int and
                 row["CivilizationId"] == proof["player_id"]]
    if len(knowledge) != 1:
        raise RuntimeError("Exploration save lacks unique player knowledge")
    surveys = knowledge[0].get("SystemSurveys")
    if not isinstance(surveys, list):
        raise RuntimeError("Exploration save has malformed surveys")
    target = [row for row in surveys
              if isinstance(row, dict) and
              type(row.get("SystemId")) is int and
              row["SystemId"] == proof["target_id"]]
    if len(target) > 1:
        raise RuntimeError("Exploration save has duplicate target surveys")
    level = target[0].get("Level") if target else 0
    progress = target[0].get("Progress") if target else 0.0
    if type(level) is not int or level not in (0, 1, 2, 3):
        raise RuntimeError("Exploration save has malformed survey level")
    return level, _number(progress, "survey progress")


def _state(payload: dict, proof: dict) -> dict:
    galaxy = payload.get("Galaxy")
    if (not isinstance(galaxy, dict) or payload.get("FormatVersion") != 17 or
            not isinstance(payload.get("SavedAtUtc"), str) or
            not payload["SavedAtUtc"] or type(galaxy.get("Seed")) is not int or
            galaxy["Seed"] != 115501 or
            type(galaxy.get("PlayerCivilizationId")) is not int or
            galaxy["PlayerCivilizationId"] != proof["player_id"]):
        raise RuntimeError("Exploration save has invalid Player17 identity")
    systems = galaxy.get("Systems")
    if not isinstance(systems, list) or len(systems) != 500:
        raise RuntimeError("Exploration save has invalid systems")
    system_ids = [row.get("Id") for row in systems if isinstance(row, dict)]
    if (len(system_ids) != 500 or
            any(type(value) is not int for value in system_ids) or
            set(system_ids) != set(range(500)) or
            proof["origin_id"] not in system_ids or
            proof["target_id"] not in system_ids):
        raise RuntimeError("Exploration save has invalid system identities")

    civilizations = galaxy.get("Civilizations")
    if not isinstance(civilizations, list):
        raise RuntimeError("Exploration save has malformed civilizations")
    players = [row for row in civilizations
               if isinstance(row, dict) and row.get("IsPlayer") is True]
    if (len(players) != 1 or type(players[0].get("Id")) is not int or
            players[0]["Id"] != proof["player_id"] or
            type(players[0].get("HomeSystemId")) is not int or
            players[0]["HomeSystemId"] != proof["origin_id"]):
        raise RuntimeError("Exploration proof does not bind the player home")

    all_fleets = galaxy.get("Fleets")
    if not isinstance(all_fleets, list) or any(not isinstance(row, dict)
                                               for row in all_fleets):
        raise RuntimeError("Exploration save has malformed fleets")
    fleet_ids = [row.get("Id") for row in all_fleets]
    if (any(type(value) is not int or value < 0 for value in fleet_ids) or
            len(fleet_ids) != len(set(fleet_ids))):
        raise RuntimeError("Exploration save has invalid fleet identities")
    owned = [row for row in all_fleets
             if type(row.get("CivilizationId")) is int and
             row["CivilizationId"] == proof["player_id"]]
    if len(owned) != 2 or any(row.get("IsActive") is not True for row in owned):
        raise RuntimeError("Exploration save lacks exactly two active owned ships")
    scout = [row for row in owned
             if row.get("Id") == proof["fleet_id"] and
             row.get("DesignId") == "warp_scout" and row.get("Role") == 0]
    science = [row for row in owned
               if row.get("DesignId") == "science_vessel" and
               row.get("Role") == 1]
    if len(scout) != 1 or len(science) != 1 or science[0]["Id"] == proof["fleet_id"]:
        raise RuntimeError("Exploration save lacks the earned first ships")

    scout = scout[0]
    for key in ("MissionOrderRevision", "TransitPhase"):
        _integer(scout.get(key), key)
    if scout["TransitPhase"] not in (0, 1, 2, 3):
        raise RuntimeError("Exploration save has malformed transit phase")
    for key in ("CurrentSystemId", "DestinationSystemId",
                "TransitOriginSystemId", "TransitTargetSystemId",
                "ReconnaissanceSystemId"):
        _optional_integer(scout.get(key), key)
    route = scout.get("PlannedRouteSystemIds")
    if (not isinstance(route, list) or
            any(type(value) is not int or value < 0 for value in route)):
        raise RuntimeError("Exploration save has malformed route identities")
    level, survey_progress = _target_survey(galaxy, proof)
    return {
        "scout": scout,
        "science": science[0],
        "level": level,
        "survey_progress": survey_progress,
        "transit_progress": _number(scout.get("TransitProgress"),
                                    "transit progress"),
        "days": _number(payload.get("SimulationDays"), "simulation time"),
        "fuel": _number(scout.get("FuelRemainingLightYears"),
                        "remaining fuel"),
        "capacity": _number(scout.get("FuelCapacityLightYears"),
                            "fuel capacity"),
    }


def _serialized_direct_lane(payload: dict, proof: dict) -> None:
    """Check an explicit lane table if a future Player schema serializes one.

    Player17 currently reconstructs lanes, so today's evidence is Core's
    authoritative direct route plus the native lane_connected proof bit.
    """
    galaxy = payload["Galaxy"]
    lanes = galaxy.get("Lanes", galaxy.get("InterstellarLanes"))
    if lanes is None:
        return
    if not isinstance(lanes, list):
        raise RuntimeError("Exploration save has malformed serialized lanes")
    endpoints = []
    for row in lanes:
        if not isinstance(row, dict):
            raise RuntimeError("Exploration save has malformed serialized lanes")
        first = row.get("FirstSystemId")
        second = row.get("SecondSystemId")
        if type(first) is not int or type(second) is not int:
            raise RuntimeError("Exploration save has malformed serialized lane identity")
        endpoints.append({first, second})
    if {proof["origin_id"], proof["target_id"]} not in endpoints:
        raise RuntimeError("Exploration target is absent from serialized direct lanes")


def _bind(proof: dict, before: dict, after: dict) -> None:
    before_state = _state(before, proof)
    after_state = _state(after, proof)
    before_scout = before_state["scout"]
    after_scout = after_state["scout"]
    if not (
            math.isclose(proof["before_days"], before_state["days"],
                         abs_tol=1e-6) and
            math.isclose(proof["after_days"], after_state["days"],
                         abs_tol=1e-6) and
            proof["revision_before"] == before_scout["MissionOrderRevision"] and
            proof["revision_after"] == after_scout["MissionOrderRevision"] and
            proof["phase_before"] == before_scout["TransitPhase"] and
            proof["phase_after"] == after_scout["TransitPhase"] and
            proof["survey_before"] == before_state["level"] and
            proof["survey_after"] == after_state["level"] and
            math.isclose(proof["survey_progress_before"],
                         before_state["survey_progress"], abs_tol=1e-8) and
            math.isclose(proof["survey_progress_after"],
                         after_state["survey_progress"], abs_tol=1e-8) and
            math.isclose(proof["transit_progress_before"],
                         before_state["transit_progress"], abs_tol=1e-8) and
            math.isclose(proof["transit_progress_after"],
                         after_state["transit_progress"], abs_tol=1e-8) and
            before_state["science"] == after_state["science"] and
            before_state["capacity"] == after_state["capacity"]):
        raise RuntimeError("Exploration proof does not match before and after saves")

    _serialized_direct_lane(before, proof)
    _serialized_direct_lane(after, proof)
    if proof["mode"] == "depart":
        valid = (
            before_scout.get("CurrentSystemId") == proof["origin_id"] and
            before_scout.get("DestinationSystemId") is None and
            before_scout.get("PlannedRouteSystemIds") == [] and
            before_state["fuel"] == before_state["capacity"] and
            after_scout.get("CurrentSystemId") is None and
            after_scout.get("DestinationSystemId") == proof["target_id"] and
            after_scout.get("TransitOriginSystemId") == proof["origin_id"] and
            after_scout.get("TransitTargetSystemId") == proof["target_id"] and
            after_scout.get("PlannedRouteSystemIds") == [proof["target_id"]] and
            0 <= after_state["fuel"] < before_state["fuel"])
        if not valid:
            raise RuntimeError(
                "Exploration departure lacks canonical direct-route/fuel evidence")
    elif proof["mode"] == "resume":
        valid_before = (
            before_scout.get("CurrentSystemId") is None and
            before_scout.get("DestinationSystemId") == proof["target_id"] and
            before_scout.get("TransitOriginSystemId") == proof["origin_id"] and
            before_scout.get("TransitTargetSystemId") == proof["target_id"] and
            before_scout.get("PlannedRouteSystemIds") == [proof["target_id"]])
        valid_after = (
            after_scout.get("CurrentSystemId") == proof["target_id"] and
            after_scout.get("DestinationSystemId") is None and
            after_scout.get("TransitOriginSystemId") is None and
            after_scout.get("TransitTargetSystemId") is None and
            after_scout.get("PlannedRouteSystemIds") == [] and
            after_scout.get("ReconnaissanceSystemId") == proof["target_id"] and
            _number(after_scout.get("ReconnaissanceDaysCompleted"),
                    "reconnaissance days") + 1e-9 >= _RECON_DAYS and
            after_state["fuel"] <= before_state["fuel"])
        if not valid_before or not valid_after:
            raise RuntimeError(
                "Exploration arrival lacks canonical route, fuel or reconnaissance evidence")


def _same_paused(left: dict, right: dict) -> bool:
    left = copy.deepcopy(left)
    right = copy.deepcopy(right)
    left.pop("SavedAtUtc", None)
    right.pop("SavedAtUtc", None)
    return left == right


def _environment(env: dict[str, str]) -> dict[str, str]:
    root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    return dict(env, PATH=str(root / "System32") + os.pathsep + str(root))


def _launch(folder: Path, work: Path, env: dict[str, str], save: Path,
            capture: Path, flag: str, width: int, height: int):
    args = [str(folder / "stellar-continuum-native.exe"),
            "--asset-root", str(folder), "--save-path", str(save), "--load",
            "--seed", "115501", "--width", str(width), "--height", str(height),
            flag, str(capture)]
    result = subprocess.run(args, cwd=work, env=env, capture_output=True,
                            text=True, encoding="utf-8", errors="strict",
                            timeout=300)
    if result.returncode:
        details = "\n".join(value for value in (result.stdout, result.stderr)
                            if value)
        raise RuntimeError(
            f"Native exploration {flag} failed ({result.returncode}): {details}")
    required = ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")
    if any(token not in result.stdout for token in required):
        raise RuntimeError(
            "Native exploration did not confirm renderer, campaign and save")
    return result


def _copy_artifact(source: Path, destination: Path, output: list[str]) -> None:
    shutil.copy2(source, destination)
    output.append(str(destination))


def validate_native_first_exploration_export(
        folder: Path, env: dict[str, str], earned_save: Path):
    source = Path(earned_save)
    if not source.is_file():
        raise RuntimeError("Earned first-ships save is missing")
    source_bytes = source.read_bytes()
    initial = _read_json(source, "Earned first-ships save")
    galaxy = initial.get("Galaxy")
    fleets = galaxy.get("Fleets") if isinstance(galaxy, dict) else None
    player_id = galaxy.get("PlayerCivilizationId") if isinstance(galaxy, dict) else None
    if type(player_id) is not int or not isinstance(fleets, list):
        raise RuntimeError("Earned first-ships save has invalid player fleets")
    owned = [fleet for fleet in fleets
             if isinstance(fleet, dict) and
             type(fleet.get("CivilizationId")) is int and
             fleet["CivilizationId"] == player_id and
             fleet.get("IsActive") is True]
    scouts = [fleet for fleet in owned
              if fleet.get("DesignId") == "warp_scout" and fleet.get("Role") == 0]
    sciences = [fleet for fleet in owned
                if fleet.get("DesignId") == "science_vessel" and
                fleet.get("Role") == 1]
    if (len(owned) != 2 or len(scouts) != 1 or len(sciences) != 1 or
            type(scouts[0].get("Id")) is not int or
            type(sciences[0].get("Id")) is not int):
        raise RuntimeError("Earned first-ships save lacks its two owned designs")
    fresh_proof = {
        "player_id": player_id,
        "scout_id": scouts[0]["Id"],
        "science_id": sciences[0]["Id"],
        "after_days": initial.get("SimulationDays"),
    }
    try:
        _validate_fresh_payload(initial, fresh_proof, "fresh")
    except (TypeError, KeyError, AttributeError, ValueError, RuntimeError) as error:
        raise RuntimeError("Earned first-ships save is invalid") from error

    captures: list[str] = []
    saves: list[str] = []
    diagnostics: list[str] = []
    payloads: list[dict] = []
    proofs: list[dict] = []
    with tempfile.TemporaryDirectory(
            prefix="stellar-native-first-exploration-") as temporary:
        work = Path(temporary)
        save = work / "first-exploration.player17.json"
        shutil.copy2(source, save)
        before = initial
        for index, (flag, mode, label, width, height) in enumerate(_RUNS):
            capture = work / f"first-exploration-{index}.bmp"
            result = _launch(folder, work, _environment(env), save, capture,
                             flag, width, height)
            proof = _proof(result.stdout, mode)
            after = _read_json(save, "Native exploration Player17 save")
            _bind(proof, before, after)
            validate_bmp(capture, width, height, "first exploration",
                         stdout=result.stdout)
            _copy_artifact(
                capture,
                folder.parent / f"{folder.name}-first-exploration-{label}.bmp",
                captures)
            if mode == "depart":
                sidecar = capture.with_name(capture.stem + "-departure.bmp")
                validate_bmp(sidecar, width, height,
                             "first exploration departure", stdout=result.stdout)
                _copy_artifact(
                    sidecar,
                    folder.parent /
                    f"{folder.name}-first-exploration-departure.bmp",
                    captures)
            if mode == "resume":
                sidecar = capture.with_name(capture.stem + "-arrival.bmp")
                validate_bmp(sidecar, width, height,
                             "first exploration arrival", stdout=result.stdout)
                _copy_artifact(
                    sidecar,
                    folder.parent / f"{folder.name}-first-exploration-arrival.bmp",
                    captures)
            _copy_artifact(
                save,
                folder.parent /
                f"{folder.name}-first-exploration-{label}.player17.json",
                saves)
            diagnostics.append(result.stdout.strip())
            payloads.append(after)
            proofs.append(proof)
            before = after

        if (not _same_paused(payloads[0], payloads[1]) or
                not _same_paused(payloads[2], payloads[3])):
            raise RuntimeError("Exploration paused reload changed Player17 payload")
        identity = (proofs[0]["player_id"], proofs[0]["fleet_id"],
                    proofs[0]["origin_id"], proofs[0]["target_id"])
        if any((proof["player_id"], proof["fleet_id"], proof["origin_id"],
                proof["target_id"]) != identity for proof in proofs[1:]):
            raise RuntimeError("Exploration proofs changed route identity")
        if any(proofs[index]["after_days"] !=
               proofs[index + 1]["before_days"] for index in range(3)):
            raise RuntimeError(
                "Exploration proofs do not form one continuous journey")
        if (proofs[0]["revision_after"] != proofs[1]["revision_before"] or
                proofs[1]["revision_after"] != proofs[2]["revision_before"] or
                proofs[2]["revision_after"] != proofs[3]["revision_before"]):
            raise RuntimeError("Exploration proofs changed persisted order identity")
        if source.read_bytes() != source_bytes:
            raise RuntimeError("Earned first-ships source was modified")
    return {
        "nativeFirstExploration": True,
        "nativeFirstExplorationReload": True,
        "firstExplorationCaptures": captures,
        "firstExplorationSaveCaptures": saves,
        "firstExplorationDiagnostics": diagnostics,
    }
