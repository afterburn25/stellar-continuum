"""Validate the read-only settlement preparation screen against a full survey save."""
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


_FIELDS = {"system_id", "body_id", "player_id", "read_only", "unsuitable_site",
           "colony_cost_visible", "outpost_cost_visible", "shipyard_opened",
           "review_closed", "days"}
_RUNS = ((1280, 720, "1280x720"), (1920, 1080, "1920x1080"))


def _number(value, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise RuntimeError(f"Native settlement preparation has invalid {name}")
    return float(value)


def _proof(stdout: str, expected_days: float) -> dict:
    rows = [line for line in stdout.splitlines() if "settlement_preparation=" in line]
    if len(rows) != 1:
        raise RuntimeError("Native settlement preparation did not report exactly one proof")
    match = re.fullmatch(r"settlement_preparation=(\{.*\})", rows[0])
    if not match:
        raise RuntimeError("Native settlement preparation proof is malformed")
    try:
        proof = _unique_json(match.group(1))
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Native settlement preparation proof is malformed") from error
    if not isinstance(proof, dict) or set(proof) != _FIELDS:
        raise RuntimeError("Native settlement preparation proof has the wrong schema")
    for key in ("system_id", "body_id", "player_id"):
        if type(proof.get(key)) is not int or proof[key] < 0:
            raise RuntimeError(f"Native settlement preparation has invalid {key}")
    for key in ("read_only", "unsuitable_site", "colony_cost_visible", "outpost_cost_visible",
                "shipyard_opened", "review_closed"):
        if type(proof.get(key)) is not bool or proof[key] is not True:
            raise RuntimeError(f"Native settlement preparation has invalid {key}")
    days = _number(proof.get("days"), "days")
    if proof["system_id"] != 1 or proof["body_id"] != 1001 or not math.isclose(days, expected_days, rel_tol=0, abs_tol=1e-9):
        raise RuntimeError("Native settlement preparation proof has the wrong target or time")
    return proof


def _full_survey_source(payload: dict) -> tuple[int, float]:
    galaxy = payload.get("Galaxy")
    if (not isinstance(galaxy, dict) or payload.get("FormatVersion") != 17 or
            galaxy.get("Seed") != 115501 or type(galaxy.get("PlayerCivilizationId")) is not int):
        raise RuntimeError("Settlement preparation source is not a Player17 seed 115501 save")
    if not isinstance(payload.get("SavedAtUtc"), str) or not payload["SavedAtUtc"]:
        raise RuntimeError("Settlement preparation source has no save timestamp")
    systems = galaxy.get("Systems")
    if (not isinstance(systems, list) or len(systems) != 500 or
            any(not isinstance(row, dict) or type(row.get("Id")) is not int for row in systems) or
            {row["Id"] for row in systems} != set(range(500))):
        raise RuntimeError("Settlement preparation source has invalid systems")
    bodies = galaxy.get("PlanetaryBodies")
    body = next((row for row in bodies or [] if isinstance(row, dict) and row.get("Id") == 1001), None)
    if not isinstance(body, dict) or body.get("SystemId") != 1 or body.get("ParentBodyId") is not None:
        raise RuntimeError("Settlement preparation source lacks body 1001 in system 1")
    knowledge = galaxy.get("Knowledge")
    player = galaxy["PlayerCivilizationId"]
    rows = [row for row in knowledge or [] if isinstance(row, dict) and row.get("CivilizationId") == player]
    surveys = rows[0].get("SystemSurveys") if len(rows) == 1 else None
    target = [row for row in surveys or [] if isinstance(row, dict) and row.get("SystemId") == 1]
    if len(target) != 1 or target[0].get("Level") != 3 or target[0].get("Progress") != 1:
        raise RuntimeError("Settlement preparation source lacks the completed full survey")
    return player, _number(payload.get("SimulationDays"), "simulation days")


def _launch(folder: Path, work: Path, env: dict[str, str], save: Path, capture: Path,
            width: int, height: int) -> subprocess.CompletedProcess:
    args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
            "--save-path", str(save), "--load", "--seed", "115501", "--width", str(width),
            "--height", str(height), "--settlement-preparation-smoke", str(capture)]
    try:
        result = subprocess.run(args, cwd=work, env=env, capture_output=True, text=True,
                                encoding="utf-8", errors="strict", timeout=300)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Native settlement preparation timed out") from error
    if result.returncode:
        raise RuntimeError(f"Native settlement preparation failed ({result.returncode}):\n{result.stdout[-2000:]}\n{result.stderr[-2000:]}")
    if any(token not in result.stdout for token in ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
        raise RuntimeError("Native settlement preparation did not confirm renderer, campaign and save")
    return result


def validate_native_settlement_preparation_export(package: Path, env: dict[str, str], source_save: Path):
    source = Path(source_save)
    if not source.is_file():
        raise RuntimeError("Full survey source save is missing")
    source_bytes = source.read_bytes()
    initial = _read_json(source, "Full survey source save")
    player, days = _full_survey_source(initial)
    captures, saves, diagnostics = [], [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-settlement-preparation-") as temporary:
        work = Path(temporary)
        for width, height, label in _RUNS:
            save = work / f"settlement-preparation-{label}.player17.json"
            shutil.copy2(source, save)
            capture = work / f"settlement-preparation-{label}.bmp"
            result = _launch(package, work, _environment(env), save, capture, width, height)
            proof = _proof(result.stdout, days)
            if proof["player_id"] != player:
                raise RuntimeError("Settlement preparation proof changed player identity")
            after = _read_json(save, "Native settlement preparation save")
            if not _same_paused(initial, after):
                raise RuntimeError("Settlement preparation changed the Player17 payload")
            validate_bmp(capture, width, height, "settlement preparation", stdout=result.stdout)
            artifacts = [capture] + [capture.with_name(capture.stem + suffix + ".bmp")
                                     for suffix in ("-assessment", "-costs", "-shipyard")]
            for artifact in artifacts:
                validate_bmp(artifact, width, height, "settlement preparation sidecar", stdout=result.stdout)
                destination = package.parent / f"{package.name}-{artifact.name}"
                shutil.copy2(artifact, destination)
                captures.append(str(destination))
            destination = package.parent / f"{package.name}-settlement-preparation-{label}.player17.json"
            shutil.copy2(save, destination)
            saves.append(str(destination))
            diagnostics.append(result.stdout.strip())
        if source.read_bytes() != source_bytes:
            raise RuntimeError("Full survey source save was modified")
    return {"nativeSettlementPreparation": True,
            "settlementPreparationCaptures": captures,
            "settlementPreparationSaveCaptures": saves,
            "settlementPreparationDiagnostics": diagnostics}
