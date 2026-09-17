"""Durable proof for expanding the earned colony through native UI."""

from __future__ import annotations
import json
import math
from pathlib import Path
import re
import shutil
import tempfile
from native_bmp import validate_bmp
from native_earned_surface_runtime import _integer, _launch, _number, _colony_identity, _construction_multiplier, _IDENTITY_FIELDS
from native_first_exploration_runtime import (
    _environment,
    _read_json,
    _same_paused,
    _unique_json,
)

_FIELDS = {
    "mode",
    "player_id",
    "system_id",
    "body_id",
    "colony_id",
    "before_days",
    "after_days",
    "power_supply_before",
    "power_supply_after",
    "science_before",
    "science_after",
    "research_instance_id",
    "research_lab_active_count",
    "research_labs_before",
    "research_labs_after",
    "fabricator_operational",
    "opened_surface",
    "roundtrip",
    "stages",
}
_STAGE_FIELDS = {
    "type_id",
    "building_id",
    "x",
    "z",
    "rotation",
    "authorization",
    "treasury_before",
    "treasury_after",
    "industry_cost",
    "industry_progress",
    "complete",
    "powered",
    "staffed",
    "enabled",
    "efficiency",
    "steps",
    "step_days",
    "cancel_unchanged",
}
_STEP = 1 / 64
_STAGES = (("power_generator", 25.0, 300.0), ("science_lab", 40.0, 400.0))


def _research(payload: dict) -> tuple[dict, list[dict], float]:
    root = payload.get("AdaptiveResearch")
    rows = root.get("Civilizations") if isinstance(root, dict) else None
    civilization = (
        next(
            (
                row
                for row in rows
                if isinstance(row, dict) and row.get("CivilizationId") == 0
            ),
            None,
        )
        if isinstance(rows, list)
        else None
    )
    node = civilization.get("Research") if isinstance(civilization, dict) else None
    for _ in range(8):
        if not isinstance(node, dict):
            break
        if isinstance(node.get("Core"), dict) and isinstance(
            node.get("Expertise"), dict
        ):
            institutions = node["Expertise"].get("Institutions")
            if not isinstance(institutions, list) or any(
                (not isinstance(row, dict) for row in institutions)
            ):
                break
            return (
                node,
                institutions,
                _number(
                    node["Core"].get("TotalEffectiveResearchLabs"),
                    "total research labs",
                ),
            )
        node = node.get("Research")
    raise RuntimeError(
        "Earned surface expansion save lacks player research institutions"
    )


def _source(payload: dict) -> dict:
    galaxy = payload.get("Galaxy")
    if (
        payload.get("FormatVersion") != 17
        or not isinstance(galaxy, dict)
        or galaxy.get("Seed") != 115501
        or (galaxy.get("PlayerCivilizationId") != 0)
    ):
        raise RuntimeError("Earned surface expansion source is not the Player17 earned colony")
    colonies = galaxy.get("Colonies")
    economies = galaxy.get("Economies")
    if not isinstance(colonies, list) or not isinstance(economies, list):
        raise RuntimeError("Earned surface expansion source is malformed")
    colony = [
        row
        for row in colonies
        if isinstance(row, dict)
        and row.get("Id") == 9
        and (row.get("CivilizationId") == 0)
    ]
    economy = [
        row
        for row in economies
        if isinstance(row, dict) and row.get("CivilizationId") == 0
    ]
    sites = colony[0].get("SurfaceBuildings") if len(colony) == 1 else None
    fabricators = (
        [
            row
            for row in sites
            if isinstance(row, dict) and row.get("TypeId") == "fabricator"
        ]
        if isinstance(sites, list)
        else []
    )
    if (
        len(colony) != 1
        or (not isinstance(sites, list))
        or (len(sites) != 1)
        or (len(fabricators) != 1)
        or (fabricators[0].get("IsComplete") is not True)
        or (fabricators[0].get("IsEnabled", True) is not True)
        or (len(economy) != 1)
    ):
        raise RuntimeError(
            "Earned surface expansion source lacks its completed fabricator"
        )
    if _number(economy[0].get("Credits"), "source treasury") < 65:
        raise RuntimeError(
            "Earned surface expansion source cannot pay both authorizations"
        )
    _, _, labs = _research(payload)
    fabricator = fabricators[0]
    arrays = {key: value for key, value in galaxy.items() if isinstance(value, list)}
    return {
        "identity": _colony_identity(colony[0]),
        "construction_multiplier": _construction_multiplier(payload, colony[0]),
        "days": _number(payload.get("SimulationDays"), "source days"),
        "treasury": _number(economy[0].get("Credits"), "source treasury"),
        "fabricator_id": _integer(fabricators[0].get("Id"), "fabricator id"),
        "fabricator": {
            key: _number(fabricator.get(key), f"fabricator {key}")
            for key in ("X", "Z", "RotationDegrees", "IndustryProgress", "Condition")
        },
        "research_labs": labs,
        "arrays": {
            key: [
                (row.get("Id"), row.get("CivilizationId"))
                for row in value
                if isinstance(row, dict)
            ]
            for key, value in arrays.items()
        },
        "counts": {key: len(value) for key, value in arrays.items()},
    }


def _proof(stdout: str, mode: str, construction_multiplier: float = 1.0) -> dict:
    rows = re.findall("(?m)^earned_surface_expansion=(\\{[^\\n]+\\})$", stdout)
    if len(rows) != 1:
        raise RuntimeError("Earned surface expansion did not report exactly one proof")
    try:
        value = _unique_json(rows[0])
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Earned surface expansion proof is malformed") from error
    if (
        not isinstance(value, dict)
        or set(value) != _FIELDS
        or value.get("mode") != mode
    ):
        raise RuntimeError("Earned surface expansion proof has the wrong schema")
    for key in _IDENTITY_FIELDS:
        _integer(value.get(key), key)
    if value["player_id"] != 0 or value["colony_id"] != 9:
        raise RuntimeError("Earned surface expansion proof has wrong player or earned colony")
    for key in (
        "before_days",
        "after_days",
        "power_supply_before",
        "power_supply_after",
        "science_before",
        "science_after",
        "research_labs_before",
        "research_labs_after",
    ):
        _number(value.get(key), key)
    if (
        not isinstance(value.get("research_instance_id"), str)
        or not value["research_instance_id"]
        or _integer(value.get("research_lab_active_count"), "research lab active count")
        != 1
    ):
        raise RuntimeError(
            "Earned surface expansion proof has invalid research evidence"
        )
    if any(
        (
            type(value.get(key)) is not bool
            for key in ("fabricator_operational", "opened_surface", "roundtrip")
        )
    ) or any(
        (
            value[key] is not True
            for key in ("fabricator_operational", "opened_surface", "roundtrip")
        )
    ):
        raise RuntimeError(
            "Earned surface expansion proof omitted operational evidence"
        )
    stages = value.get("stages")
    expected_stages = (
        (("fabricator", 0.0, 450.0), *_STAGES) if mode == "paused" else _STAGES
    )
    if not isinstance(stages, list) or len(stages) != len(expected_stages):
        raise RuntimeError("Earned surface expansion proof has wrong stages")
    total_steps = 0
    for stage, (type_id, authorization, cost) in zip(stages, expected_stages):
        if (
            not isinstance(stage, dict)
            or set(stage) != _STAGE_FIELDS
            or stage.get("type_id") != type_id
            or (_integer(stage.get("building_id"), "building id") < 0)
        ):
            raise RuntimeError("Earned surface expansion stage identity is invalid")
        for key in (
            "x",
            "z",
            "rotation",
            "authorization",
            "treasury_before",
            "treasury_after",
            "industry_cost",
            "industry_progress",
            "efficiency",
            "step_days",
        ):
            _number(stage.get(key), key)
        steps = _integer(stage.get("steps"), "stage steps")
        expected_authorization = 0 if mode == "paused" else authorization * construction_multiplier
        if (
            stage["authorization"] != expected_authorization
            or stage["industry_cost"] != cost
            or (mode != "paused" and steps <= 0)
            or (steps > 1024 * 64)
            or (not math.isclose(stage["step_days"], _STEP, rel_tol=0, abs_tol=1e-15))
            or (
                not math.isclose(
                    stage["treasury_before"] - stage["treasury_after"],
                    expected_authorization,
                    rel_tol=0,
                    abs_tol=1e-07,
                )
            )
            or (
                not math.isclose(
                    stage["industry_progress"], cost, rel_tol=0, abs_tol=1e-07
                )
            )
            or any(
                (
                    stage.get(key) is not True
                    for key in (
                        "complete",
                        "powered",
                        "staffed",
                        "enabled",
                        "cancel_unchanged",
                    )
                )
            )
            or (stage["efficiency"] <= 0)
        ):
            raise RuntimeError("Earned surface expansion stage economics are invalid")
        total_steps += steps
    if len({stage["building_id"] for stage in stages}) != len(expected_stages):
        raise RuntimeError("Earned surface expansion stage IDs are duplicated")
    science_id = stages[-1]["building_id"]
    if value["research_instance_id"] != f"construction:surface:9:{science_id}":
        raise RuntimeError(
            "Earned surface expansion proof binds research to the wrong site"
        )
    elapsed = value["after_days"] - value["before_days"]
    if (
        value["before_days"] < 0
        or elapsed < 0
        or (not math.isclose(elapsed, total_steps * _STEP, rel_tol=0, abs_tol=1e-07))
    ):
        raise RuntimeError("Earned surface expansion proof has invalid clock evidence")
    if mode == "paused":
        if (
            total_steps
            or elapsed
            or any((stage["authorization"] != 0 for stage in stages))
            or (value["power_supply_after"] != value["power_supply_before"])
            or (value["science_after"] != value["science_before"])
            or (value["research_labs_after"] != value["research_labs_before"])
        ):
            raise RuntimeError(
                "Earned surface expansion paused proof mutated canonical state"
            )
    elif (
        not total_steps
        or value["power_supply_after"] - value["power_supply_before"] != 4
        or value["science_after"] - value["science_before"] != 1
        or (value["research_labs_after"] != value["research_labs_before"] + 1)
    ):
        raise RuntimeError(
            "Earned surface expansion did not prove power and science output"
        )
    return value


def _save(payload: dict, proof: dict, source: dict) -> None:
    if tuple(proof[key] for key in _IDENTITY_FIELDS) != source["identity"]:
        raise RuntimeError("Earned surface expansion changed the source colony identity")
    galaxy = payload.get("Galaxy")
    if (
        payload.get("FormatVersion") != 17
        or not isinstance(galaxy, dict)
        or galaxy.get("Seed") != 115501
        or (galaxy.get("PlayerCivilizationId") != 0)
    ):
        raise RuntimeError("Earned surface expansion save changed Player17 identity")
    arrays = {key: value for key, value in galaxy.items() if isinstance(value, list)}
    if {key: len(value) for key, value in arrays.items()} != source["counts"] or {
        key: [
            (row.get("Id"), row.get("CivilizationId"))
            for row in value
            if isinstance(row, dict)
        ]
        for key, value in arrays.items()
    } != source["arrays"]:
        raise RuntimeError(
            "Earned surface expansion changed persistent entity identity"
        )
    colonies = galaxy.get("Colonies", [])
    colony = next(
        (
            row
            for row in colonies
            if isinstance(row, dict)
            and row.get("Id") == 9
            and (row.get("CivilizationId") == 0)
        ),
        None,
    )
    sites = colony.get("SurfaceBuildings") if isinstance(colony, dict) else None
    if colony is None or _colony_identity(colony) != source["identity"]:
        raise RuntimeError("Earned surface expansion moved the saved colony")
    if not isinstance(sites, list) or len(sites) != 3:
        raise RuntimeError("Earned surface expansion save has wrong site count")
    expected = {
        source["fabricator_id"]: "fabricator",
        **{stage["building_id"]: stage["type_id"] for stage in proof["stages"]},
    }
    if {
        row.get("Id"): row.get("TypeId") for row in sites if isinstance(row, dict)
    } != expected:
        raise RuntimeError("Earned surface expansion save changed site identities")
    fabricator = next(
        (
            row
            for row in sites
            if isinstance(row, dict) and row.get("Id") == source["fabricator_id"]
        ),
        None,
    )
    if (
        not isinstance(fabricator, dict)
        or fabricator.get("IsComplete") is not True
        or fabricator.get("IsEnabled", True) is not True
        or any(
            (
                not math.isclose(
                    _number(fabricator.get(key), f"saved fabricator {key}"),
                    value,
                    rel_tol=0,
                    abs_tol=1e-06,
                )
                for key, value in source["fabricator"].items()
            )
        )
    ):
        raise RuntimeError(
            "Earned surface expansion did not preserve completed fabricator state"
        )
    for stage in proof["stages"]:
        site = next((row for row in sites if row.get("Id") == stage["building_id"]))
        if (
            site.get("IsComplete") is not True
            or site.get("IsEnabled", True) is not True
            or any(
                (
                    not math.isclose(
                        _number(site.get(key), key),
                        stage[value],
                        rel_tol=0,
                        abs_tol=1e-06,
                    )
                    for key, value in (
                        ("X", "x"),
                        ("Z", "z"),
                        ("RotationDegrees", "rotation"),
                        ("IndustryProgress", "industry_progress"),
                    )
                )
            )
        ):
            raise RuntimeError(
                "Earned surface expansion proof does not bind saved sites"
            )
    if not math.isclose(
        _number(payload.get("SimulationDays"), "saved days"),
        proof["after_days"],
        rel_tol=0,
        abs_tol=1e-07,
    ):
        raise RuntimeError("Earned surface expansion proof does not bind saved time")
    _, institutions, labs = _research(payload)
    research = [
        row
        for row in institutions
        if row.get("InstitutionInstanceId") == proof["research_instance_id"]
    ]
    if (
        len(research) != 1
        or research[0].get("InstitutionArchetypeId") != "surface_science_laboratory"
        or research[0].get("ContextId") != "colony:9"
        or (type(research[0].get("TotalCount")) is not int)
        or (type(research[0].get("ActiveCount")) is not int)
        or (research[0]["TotalCount"] != 1)
        or (research[0]["ActiveCount"] != proof["research_lab_active_count"])
        or (
            not math.isclose(
                labs, proof["research_labs_after"], rel_tol=0, abs_tol=1e-07
            )
        )
    ):
        raise RuntimeError(
            "Earned surface expansion proof does not bind saved research institution"
        )


def _bind_paused_final(paused: dict, resume: dict, payload: dict) -> None:
    if (
        paused["power_supply_before"] != resume["power_supply_after"]
        or paused["power_supply_after"] != resume["power_supply_after"]
        or paused["science_before"] != resume["science_after"]
        or (paused["science_after"] != resume["science_after"])
        or (paused["research_labs_before"] != resume["research_labs_after"])
        or (paused["research_labs_after"] != resume["research_labs_after"])
    ):
        raise RuntimeError("Earned surface expansion paused proof changed final output")
    galaxy = payload.get("Galaxy")
    economies = galaxy.get("Economies") if isinstance(galaxy, dict) else None
    economy = (
        next(
            (
                row
                for row in economies
                if isinstance(row, dict) and row.get("CivilizationId") == 0
            ),
            None,
        )
        if isinstance(economies, list)
        else None
    )
    if economy is None or any(
        (
            not math.isclose(
                stage[side],
                _number(economy.get("Credits"), "paused treasury"),
                rel_tol=0,
                abs_tol=1e-07,
            )
            for stage in paused["stages"]
            for side in ("treasury_before", "treasury_after")
        )
    ):
        raise RuntimeError(
            "Earned surface expansion paused proof does not bind saved treasury"
        )


def validate_native_earned_surface_expansion_export(
    package: Path, env: dict[str, str], source_save: Path
):
    package, source_path = (Path(package), Path(source_save))
    if not source_path.is_absolute() or not source_path.is_file():
        raise RuntimeError(
            "Earned surface expansion source save must be an absolute file"
        )
    source_bytes = source_path.read_bytes()
    before = _read_json(source_path, "Earned surface expansion source")
    source = _source(before)
    captures = []
    saves = []
    proofs = []
    with tempfile.TemporaryDirectory(
        prefix="stellar-native-earned-surface-expansion-"
    ) as temporary:
        work = Path(temporary)
        save = work / "earned-surface-expansion.player17.json"
        shutil.copy2(source_path, save)
        capture = work / "earned-surface-expansion-1280x720.bmp"
        result = _launch(
            package,
            work,
            _environment(env),
            save,
            capture,
            "--earned-surface-expansion-smoke",
            1280,
            720,
        )
        resume = _proof(result.stdout, "expansion", source["construction_multiplier"])
        after = _read_json(save, "Earned surface expansion completed save")
        if not math.isclose(
            resume["before_days"], source["days"], rel_tol=0, abs_tol=1e-07
        ) or not math.isclose(
            resume["research_labs_before"],
            source["research_labs"],
            rel_tol=0,
            abs_tol=1e-07,
        ):
            raise RuntimeError(
                "Earned surface expansion proof does not bind source time or research labs"
            )
        if not math.isclose(
            resume["stages"][0]["treasury_before"],
            source["treasury"],
            rel_tol=0,
            abs_tol=1e-07,
        ):
            raise RuntimeError(
                "Earned surface expansion first payment does not bind source treasury"
            )
        _save(after, resume, source)
        for name in (
            "",
            "-power-review",
            "-power-construction",
            "-power-complete",
            "-science-review",
            "-science-construction",
            "-science-complete",
        ):
            frame = capture.with_name(capture.stem + name + ".bmp")
            validate_bmp(
                frame, 1280, 720, "earned surface expansion", stdout=result.stdout
            )
            target = package.parent / f"{package.name}-{frame.name}"
            shutil.copy2(frame, target)
            captures.append(str(target))
        target = (
            package.parent / f"{package.name}-earned-surface-expansion.player17.json"
        )
        shutil.copy2(save, target)
        saves.append(str(target))
        proofs.append(resume)
        capture = work / "earned-surface-expansion-paused-1920x1080.bmp"
        result = _launch(
            package,
            work,
            _environment(env),
            save,
            capture,
            "--earned-surface-expansion-paused-smoke",
            1920,
            1080,
        )
        paused = _proof(result.stdout, "paused")
        stable = _read_json(save, "Earned surface expansion paused save")
        if not _same_paused(after, stable):
            raise RuntimeError(
                "Earned surface expansion paused reload changed Player17"
            )
        if not math.isclose(
            paused["before_days"], resume["after_days"], rel_tol=0, abs_tol=1e-07
        ) or not math.isclose(
            paused["after_days"], paused["before_days"], rel_tol=0, abs_tol=1e-07
        ):
            raise RuntimeError(
                "Earned surface expansion paused proof does not bind completed clock"
            )
        _bind_paused_final(paused, resume, stable)
        _save(stable, paused, source)
        validate_bmp(
            capture, 1920, 1080, "earned surface expansion paused", stdout=result.stdout
        )
        target = package.parent / f"{package.name}-{capture.name}"
        shutil.copy2(capture, target)
        captures.append(str(target))
        target = (
            package.parent
            / f"{package.name}-earned-surface-expansion-paused.player17.json"
        )
        shutil.copy2(save, target)
        saves.append(str(target))
        proofs.append(paused)
        if source_path.read_bytes() != source_bytes:
            raise RuntimeError("Earned surface expansion source was modified")
    return {
        "nativeEarnedSurfaceExpansion": True,
        "earnedSurfaceExpansionCaptures": captures,
        "earnedSurfaceExpansionSaveCaptures": saves,
        "earnedSurfaceExpansionProofs": proofs,
    }
