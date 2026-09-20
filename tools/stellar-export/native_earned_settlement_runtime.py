"""Orchestrate the maintained earned settlement replay through native UI completion."""
from __future__ import annotations

import json
import math
from pathlib import Path
import shutil
import subprocess
import tempfile

from native_bmp import validate_bmp
from native_first_exploration_runtime import _environment, _read_json, _unique_json
from native_settlement_runtime import _diagnostic
from native_settlement_completion_runtime import validate_native_settlement_completion_export


_CHECKPOINTS = ("eligible-site", "populated-vessel", "partial-establishment", "founded")


def _number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise RuntimeError(f"Earned settlement has invalid {name}")
    return float(value)


def _checker_evidence(stdout: str) -> tuple[dict, list[dict]]:
    records = []
    for line in stdout.splitlines():
        if not line.startswith("{"):
            continue
        try:
            value = _unique_json(line)
        except (TypeError, ValueError, json.JSONDecodeError) as error:
            raise RuntimeError("Earned settlement checker emitted malformed JSON") from error
        if isinstance(value, dict):
            records.append(value)
    finals = [x for x in records if x.get("kind") == "earned_settlement"]
    visits = [x for x in records if x.get("kind") == "earned_settlement_visit"]
    if len(finals) != 1 or not visits:
        raise RuntimeError("Earned settlement checker did not emit complete visit/final evidence")
    if any(set(x) != {"kind", "system_id", "day", "stage"} or type(x["system_id"]) is not int or
           x["stage"] != "reconnaissance" or not isinstance(x["day"], (int, float)) or isinstance(x["day"], bool) or
           not math.isfinite(float(x["day"]))
           for x in visits):
        raise RuntimeError("Earned settlement checker reported invalid visit evidence")
    final = finals[0]
    if final.get("terminal") != "established":
        raise RuntimeError(f"Earned settlement checker did not establish a settlement: {final.get('terminal')}")
    if set(final) != {"kind", "terminal", "before_day", "after_day", "step_days", "visited_systems",
                      "ordered_steps", "outcomes", "target", "fleet_id", "colony_id"}:
        raise RuntimeError("Earned settlement checker final evidence has the wrong schema")
    for key in ("before_day", "after_day", "step_days"):
        _number(final.get(key), key)
    if (final["before_day"] <= 0 or
            final["after_day"] <= final["before_day"] or
            final["after_day"] > final["before_day"] + 5000 or
            not math.isclose(final["step_days"], 1 / 64, abs_tol=1e-15)):
        raise RuntimeError("Earned settlement checker reported invalid time evidence")
    if (not isinstance(final.get("visited_systems"), list) or
            any(type(x) is not int for x in final["visited_systems"]) or
            not isinstance(final.get("ordered_steps"), list) or
            any(not isinstance(x, str) or not x for x in final["ordered_steps"])):
        raise RuntimeError("Earned settlement checker reported invalid visit traces")
    target = final.get("target")
    if (not isinstance(target, dict) or set(target) != {"system_id", "body_id", "kind"} or
            type(target["system_id"]) is not int or type(target["body_id"]) is not int or
            target["kind"] not in ("colony", "outpost")):
        raise RuntimeError("Earned settlement checker reported invalid target evidence")
    if (len(final["visited_systems"]) > 24 or len(set(final["visited_systems"])) != len(final["visited_systems"]) or
            [row["system_id"] for row in visits] != final["visited_systems"] or
            any(not final["before_day"] < visit["day"] < final["after_day"] for visit in visits) or
            any(visits[index]["day"] >= visits[index + 1]["day"] for index in range(len(visits) - 1)) or
            target["system_id"] != final["visited_systems"][-1]):
        raise RuntimeError("Earned settlement checker visit evidence is not chronological and bound to its target")
    outcomes = final.get("outcomes")
    if (not isinstance(outcomes, dict) or set(outcomes) != {"fleets", "colonies", "construction_states", "treasury"} or
            any(type(outcomes[x]) is not int or outcomes[x] < 0 for x in ("fleets", "colonies", "construction_states"))):
        raise RuntimeError("Earned settlement checker reported invalid outcomes")
    for key in ("fleet_id", "colony_id"):
        if type(final.get(key)) is not int or final[key] < 0:
            raise RuntimeError(f"Earned settlement checker reported invalid {key}")
    return final, visits


def _galaxy(payload, label):
    galaxy = payload.get("Galaxy")
    if not isinstance(galaxy, dict) or payload.get("FormatVersion") != 17 or galaxy.get("Seed") != 115501:
        raise RuntimeError(f"{label} is not Player17 seed 115501")
    player = galaxy.get("PlayerCivilizationId")
    if type(player) is not int:
        raise RuntimeError(f"{label} has invalid player")
    return galaxy, player


def _economy(galaxy, player, label):
    rows = [row for row in galaxy.get("Economies", []) if isinstance(row, dict) and row.get("CivilizationId") == player]
    if len(rows) != 1:
        raise RuntimeError(f"{label} has no unique player economy")
    return _number(rows[0].get("Credits"), f"{label} credits")


def _fleet(galaxy, fleet_id, label):
    rows = [row for row in galaxy.get("Fleets", []) if isinstance(row, dict) and row.get("Id") == fleet_id]
    if len(rows) != 1:
        raise RuntimeError(f"{label} has no unique earned settlement fleet")
    return rows[0]


def _surveyed_target(galaxy, player, target, label):
    knowledge = [row for row in galaxy.get("Knowledge", []) if isinstance(row, dict) and row.get("CivilizationId") == player]
    surveys = [row for row in (knowledge[0].get("SystemSurveys", []) if len(knowledge) == 1 else [])
               if isinstance(row, dict) and row.get("SystemId") == target["system_id"]]
    if len(surveys) != 1 or surveys[0].get("Level") != 3 or surveys[0].get("Progress") != 1:
        raise RuntimeError(f"{label} target is not fully surveyed")
    if not any(isinstance(row, dict) and row.get("Id") == target["body_id"] and row.get("SystemId") == target["system_id"]
               for row in galaxy.get("PlanetaryBodies", [])):
        raise RuntimeError(f"{label} target body is not in its surveyed system")


def _validate_checkpoint_chain(source_payload, checkpoint_payloads, final):
    target = final["target"]; fleet_id = final["fleet_id"]
    _, player = _galaxy(source_payload, "Earned source save")
    galaxies = {}
    days = [_number(source_payload.get("SimulationDays"), "source days")]
    for name in _CHECKPOINTS:
        galaxy, checkpoint_player = _galaxy(checkpoint_payloads[name], f"Earned {name} checkpoint")
        if checkpoint_player != player: raise RuntimeError("Earned checkpoints changed player identity")
        galaxies[name] = galaxy; days.append(_number(checkpoint_payloads[name].get("SimulationDays"), f"{name} days"))
    if (not math.isclose(days[0], final["before_day"], rel_tol=0, abs_tol=1e-8) or not math.isclose(days[-1], final["after_day"], rel_tol=0, abs_tol=1e-8) or any(days[i] >= days[i + 1] for i in range(len(days) - 1))):
        raise RuntimeError("Earned checkpoints are not strictly monotonic from source through founding")
    _surveyed_target(galaxies["eligible-site"], player, target, "Eligible-site checkpoint")
    fleet = _fleet(galaxies["populated-vessel"], fleet_id, "Populated-vessel checkpoint")
    design = "colony_ship" if target["kind"] == "colony" else "resource_outpost_ship"; population = 250.0 if design == "colony_ship" else 8.0
    if (fleet.get("CivilizationId") != player or fleet.get("IsActive") is not True or fleet.get("Role") != 2 or fleet.get("DesignId") != design or _number(fleet.get("EmbarkedPopulationMillions"), "populated vessel population") != population or any(fleet.get(key) is not None for key in ("DestinationSystemId", "DestinationPlanetaryBodyId", "SettlementBodyId"))):
        raise RuntimeError("Earned populated vessel is not an active unassigned paid registry vessel")
    partial = _fleet(galaxies["partial-establishment"], fleet_id, "Partial-establishment checkpoint")
    if (partial.get("DestinationSystemId") not in (target["system_id"], None) or (partial.get("DestinationSystemId") is None and partial.get("CurrentSystemId") != target["system_id"]) or partial.get("DestinationPlanetaryBodyId") != target["body_id"] or partial.get("SettlementBodyId") != target["body_id"] or partial.get("MissionOrderRevision") != fleet.get("MissionOrderRevision") + 1 or _number(partial.get("SettlementDaysCompleted"), "partial establishment progress") <= 0):
        raise RuntimeError("Earned partial establishment does not bind the ordered target and revision")
    founded = _fleet(galaxies["founded"], fleet_id, "Founded checkpoint")
    colonies = [row for row in galaxies["founded"].get("Colonies", []) if isinstance(row, dict) and row.get("CivilizationId") == player and row.get("Id") == final["colony_id"]]
    if (founded.get("IsActive") is not False or founded.get("EmbarkedPopulationMillions") != 0 or founded.get("MissionOrderRevision") != partial.get("MissionOrderRevision") + 1 or len(colonies) != 1 or colonies[0].get("SystemId") != target["system_id"] or colonies[0].get("PlanetaryBodyId") != target["body_id"] or colonies[0].get("Kind") != (0 if target["kind"] == "colony" else 1) or _number(colonies[0].get("PopulationMillions"), "founded population") <= 0):
        raise RuntimeError("Earned founded checkpoint lacks the consumed vessel and exact new populated colony")
    credits = _economy(galaxies["founded"], player, "Founded checkpoint")
    if (final["outcomes"]["fleets"] != len(galaxies["founded"].get("Fleets", [])) or final["outcomes"]["colonies"] != len(galaxies["founded"].get("Colonies", [])) or final["outcomes"]["construction_states"] != len(galaxies["founded"].get("ConstructionStates", [])) or not math.isclose(_number(final["outcomes"]["treasury"], "final treasury"), credits, abs_tol=1e-8)):
        raise RuntimeError("Earned final proof outcomes do not bind the founded checkpoint")
    return galaxies, player, fleet


def _run_checker(checker: Path, package: Path, catalog: Path, source: Path,
                 output: Path, env: dict[str, str]) -> subprocess.CompletedProcess:
    args = [str(checker), str(package / "Data" / "research" / "v1"), str(catalog),
            "--earned", str(source), "--output-dir", str(output)]
    result = subprocess.run(args, cwd=output, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="strict", timeout=330)
    if result.returncode == 2:
        raise RuntimeError(f"Earned settlement checker blocked progression:\n{result.stdout[-3000:]}\n{result.stderr[-3000:]}")
    if result.returncode:
        raise RuntimeError(f"Earned settlement checker failed ({result.returncode}):\n{result.stdout[-3000:]}\n{result.stderr[-3000:]}")
    return result


def validate_native_earned_settlement_export(package: Path, env: dict[str, str], source_save: Path,
                                              checker_exe: Path, catalog: Path):
    source = Path(source_save); checker = Path(checker_exe); catalog = Path(catalog)
    if not source.is_file() or not source.is_absolute():
        raise RuntimeError("Earned settlement source save must be an existing absolute file")
    if not checker.is_file() or not catalog.is_file():
        raise RuntimeError("Earned settlement checker or catalog is missing")
    source_bytes = source.read_bytes(); captures=[]; saves=[]
    with tempfile.TemporaryDirectory(prefix="stellar-native-earned-settlement-") as temporary:
        work=Path(temporary); checkpoints=work / "checkpoints"; checkpoints.mkdir()
        result=_run_checker(checker, package, catalog, source, checkpoints, dict(env))
        final, visits = _checker_evidence(result.stdout)
        checkpoint_payloads = {}
        for name in _CHECKPOINTS:
            path=checkpoints / f"{name}.player17.json"
            if not path.is_file(): raise RuntimeError(f"Earned settlement checker omitted {name} checkpoint")
            checkpoint_payloads[name] = _read_json(path, f"Earned {name} checkpoint")
            destination=package.parent / f"{package.name}-earned-{name}.player17.json"; shutil.copy2(path,destination); saves.append(str(destination))
        populated=checkpoints / "populated-vessel.player17.json"
        ui_save=work / "earned-ordered.player17.json"; shutil.copy2(populated,ui_save)
        before=_read_json(ui_save,"Earned populated vessel save")
        target=final["target"]; fleet_id=final["fleet_id"]
        source_payload = _read_json(source, "Earned source save")
        _, player, fleet = _validate_checkpoint_chain(source_payload, checkpoint_payloads, final)
        capture=work / "earned-settlement-ordered-1280x720.bmp"
        args=[str(package/"stellar-continuum-native.exe"),"--asset-root",str(package),"--save-path",str(ui_save),"--load","--seed","115501","--width","1280","--height","720","--settlement-smoke",str(capture)]
        ui=subprocess.run(args,cwd=work,env=_environment(env),capture_output=True,text=True,encoding="utf-8",errors="strict",timeout=300)
        if ui.returncode: raise RuntimeError(f"Earned settlement UI order failed ({ui.returncode}):\n{ui.stdout[-2000:]}\n{ui.stderr[-2000:]}")
        state=_diagnostic(ui.stdout,"ordered",target["kind"])
        if (state["fleet_id"]!=fleet_id or state["system_id"]!=target["system_id"] or state["body_id"]!=target["body_id"] or
                state["mission_revision"] != fleet.get("MissionOrderRevision",-1) + 1):
            raise RuntimeError("Earned settlement UI order changed the checker mission identity")
        after=_read_json(ui_save,"Earned ordered settlement save")
        if (len(after.get("Galaxy", {}).get("Colonies", [])) != len(before.get("Galaxy", {}).get("Colonies", [])) or
                any(row.get("CivilizationId") == player and row.get("PlanetaryBodyId") == target["body_id"]
                    for row in after.get("Galaxy", {}).get("Colonies", []) if isinstance(row, dict))):
            raise RuntimeError("Earned settlement UI order founded a colony before timed establishment")
        before_credits=_economy(before["Galaxy"], player, "Earned populated vessel save")
        if not math.isclose(state["treasury_before"], before_credits, abs_tol=1e-8):
            raise RuntimeError("Earned settlement UI diagnostic did not bind the real preorder treasury")
        saved_fleet = _fleet(after.get("Galaxy", {}), fleet_id, "Earned ordered settlement save")
        if (saved_fleet.get("DestinationSystemId") != target["system_id"] or
                saved_fleet.get("DestinationPlanetaryBodyId") != target["body_id"] or
                saved_fleet.get("MissionOrderRevision") != state["mission_revision"] or
                not math.isclose(_number(after.get("SimulationDays"), "ordered saved days"), state["saved_days"], abs_tol=1e-8)):
            raise RuntimeError("Earned ordered save does not bind the UI diagnostic mission and clock")
        validate_bmp(capture,1280,720,"earned settlement order",stdout=ui.stdout)
        destination=package.parent / f"{package.name}-earned-settlement-ordered-1280x720.bmp"; shutil.copy2(capture,destination); captures.append(str(destination))
        destination=package.parent / f"{package.name}-earned-settlement-ordered.player17.json"; shutil.copy2(ui_save,destination); saves.append(str(destination))
        completion=validate_native_settlement_completion_export(package,env,ui_save)
        if source.read_bytes()!=source_bytes: raise RuntimeError("Earned settlement source save was modified")
    return {"nativeEarnedSettlement":True,"earnedSettlementVisits":visits,"earnedSettlementProof":final,
            "earnedSettlementCaptures":captures+completion.get("settlementCompletionCaptures",[]),
            "earnedSettlementSaveCaptures":saves+completion.get("settlementCompletionSaveCaptures",[]),
            "earnedSettlementDiagnostics":result.stdout.strip(),**completion}
