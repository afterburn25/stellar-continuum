"""Native fresh-campaign progression through the first two completed ships."""
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


_EXPECTED_FIELDS = {
    "mode", "seed", "player_id", "before_days", "after_days", "scout_id",
    "science_id", "research_actions", "construction_actions", "ship_actions",
    "no_instant_ships", "offline_quarter_day_steps", "ui_input", "paused",
    "save_roundtrip",
}
_RESEARCH_NODES = {
    "in_space_assembly", "asteroid_prospecting", "asteroid_mining",
    "vacuum_refining", "orbital_manufacturing", "orbital_shipyard",
    "gravitational_physics", "field_theory", "warp_metric_theory",
    "exotic_energy_coupling", "micro_field_distortion", "warp_field_control",
    "prototype_warp_drive",
}
_CAPABILITIES = {"spacecraft_construction", "experimental_interstellar_transit",
                 "orbital_industry"}
_PLAYER_SCALAR_FIELDS = ("Id", "CivilizationId")


def _unique_json(text: str):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result
    return json.loads(text, object_pairs_hook=pairs)


def _proof(stdout: str, mode: str) -> dict:
    rows = [line for line in stdout.splitlines() if "fresh_progression=" in line]
    if len(rows) != 1:
        raise RuntimeError("Native fresh progression did not report exactly one proof line")
    match = re.fullmatch(r"fresh_progression=(\{.*\})", rows[0])
    if not match:
        raise RuntimeError("Native fresh progression proof is malformed")
    try:
        report = _unique_json(match.group(1))
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Native fresh progression proof is malformed") from error
    if not isinstance(report, dict) or set(report) != _EXPECTED_FIELDS or report.get("mode") != mode:
        raise RuntimeError("Native fresh progression proof has the wrong schema")
    if type(report.get("seed")) is not int or report.get("seed") != 115501:
        raise RuntimeError("Native fresh progression used the wrong seed")
    for key in ("player_id", "scout_id", "science_id"):
        if type(report[key]) is not int or report[key] < 0:
            raise RuntimeError(f"Native fresh progression reported invalid {key}")
    if report["scout_id"] == report["science_id"]:
        raise RuntimeError("Native fresh progression reused one ship identity")
    for key in ("before_days", "after_days"):
        if isinstance(report[key], bool) or not isinstance(report[key], (int, float)) or not math.isfinite(report[key]):
            raise RuntimeError(f"Native fresh progression reported invalid {key}")
    if report["before_days"] < 0 or report["after_days"] <= 0 or report["after_days"] > 10958:
        raise RuntimeError("Native fresh progression reported an invalid time range")
    if mode == "fresh" and report["before_days"] != 0:
        raise RuntimeError("Native fresh progression did not start at day zero")
    if mode == "paused_reload" and report["before_days"] <= 0:
        raise RuntimeError("Native fresh progression reload did not report its loaded day")
    positive = ("research_actions", "construction_actions", "ship_actions",
                "offline_quarter_day_steps")
    for key in positive:
        if type(report[key]) is not int or report[key] < 0:
            raise RuntimeError(f"Native fresh progression reported invalid {key}")
    if mode == "fresh":
        step_delta = 4.0 * (report["after_days"] - report["before_days"])
        if (not math.isfinite(step_delta) or
                not math.isclose(step_delta, round(step_delta), rel_tol=0, abs_tol=1e-9)):
            raise RuntimeError("Native fresh progression reported a non-quarter-day progression")
        expected_steps = int(round(step_delta))
        if (report["research_actions"] != 13 or
                report["construction_actions"] != 3 or report["ship_actions"] != 2 or
                report["offline_quarter_day_steps"] != expected_steps):
            raise RuntimeError("Native fresh progression did not prove the complete action path")
    elif any(report[key] != 0 for key in positive):
        raise RuntimeError("Native fresh progression reload repeated progression actions")
    for key in ("no_instant_ships", "ui_input", "paused", "save_roundtrip"):
        if report[key] is not True:
            raise RuntimeError(f"Native fresh progression did not prove {key}")
    return report


def _player_entry(payload: dict, collection: str, player_id: int) -> dict:
    rows = [row for row in payload.get("Galaxy", {}).get(collection, [])
            if type(row.get("CivilizationId")) is int and
            row.get("CivilizationId") == player_id]
    if len(rows) != 1:
        raise RuntimeError(f"Native fresh progression has no unique player {collection} row")
    return rows[0]


def _validate_payload(payload: dict, proof: dict, mode: str):
    galaxy = payload.get("Galaxy", {})
    if payload.get("FormatVersion") != 17 or not payload.get("SavedAtUtc"):
        raise RuntimeError("Native fresh progression save is not a timestamped Player17 payload")
    if (type(galaxy.get("Seed")) is not int or galaxy["Seed"] != 115501 or
            type(galaxy.get("PlayerCivilizationId")) is not int or
            galaxy["PlayerCivilizationId"] != proof["player_id"]):
        raise RuntimeError("Native fresh progression save has the wrong campaign identity")
    civilization_rows = galaxy.get("Civilizations", [])
    if (any(type(row.get("Id")) is not int or row.get("Id") < 0
            for row in civilization_rows) or
            len({row.get("Id") for row in civilization_rows}) != len(civilization_rows)):
        raise RuntimeError("Native fresh progression save has invalid civilization identities")
    players = [row for row in civilization_rows if row.get("IsPlayer") is True]
    if (len(players) != 1 or type(players[0].get("Id")) is not int or
            players[0]["Id"] != proof["player_id"]):
        raise RuntimeError("Native fresh progression save has no unique player civilization")
    if len(galaxy.get("Systems", [])) != 500:
        raise RuntimeError("Native fresh progression save has the wrong system count")
    days = payload.get("SimulationDays")
    if isinstance(days, bool) or not isinstance(days, (int, float)) or not math.isfinite(days):
        raise RuntimeError("Native fresh progression save has invalid simulation time")
    if not math.isclose(float(days), float(proof["after_days"]), rel_tol=1e-9, abs_tol=1e-6):
        raise RuntimeError("Native fresh progression proof disagrees with its save")
    construction = _player_entry(payload, "ConstructionStates", proof["player_id"])
    completed = construction.get("CompletedProjectIds", [])
    if any(project not in completed for project in
           ("orbital_launch_complex", "orbital_shipyard", "warp_test_facility")):
        raise RuntimeError("Native fresh progression save is missing a required completed facility")
    yard = _player_entry(payload, "ShipyardStates", proof["player_id"])
    if (type(yard.get("NextOrderSequence")) is not int or yard["NextOrderSequence"] != 3 or
            yard.get("ActiveDesignId") is not None or yard.get("ActiveOrderId") is not None or
            yard.get("QueuedBuilds") != [] or
            yard.get("ReservedPopulationMillions") != 0 or
            yard.get("ReservedPopulationSpeciesId") is not None or
            yard.get("ReservedPopulationSourceColonyId") is not None):
        raise RuntimeError("Native fresh progression save did not complete its first two ship orders")
    all_fleets = galaxy.get("Fleets", [])
    fleet_ids = [fleet.get("Id") for fleet in all_fleets]
    if (any(type(value) is not int or value < 0 for value in fleet_ids) or
            len(set(fleet_ids)) != len(fleet_ids)):
        raise RuntimeError("Native fresh progression save has duplicate or invalid fleet identities")
    fleets = [fleet for fleet in all_fleets
              if type(fleet.get("CivilizationId")) is int and
              fleet.get("CivilizationId") == proof["player_id"]]
    if len(fleets) != 2 or any(fleet.get("IsActive") is not True for fleet in fleets):
        raise RuntimeError("Native fresh progression did not stop after the first two owned ships")
    matches = {"scout_id": [], "science_id": []}
    for fleet in fleets:
        if fleet.get("DesignId") == "warp_scout" and fleet.get("Role") == 0:
            matches["scout_id"].append(fleet)
        if fleet.get("DesignId") == "science_vessel" and fleet.get("Role") == 1:
            matches["science_id"].append(fleet)
    for key in matches:
        rows = [fleet for fleet in matches[key] if fleet.get("Id") == proof[key]]
        if len(rows) != 1:
            raise RuntimeError(f"Native fresh progression proof does not identify its owned {key}")
    if proof["scout_id"] == proof["science_id"]:
        raise RuntimeError("Native fresh progression selected one fleet twice")
    civilizations = [row for row in payload.get("AdaptiveResearch", {}).get("Civilizations", [])
                     if type(row.get("CivilizationId")) is int and
                     row.get("CivilizationId") == proof["player_id"]]
    if len(civilizations) != 1:
        raise RuntimeError("Native fresh progression save has no unique player research row")
    core = civilizations[0].get("Research", {}).get("Research", {}).get("Research", {}).get("Research", {}).get("Core", {})
    capabilities = {row.get("CapabilityId") for row in core.get("Capabilities", [])}
    if not _CAPABILITIES.issubset(capabilities):
        raise RuntimeError("Native fresh progression save is missing canonical ship capabilities")
    research_rows = core.get("Nodes", [])
    node_ids = [row.get("NodeId") for row in research_rows]
    if len(node_ids) != len(set(node_ids)):
        raise RuntimeError("Native fresh progression save has duplicate research node identities")
    nodes = {row.get("NodeId"): row for row in research_rows}
    if any(node not in nodes or type(nodes[node].get("Maturity")) is not int or
           (nodes[node]["Maturity"] != 7 if node != "prototype_warp_drive" else
            nodes[node]["Maturity"] not in (5, 6, 7)) for node in _RESEARCH_NODES):
        raise RuntimeError("Native fresh progression save is missing canonical research gates")


def _environment(env: dict[str, str]) -> dict[str, str]:
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    return dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))


def _launch(folder: Path, work: Path, env: dict[str, str], save: Path,
            capture: Path, mode: str, width: int, height: int, timeout: int):
    args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
            "--save-path", str(save), "--seed", "115501", "--width", str(width),
            "--height", str(height), mode, str(capture)]
    if mode == "--fresh-progression-reload-smoke":
        args.append("--load")
    result = subprocess.run(args, cwd=work, env=env, capture_output=True,
                            text=True, encoding="utf-8", errors="strict", timeout=timeout)
    if result.returncode != 0:
        details = "\n".join(value for value in (result.stdout, result.stderr) if value)
        raise RuntimeError(f"Native fresh progression {mode} failed ({result.returncode}): {details}")
    if "gpu_driver=vulkan " not in result.stdout or "systems=500 " not in result.stdout or "save=ok " not in result.stdout:
        raise RuntimeError("Native fresh progression did not confirm renderer, campaign and save")
    return result


def validate_native_fresh_progression_export(folder: Path, env: dict[str, str]):
    clean = _environment(env)
    captures, save_captures, diagnostics, payloads, proofs = [], [], [], [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-fresh-progression-") as temporary:
        work = Path(temporary)
        save = work / "fresh-progression.player17.json"
        for index, (mode, label, width, height, timeout) in enumerate((
                ("--fresh-progression-smoke", "fresh", 1280, 720, 600),
                ("--fresh-progression-reload-smoke", "paused_reload", 1920, 1080, 120))):
            capture = work / f"fresh-progression-{width}x{height}.bmp"
            result = _launch(folder, work, clean, save, capture, mode, width, height, timeout)
            proof = _proof(result.stdout, label)
            if not save.is_file():
                raise RuntimeError("Native fresh progression did not write its Player17 save")
            payload = json.loads(save.read_text(encoding="utf-8-sig"))
            _validate_payload(payload, proof, label)
            validate_bmp(capture, width, height, "fresh progression", stdout=result.stdout)
            evidence = folder.parent / f"{folder.name}-fresh-progression-{label}.bmp"
            save_evidence = folder.parent / f"{folder.name}-fresh-progression-{label}.player17.json"
            shutil.copy2(capture, evidence)
            shutil.copy2(save, save_evidence)
            captures.append(str(evidence))
            save_captures.append(str(save_evidence))
            diagnostics.append(result.stdout.strip())
            payloads.append(payload)
            proofs.append(proof)
        before = copy.deepcopy(payloads[0]); after = copy.deepcopy(payloads[1])
        before.pop("SavedAtUtc", None); after.pop("SavedAtUtc", None)
        if before != after:
            raise RuntimeError("Native fresh progression paused reload changed the Player17 payload")
        if proofs[1]["before_days"] != proofs[0]["after_days"]:
            raise RuntimeError("Native fresh progression reload proof has the wrong loaded day")
        if proofs[0]["scout_id"] != proofs[1]["scout_id"] or proofs[0]["science_id"] != proofs[1]["science_id"]:
            raise RuntimeError("Native fresh progression changed ship identity during reload")
    return {"nativeFreshProgression": True, "nativeFreshProgressionReload": True,
            "freshProgressionCaptures": captures,
            "freshProgressionSaveCaptures": save_captures,
            "freshProgressionDiagnostics": diagnostics}
