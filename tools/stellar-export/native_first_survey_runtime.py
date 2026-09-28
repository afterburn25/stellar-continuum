"""Native proof that the earned science vessel completes its first full survey."""
from __future__ import annotations

import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_bmp import validate_bmp
from native_first_exploration_runtime import (_environment, _integer, _number,
                                              _read_json, _same_paused,
                                              _unique_json)
from native_fresh_progression_runtime import _validate_payload as _validate_fresh_payload


_FIELDS = {"mode", "seed", "player_id", "fleet_id", "origin_id", "target_id",
           "before_days", "after_days", "input_orders", "steps", "step_days",
           "revision_before", "revision_after", "phase_before", "phase_after",
           "survey_before", "survey_after", "survey_progress_before",
           "survey_progress_after", "transit_progress_before",
           "transit_progress_after", "seen_phases", "selected",
           "selection_read_only", "preview_read_only", "lane_connected", "paused",
           "save_roundtrip", "scout_id", "body_id", "inspection_read_only",
           "facts_visible"}
_STEP = 1 / 64
_MAX_STEPS = 256 * 64
_RUNS = (("--first-survey-smoke", "depart", "depart", 1280, 720),
         ("--first-survey-paused-smoke", "paused", "paused-partial", 1920, 1080),
         ("--first-survey-resume-smoke", "resume", "resume", 1280, 720),
         ("--first-survey-paused-smoke", "paused", "paused-full", 1920, 1080))


def _proof(stdout: str, mode: str) -> dict:
    rows = [line for line in stdout.splitlines() if "first_survey=" in line]
    if len(rows) != 1:
        raise RuntimeError("Native first survey did not report exactly one proof")
    match = re.fullmatch(r"first_survey=(\{.*\})", rows[0])
    if not match:
        raise RuntimeError("Native first survey proof is malformed")
    try:
        proof = _unique_json(match.group(1))
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Native first survey proof is malformed") from error
    if not isinstance(proof, dict) or set(proof) != _FIELDS or proof.get("mode") != mode:
        raise RuntimeError("Native first survey proof has the wrong schema")
    if type(proof.get("seed")) is not int or proof["seed"] != 115501:
        raise RuntimeError("Native first survey proof has the wrong seed")
    for key in ("player_id", "fleet_id", "scout_id", "origin_id", "target_id",
                "body_id", "input_orders", "steps", "revision_before",
                "revision_after", "survey_before", "survey_after"):
        _integer(proof.get(key), key)
    if proof["fleet_id"] == proof["scout_id"] or proof["origin_id"] == proof["target_id"]:
        raise RuntimeError("Native first survey proof has invalid identities")
    for key in ("phase_before", "phase_after"):
        if _integer(proof.get(key), key) not in (0, 1, 2, 3):
            raise RuntimeError("Native first survey proof has invalid phases")
    for key in ("before_days", "after_days", "step_days", "survey_progress_before",
                "survey_progress_after", "transit_progress_before", "transit_progress_after"):
        _number(proof.get(key), key)
    elapsed = proof["after_days"] - proof["before_days"]
    if (proof["before_days"] < 0 or elapsed < 0 or elapsed > 256 or
            proof["steps"] > _MAX_STEPS or
            not math.isclose(proof["step_days"], _STEP, rel_tol=0, abs_tol=1e-15) or
            not math.isclose(elapsed, proof["steps"] * _STEP, rel_tol=0, abs_tol=1e-8)):
        raise RuntimeError("Native first survey proof has invalid clock evidence")
    phases = proof.get("seen_phases")
    if (not isinstance(phases, list) or not phases or
            any(type(value) is not int or value not in (0, 1, 2, 3) for value in phases)):
        raise RuntimeError("Native first survey proof has invalid phase evidence")
    if len(phases) != len(set(phases)):
        raise RuntimeError("Native first survey proof has invalid phase evidence")
    for key in ("selected", "selection_read_only", "preview_read_only", "lane_connected",
                "paused", "save_roundtrip", "inspection_read_only", "facts_visible"):
        if type(proof.get(key)) is not bool:
            raise RuntimeError(f"Native first survey proof has invalid {key}")
    if any(proof[key] is not True for key in ("selected", "selection_read_only",
                                               "lane_connected", "paused", "save_roundtrip",
                                               "inspection_read_only")):
        raise RuntimeError("Native first survey proof flags are invalid")
    if mode == "depart":
        valid = (proof["phase_before"] == proof["phase_after"] == 0 and
                 phases == [0, 1, 2, 3] and proof["input_orders"] == 1 and
                 proof["revision_after"] == proof["revision_before"] + 1 and
                 proof["steps"] > 0 and proof["survey_before"] == 2 == proof["survey_after"] and
                 .35 < proof["survey_progress_after"] < 1 and
                 proof["preview_read_only"] is True and proof["facts_visible"] is False)
    elif mode == "resume":
        valid = (proof["phase_before"] == proof["phase_after"] == 0 and phases == [0] and
                 proof["input_orders"] == 0 and proof["revision_after"] == proof["revision_before"] and
                 proof["steps"] > 0 and proof["survey_before"] == 2 and proof["survey_after"] == 3 and
                 proof["survey_progress_after"] == 1 and proof["preview_read_only"] is False and
                 proof["facts_visible"] is True)
    else:
        valid = (proof["input_orders"] == 0 and proof["steps"] == 0 and elapsed == 0 and phases == [0] and
                 proof["revision_after"] == proof["revision_before"] and
                 proof["phase_after"] == proof["phase_before"] and
                 proof["survey_after"] == proof["survey_before"] and
                 math.isclose(proof["survey_progress_after"], proof["survey_progress_before"], abs_tol=1e-12) and
                 math.isclose(proof["transit_progress_after"], proof["transit_progress_before"], abs_tol=1e-12) and
                 proof["preview_read_only"] is False and
                 proof["facts_visible"] is (proof["survey_after"] == 3))
    if not valid:
        raise RuntimeError(f"Native first survey {mode} proof is invalid")
    return proof


def _survey(galaxy: dict, proof: dict) -> tuple[int, float]:
    knowledge = galaxy.get("Knowledge")
    if (not isinstance(knowledge, list) or any(not isinstance(row, dict) for row in knowledge) or
            any(type(row.get("CivilizationId")) is not int for row in knowledge) or
            len({row["CivilizationId"] for row in knowledge}) != len(knowledge)):
        raise RuntimeError("First survey save has malformed knowledge identities")
    rows = [row for row in knowledge if row["CivilizationId"] == proof["player_id"]]
    if len(rows) != 1 or not isinstance(rows[0].get("SystemSurveys"), list):
        raise RuntimeError("First survey save has malformed knowledge")
    surveys = rows[0]["SystemSurveys"]
    if (any(not isinstance(row, dict) or type(row.get("SystemId")) is not int
            for row in surveys) or
            len({row["SystemId"] for row in surveys}) != len(surveys)):
        raise RuntimeError("First survey save has invalid survey identities")
    target = [row for row in surveys if row["SystemId"] == proof["target_id"]]
    if len(target) != 1 or type(target[0].get("Level")) is not int:
        raise RuntimeError("First survey save lacks target survey")
    return target[0]["Level"], _number(target[0].get("Progress"), "survey progress")


def _state(payload: dict, proof: dict) -> dict:
    galaxy = payload.get("Galaxy")
    if (not isinstance(galaxy, dict) or payload.get("FormatVersion") != 17 or
            not isinstance(payload.get("SavedAtUtc"), str) or not payload["SavedAtUtc"] or
            galaxy.get("Seed") != 115501 or galaxy.get("PlayerCivilizationId") != proof["player_id"]):
        raise RuntimeError("First survey save has invalid Player17 identity")
    systems = galaxy.get("Systems")
    if (not isinstance(systems, list) or len(systems) != 500 or
            any(not isinstance(row, dict) or type(row.get("Id")) is not int for row in systems) or
            len({row["Id"] for row in systems}) != len(systems) or
            {row["Id"] for row in systems} != set(range(500))):
        raise RuntimeError("First survey save has invalid systems")
    players = [row for row in galaxy.get("Civilizations", []) if isinstance(row, dict) and row.get("IsPlayer") is True]
    if len(players) != 1 or players[0].get("Id") != proof["player_id"] or players[0].get("HomeSystemId") != proof["origin_id"]:
        raise RuntimeError("First survey proof does not bind player home")
    fleets = galaxy.get("Fleets")
    if not isinstance(fleets, list) or any(not isinstance(row, dict) for row in fleets):
        raise RuntimeError("First survey save has malformed fleets")
    if (any(type(row.get("Id")) is not int or type(row.get("CivilizationId")) is not int for row in fleets) or
            len({row["Id"] for row in fleets}) != len(fleets)):
        raise RuntimeError("First survey save has invalid fleet identities")
    owned = [row for row in fleets if row["CivilizationId"] == proof["player_id"] and row.get("IsActive") is True]
    science = [row for row in owned if row["Id"] == proof["fleet_id"] and row.get("DesignId") == "science_vessel" and type(row.get("Role")) is int and row["Role"] == 1]
    scout = [row for row in owned if row["Id"] == proof["scout_id"] and row.get("DesignId") == "warp_scout" and type(row.get("Role")) is int and row["Role"] == 0]
    if len(owned) != 2 or len(science) != 1 or len(scout) != 1:
        raise RuntimeError("First survey save lacks exact earned ships")
    bodies = galaxy.get("PlanetaryBodies")
    if (not isinstance(bodies, list) or any(not isinstance(row, dict) or type(row.get("Id")) is not int for row in bodies) or
            len({row["Id"] for row in bodies}) != len(bodies)):
        raise RuntimeError("First survey save has invalid body identities")
    body = [row for row in bodies if row["Id"] == proof["body_id"]]
    if (len(body) != 1 or type(body[0].get("SystemId")) is not int or
            body[0]["SystemId"] != proof["target_id"] or body[0].get("ParentBodyId") is not None):
        raise RuntimeError("First survey proof does not bind a target non-moon body")
    science = science[0]
    for key in ("MissionOrderRevision", "TransitPhase"):
        _integer(science.get(key), key)
    if science["TransitPhase"] not in (0, 1, 2, 3):
        raise RuntimeError("First survey save has malformed science phase")
    route = science.get("PlannedRouteSystemIds")
    if not isinstance(route, list) or any(type(value) is not int for value in route):
        raise RuntimeError("First survey save has malformed science route")
    level, progress = _survey(galaxy, proof)
    return {"science": science, "scout": scout[0], "level": level, "progress": progress,
            "transit": _number(science.get("TransitProgress"), "transit progress"),
            "days": _number(payload.get("SimulationDays"), "simulation time"),
            "fuel": _number(science.get("FuelRemainingLightYears"), "remaining fuel"),
            "capacity": _number(science.get("FuelCapacityLightYears"), "fuel capacity")}


def _bind(proof: dict, before: dict, after: dict) -> None:
    left, right = _state(before, proof), _state(after, proof)
    b, a = left["science"], right["science"]
    if not (math.isclose(proof["before_days"], left["days"], abs_tol=1e-6) and
            math.isclose(proof["after_days"], right["days"], abs_tol=1e-6) and
            proof["revision_before"] == b["MissionOrderRevision"] and proof["revision_after"] == a["MissionOrderRevision"] and
            proof["phase_before"] == b["TransitPhase"] and proof["phase_after"] == a["TransitPhase"] and
            proof["survey_before"] == left["level"] and proof["survey_after"] == right["level"] and
            math.isclose(proof["survey_progress_before"], left["progress"], abs_tol=1e-8) and
            math.isclose(proof["survey_progress_after"], right["progress"], abs_tol=1e-8) and
            math.isclose(proof["transit_progress_before"], left["transit"], abs_tol=1e-8) and
            math.isclose(proof["transit_progress_after"], right["transit"], abs_tol=1e-8) and
            left["scout"] == right["scout"] and left["capacity"] == right["capacity"]):
        raise RuntimeError("First survey proof does not match before and after saves")
    for vessel in (b, a):
        if vessel.get("HoldRequested") is not False or vessel.get("ReturnToBaseRequested") is not False:
            raise RuntimeError("First survey science vessel was held or returning")
    if proof["mode"] == "depart" and not (b.get("CurrentSystemId") == proof["origin_id"] and
                                              b.get("DestinationSystemId") is None and
                                              b.get("TransitPhase") == 0 and
                                              b.get("PlannedRouteSystemIds") == [] and
                                              left["fuel"] == left["capacity"]):
        raise RuntimeError("First survey did not start from the idle science vessel at home")
    if not (a.get("CurrentSystemId") == proof["target_id"] and
            a.get("DestinationSystemId") is None and a.get("TransitPhase") == 0 and
            a.get("TransitOriginSystemId") is None and a.get("TransitTargetSystemId") is None and
            math.isclose(right["transit"], 0., abs_tol=1e-12) and
            a.get("PlannedRouteSystemIds") == [] and 0 <= right["fuel"] < right["capacity"]):
        raise RuntimeError("First survey did not persist local science work at the target")
    if proof["mode"] == "depart" and not right["fuel"] < left["fuel"]:
        raise RuntimeError("First survey departure did not spend canonical travel fuel")
    if proof["mode"] == "resume" and not (
            b.get("CurrentSystemId") == proof["target_id"] and
            b.get("DestinationSystemId") is None and b.get("TransitPhase") == 0 and
            b.get("TransitOriginSystemId") is None and b.get("TransitTargetSystemId") is None and
            math.isclose(left["transit"], 0., abs_tol=1e-12) and
            b.get("PlannedRouteSystemIds") == [] and
            math.isclose(right["fuel"], left["fuel"], abs_tol=1e-12)):
        raise RuntimeError("First survey resume did not preserve local science state")


def _launch(folder: Path, work: Path, env: dict[str, str], save: Path, capture: Path,
            flag: str, width: int, height: int) -> subprocess.CompletedProcess:
    args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
            "--save-path", str(save), "--load", "--seed", "115501", "--width", str(width),
            "--height", str(height), flag, str(capture)]
    try:
        result = subprocess.run(args, cwd=work, env=env, capture_output=True, text=True,
                                encoding="utf-8", errors="strict", timeout=300)
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout.decode("utf-8", "replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
        stderr = error.stderr.decode("utf-8", "replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
        raise RuntimeError(f"Native first survey {flag} timed out after 300 seconds:\n{stdout[-2000:]}\n{stderr[-2000:]}") from error
    if result.returncode:
        raise RuntimeError(f"Native first survey {flag} failed ({result.returncode}):\n{result.stdout[-2000:]}\n{result.stderr[-2000:]}")
    if any(value not in result.stdout for value in ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
        raise RuntimeError("Native first survey did not confirm renderer, campaign and save")
    return result


def validate_native_first_survey_export(folder: Path, env: dict[str, str], recon_save: Path):
    source = Path(recon_save)
    if not source.is_file():
        raise RuntimeError("Completed reconnaissance save is missing")
    source_bytes = source.read_bytes()
    initial = _read_json(source, "Completed reconnaissance save")
    galaxy = initial.get("Galaxy") if isinstance(initial, dict) else None
    fleets = galaxy.get("Fleets") if isinstance(galaxy, dict) else None
    player = galaxy.get("PlayerCivilizationId") if isinstance(galaxy, dict) else None
    civilizations = galaxy.get("Civilizations") if isinstance(galaxy, dict) else None
    if not isinstance(fleets, list) or type(player) is not int or not isinstance(civilizations, list):
        raise RuntimeError("Completed reconnaissance save has invalid player fleets")
    owned = [row for row in fleets if isinstance(row, dict) and row.get("CivilizationId") == player and row.get("IsActive") is True]
    scout = next((row for row in owned if row.get("DesignId") == "warp_scout" and row.get("Role") == 0), None)
    science = next((row for row in owned if row.get("DesignId") == "science_vessel" and row.get("Role") == 1), None)
    if len(owned) != 2 or not isinstance(scout, dict) or not isinstance(science, dict):
        raise RuntimeError("Completed reconnaissance save lacks exact earned ships")
    seed = {"player_id": player, "scout_id": scout.get("Id"), "science_id": science.get("Id"),
            "after_days": initial.get("SimulationDays")}
    try:
        _validate_fresh_payload(initial, seed, "fresh")
    except (TypeError, KeyError, AttributeError, ValueError) as error:
        raise RuntimeError("Completed reconnaissance save fails fresh progression prerequisites") from error
    home = next((row.get("HomeSystemId") for row in civilizations
                 if isinstance(row, dict) and row.get("Id") == player), None)
    target = scout.get("CurrentSystemId")
    if (type(target) is not int or scout.get("TransitPhase") != 0 or scout.get("DestinationSystemId") is not None or
            scout.get("ReconnaissanceSystemId") != target or _number(scout.get("ReconnaissanceDaysCompleted"), "recon days") < 2 or
            science.get("CurrentSystemId") != home or
            science.get("TransitPhase") != 0 or science.get("DestinationSystemId") is not None or science.get("PlannedRouteSystemIds") != []):
        raise RuntimeError("Completed reconnaissance save lacks the required scout/science starting state")
    source_survey = _survey(galaxy, {"player_id": player, "target_id": target})
    if source_survey[0] != 2 or not math.isclose(source_survey[1], .35, abs_tol=1e-9):
        raise RuntimeError("Completed reconnaissance save lacks canonical partial reconnaissance knowledge")
    captures, saves, diagnostics, payloads, proofs = [], [], [], [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-first-survey-") as temporary:
        work = Path(temporary)
        save = work / "first-survey.player17.json"
        shutil.copy2(source, save)
        before = initial
        identity = (player, science.get("Id"), scout.get("Id"), home, target)
        inspected_body = None
        for flag, mode, label, width, height in _RUNS:
            capture = work / f"first-survey-{label}.bmp"
            result = _launch(folder, work, _environment(env), save, capture,
                             flag, width, height)
            proof = _proof(result.stdout, mode)
            after = _read_json(save, "Native first survey save")
            _bind(proof, before, after)
            route_identity = (proof["player_id"], proof["fleet_id"], proof["scout_id"],
                              proof["origin_id"], proof["target_id"])
            if route_identity != identity:
                raise RuntimeError("First survey proof does not bind the actual source route")
            if inspected_body is None:
                inspected_body = proof["body_id"]
            elif proof["body_id"] != inspected_body:
                raise RuntimeError("First survey selected a different planet after reload")
            validate_bmp(capture, width, height, "first survey", stdout=result.stdout)
            artifacts = [capture]
            if mode == "depart":
                artifacts.append(capture.with_name(capture.stem + "-departure.bmp"))
            if mode == "resume":
                artifacts.append(capture.with_name(capture.stem + "-inspection.bmp"))
            for artifact in artifacts:
                validate_bmp(artifact, width, height, "first survey sidecar", stdout=result.stdout)
                destination = folder.parent / f"{folder.name}-{artifact.name}"
                shutil.copy2(artifact, destination)
                captures.append(str(destination))
            destination = folder.parent / f"{folder.name}-first-survey-{label}.player17.json"
            shutil.copy2(save, destination)
            saves.append(str(destination))
            diagnostics.append(result.stdout.strip())
            payloads.append(after)
            proofs.append(proof)
            before = after
        if not _same_paused(payloads[0], payloads[1]) or not _same_paused(payloads[2], payloads[3]):
            raise RuntimeError("First survey paused reload changed Player17 payload")
        if any(proofs[index]["after_days"] != proofs[index + 1]["before_days"] for index in range(3)):
            raise RuntimeError("First survey proofs do not form a continuous journey")
        if source.read_bytes() != source_bytes:
            raise RuntimeError("Completed reconnaissance source was modified")
    return {"nativeFirstSurvey": True, "nativeFirstSurveyReload": True,
            "firstSurveyCaptures": captures, "firstSurveySaveCaptures": saves,
            "firstSurveyDiagnostics": diagnostics}
