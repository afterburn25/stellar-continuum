"""Relocated native shipyard and construction UI runtime checks."""
from __future__ import annotations

import copy
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def _source_row(fixture: Path) -> dict:
    root = json.loads(fixture.read_text(encoding="utf-8"))
    rows = root.get("Rows")
    if not isinstance(rows, list):
        raise RuntimeError("Player17 production fixture has no source-authored rows")
    matches = [row for row in rows if row.get("Name") == "valid-current17"]
    if len(matches) != 1 or not isinstance(matches[0].get("InputJson"), str):
        raise RuntimeError("Player17 production fixture has no unique valid-current17 input")
    return json.loads(matches[0]["InputJson"])


def _player_entry(payload: dict, collection: str) -> tuple[dict, int]:
    galaxy = payload.get("Galaxy", {})
    player_id = galaxy.get("PlayerCivilizationId")
    matches = [entry for entry in galaxy.get(collection, [])
               if entry.get("CivilizationId") == player_id]
    if len(matches) != 1:
        raise RuntimeError(
            f"Production save has no unique player-owned {collection} row")
    return matches[0], player_id


def _player_economy(payload: dict) -> dict:
    return _player_entry(payload, "Economies")[0]


def _research_core(payload: dict) -> dict:
    player_id = payload.get("Galaxy", {}).get("PlayerCivilizationId")
    matches = [entry for entry in
               payload.get("AdaptiveResearch", {}).get("Civilizations", [])
               if entry.get("CivilizationId") == player_id]
    if len(matches) != 1:
        raise RuntimeError("Production fixture has no unique player research row")
    return matches[0]["Research"]["Research"]["Research"]["Research"]["Core"]


def _clean_environment(env: dict[str, str]) -> dict[str, str]:
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    return dict(env, PATH=str(system_root / "System32") +
                os.pathsep + str(system_root))


def _capture_is_bmp(path: Path) -> bool:
    return path.is_file() and path.stat().st_size >= 54 and path.read_bytes()[:2] == b"BM"


def _without_timestamp(payload: dict) -> dict:
    result = copy.deepcopy(payload)
    result.pop("SavedAtUtc", None)
    return result


def _finite_number(value) -> bool:
    return (isinstance(value, (int, float)) and not isinstance(value, bool) and
            math.isfinite(value))


def _validate_saved_campaign(payload: dict, player_id: int,
                             system_count: int, context: str) -> None:
    galaxy = payload.get("Galaxy", {})
    if payload.get("FormatVersion") != 17 or not payload.get("SavedAtUtc"):
        raise RuntimeError(f"{context} save is not a timestamped Player17 campaign")
    if galaxy.get("PlayerCivilizationId") != player_id:
        raise RuntimeError(f"{context} save changed the player civilization")
    if len(galaxy.get("Systems", [])) != system_count:
        raise RuntimeError(f"{context} save changed the campaign system count")


def _require_running_advance(payload: dict, source_days: float,
                             context: str) -> None:
    saved_days = payload.get("SimulationDays")
    if (not isinstance(saved_days, (int, float)) or
            not math.isfinite(saved_days) or saved_days <= source_days):
        raise RuntimeError(f"{context} running input did not advance finite simulation time")
    control = payload.get("Control")
    if isinstance(control, dict) and control.get("SimulationDays") != saved_days:
        raise RuntimeError(f"{context} saved inconsistent simulation clocks")


def _launch(folder: Path, work: Path, env: dict[str, str], save: Path,
            capture: Path, mode: str, expected_systems: int,
            load: bool) -> tuple[str, dict]:
    args = [str(folder / "stellar-continuum-native.exe"),
            "--asset-root", str(folder), "--save-path", str(save),
            f"--{mode}-smoke", str(capture)]
    if load:
        args.append("--load")
    result = subprocess.run(args, cwd=work, env=env, capture_output=True,
                            text=True, timeout=90)
    if result.returncode != 0:
        details = (result.stderr or result.stdout).strip()
        raise RuntimeError(
            f"Native {mode} UI replay failed ({result.returncode}): {details}")
    if ("gpu_driver=vulkan" not in result.stdout or
            f"systems={expected_systems} " not in result.stdout):
        raise RuntimeError(
            f"Native {mode} did not confirm renderer/campaign: {result.stdout}")
    if "save=ok " not in result.stdout:
        raise RuntimeError(
            f"Native {mode} did not confirm an actual manual save: {result.stdout}")
    if not _capture_is_bmp(capture):
        raise RuntimeError(f"Native {mode} did not capture its rendered workspace")
    if not save.is_file():
        raise RuntimeError(f"Native {mode} did not save its campaign")
    return result.stdout.strip(), json.loads(save.read_text(encoding="utf-8"))


def _shipyard_marker(stdout: str) -> list[str]:
    token = next((item for item in stdout.split()
                  if item.startswith("shipyard=")), None)
    if token is None:
        raise RuntimeError("Native shipyard did not report its UI result")
    fields = token.removeprefix("shipyard=").split(":")
    if len(fields) not in (5, 6):
        raise RuntimeError(f"Native shipyard reported a malformed UI result: {token}")
    try:
        if int(fields[1]) < 0 or int(fields[2]) < 0:
            raise ValueError
    except ValueError as error:
        raise RuntimeError(
            f"Native shipyard reported a malformed UI result: {token}") from error
    return fields


def _construction_marker(stdout: str) -> list[str]:
    token = next((item for item in stdout.split()
                  if item.startswith("construction=")), None)
    if token is None:
        raise RuntimeError("Native construction did not report its UI result")
    fields = token.removeprefix("construction=").split(":")
    if len(fields) != 5:
        raise RuntimeError(
            f"Native construction reported a malformed UI result: {token}")
    try:
        if int(fields[1]) < 0:
            raise ValueError
    except ValueError as error:
        raise RuntimeError(
            f"Native construction reported a malformed UI result: {token}") from error
    return fields


def _author_shipyard_source(source: dict) -> tuple[dict, int, int, float]:
    authored = copy.deepcopy(source)
    systems = authored.get("Galaxy", {}).get("Systems", [])
    player_id = authored.get("Galaxy", {}).get("PlayerCivilizationId")
    source_days = authored.get("SimulationDays")
    if (authored.get("FormatVersion") != 17 or not systems or
            not isinstance(source_days, (int, float)) or
            not math.isfinite(source_days)):
        raise RuntimeError("Shipyard source row is not a finite Player17 campaign")
    yard, _ = _player_entry(authored, "ShipyardStates")
    yard.update({"NextOrderSequence": 1, "ActiveDesignId": None,
                 "ActiveOrderId": None, "ActiveBuildProgress": 0,
                 "ActiveAuthorizationCredits": 0,
                 "ReservedPopulationMillions": 0,
                 "ReservedPopulationSpeciesId": None,
                 "ReservedPopulationSourceColonyId": None,
                 "QueuedBuilds": []})
    construction, _ = _player_entry(authored, "ConstructionStates")
    construction["CompletedProjectIds"] = ["orbital_shipyard"]
    economy = _player_economy(authored)
    economy["Credits"] = 70
    economy["Industry"] = 200
    _research_core(authored)["Capabilities"] = [
        {"CapabilityId": "spacecraft_construction", "ContextId": None},
        {"CapabilityId": "experimental_interstellar_transit", "ContextId": None},
    ]
    return authored, player_id, len(systems), float(source_days)


def _author_construction_source(source: dict) -> tuple[dict, int, int, float]:
    authored = copy.deepcopy(source)
    systems = authored.get("Galaxy", {}).get("Systems", [])
    player_id = authored.get("Galaxy", {}).get("PlayerCivilizationId")
    source_days = authored.get("SimulationDays")
    if (authored.get("FormatVersion") != 17 or not systems or
            not isinstance(source_days, (int, float)) or
            not math.isfinite(source_days)):
        raise RuntimeError("Construction source row is not a finite Player17 campaign")
    state, _ = _player_entry(authored, "ConstructionStates")
    state.update({"CompletedProjectIds": ["orbital_launch_complex"],
                  "ActiveProjectId": None, "ActiveProjectProgress": 0,
                  "ActiveProjectAuthorizationCredits": 0,
                  "QueuedProjects": []})
    economy = _player_economy(authored)
    economy["Credits"] = 350
    economy["Industry"] = 200
    _research_core(authored)["Capabilities"] = [
        {"CapabilityId": "orbital_industry", "ContextId": None},
    ]
    return authored, player_id, len(systems), float(source_days)


def _shipyard_order(state: dict, order_id: str) -> dict:
    matches = []
    if state.get("ActiveOrderId") == order_id:
        matches.append({"OrderId": order_id,
                        "DesignId": state.get("ActiveDesignId"),
                        "Progress": state.get("ActiveBuildProgress"),
                        "AuthorizationCredits":
                            state.get("ActiveAuthorizationCredits")})
    matches.extend(order for order in state.get("QueuedBuilds", [])
                   if order.get("OrderId") == order_id)
    if len(matches) != 1:
        raise RuntimeError("Shipyard diagnostic does not identify one saved order")
    return matches[0]


def validate_native_shipyard_export(folder: Path, env: dict[str, str],
                                    player17_fixture: Path):
    clean_env = _clean_environment(env)
    with tempfile.TemporaryDirectory(prefix="stellar-native-shipyard-") as temporary:
        work = Path(temporary)
        captures = []
        diagnostics = []

        locked_save = work / "fresh-locked.player17.json"
        locked_capture = work / "shipyard-fresh-locked.bmp"
        stdout, locked = _launch(folder, work, clean_env, locked_save,
                                 locked_capture, "shipyard", 500, False)
        marker = _shipyard_marker(stdout)
        if marker != ["locked", marker[1], "0", "start-not-run",
                      "clock-unproven"]:
            raise RuntimeError(
                f"Fresh campaign did not retain its locked shipyard: {stdout}")
        locked_yard, locked_player = _player_entry(locked, "ShipyardStates")
        _validate_saved_campaign(locked, locked_player, 500, "Fresh shipyard")
        if (locked_yard.get("ActiveOrderId") is not None or
                locked_yard.get("QueuedBuilds")):
            raise RuntimeError("Fresh locked shipyard unexpectedly saved a build order")
        locked_evidence = folder.parent / (folder.name + "-shipyard-fresh-locked.bmp")
        shutil.copy2(locked_capture, locked_evidence)
        captures.append(str(locked_evidence))
        diagnostics.append(stdout)

        authored, player_id, system_count, source_days = _author_shipyard_source(
            _source_row(player17_fixture))
        source_credits = _player_economy(authored)["Credits"]
        save = work / "authored-shipyard.player17.json"
        save.write_text(json.dumps(authored, ensure_ascii=False), encoding="utf-8")
        first_payload = None
        order_id = None
        for replay in (False, True):
            capture = work / ("shipyard-loaded.bmp" if replay else
                              "shipyard-started.bmp")
            stdout, payload = _launch(folder, work, clean_env, save, capture,
                                      "shipyard", system_count, True)
            _validate_saved_campaign(payload, player_id, system_count,
                                     "Authored shipyard")
            marker = _shipyard_marker(stdout)
            if not replay:
                if (len(marker) != 6 or marker[0] != "started" or
                        marker[2] != "1" or marker[4] != "start-running" or
                        marker[5] != "running-to-paused"):
                    raise RuntimeError(
                        f"Shipyard UI did not start while running and prepare cancellation: {stdout}")
                order_id = marker[3]
                state, _ = _player_entry(payload, "ShipyardStates")
                order = _shipyard_order(state, order_id)
                if (order.get("DesignId") != "warp_scout" or
                        not _finite_number(order.get("AuthorizationCredits")) or
                        order["AuthorizationCredits"] != 70 or
                        not _finite_number(order.get("Progress"))):
                    raise RuntimeError("Shipyard player input did not save a canonical build order")
                current_credits = _player_economy(payload).get("Credits")
                if (not _finite_number(current_credits) or
                        current_credits >= source_credits):
                    raise RuntimeError("Shipyard build did not debit its canonical authorization")
                _require_running_advance(payload, source_days, "Shipyard")
                first_payload = payload
            else:
                if (len(marker) != 6 or marker[0] != "orders" or
                        marker[2] != "1" or marker[3] != order_id or
                        marker[4] != "start-not-run" or
                        marker[5] != "running-to-paused"):
                    raise RuntimeError(
                        f"Loaded shipyard did not reopen and prepare the saved cancellation quote: {stdout}")
                if _without_timestamp(payload) != _without_timestamp(first_payload):
                    raise RuntimeError("Shipyard campaign changed during paused reload/recapture")
            evidence = folder.parent / (folder.name +
                ("-shipyard-loaded.bmp" if replay else "-shipyard-started.bmp"))
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(stdout)

        return {"nativeShipyardPlayerInput": True,
                "nativeShipyardCancellationQuotePrepared": True,
                "nativeShipyardOrderReload": True,
                "nativeShipyardFreshLocked": True,
                "shipyardScenarioAuthoredForValidation": True,
                "shipyardCaptures": captures,
                "shipyardDiagnostics": diagnostics}


def _construction_order(state: dict, project_id: str) -> dict:
    matches = []
    if state.get("ActiveProjectId") == project_id:
        matches.append({"ProjectId": project_id,
                        "Progress": state.get("ActiveProjectProgress"),
                        "AuthorizationCredits":
                            state.get("ActiveProjectAuthorizationCredits")})
    matches.extend(order for order in state.get("QueuedProjects", [])
                   if order.get("ProjectId") == project_id)
    if len(matches) != 1:
        raise RuntimeError("Construction diagnostic does not identify one saved order")
    return matches[0]


def validate_native_construction_export(folder: Path, env: dict[str, str],
                                        player17_fixture: Path):
    authored, player_id, system_count, source_days = _author_construction_source(
        _source_row(player17_fixture))
    source_credits = _player_economy(authored)["Credits"]
    clean_env = _clean_environment(env)
    with tempfile.TemporaryDirectory(prefix="stellar-native-construction-") as temporary:
        work = Path(temporary)
        save = work / "authored-construction.player17.json"
        save.write_text(json.dumps(authored, ensure_ascii=False), encoding="utf-8")
        captures = []
        diagnostics = []
        first_payload = None
        project_id = None
        for replay in (False, True):
            capture = work / ("construction-loaded.bmp" if replay else
                              "construction-started.bmp")
            stdout, payload = _launch(folder, work, clean_env, save, capture,
                                      "construction", system_count, True)
            _validate_saved_campaign(payload, player_id, system_count,
                                     "Authored construction")
            marker = _construction_marker(stdout)
            if not replay:
                if (marker[0] != "started" or marker[2] != "orbital_shipyard" or
                        marker[3] != "start-running" or
                        marker[4] != "quote-paused"):
                    raise RuntimeError(
                        f"Construction UI did not start while running and prepare cancellation: {stdout}")
                project_id = marker[2]
                state, _ = _player_entry(payload, "ConstructionStates")
                order = _construction_order(state, project_id)
                if (not _finite_number(order.get("AuthorizationCredits")) or
                        order["AuthorizationCredits"] != 350 or
                        not _finite_number(order.get("Progress"))):
                    raise RuntimeError("Construction player input did not save a canonical order")
                current_credits = _player_economy(payload).get("Credits")
                if (not _finite_number(current_credits) or
                        current_credits >= source_credits):
                    raise RuntimeError(
                        "Construction order did not debit its canonical authorization")
                _require_running_advance(payload, source_days, "Construction")
                first_payload = payload
            else:
                if (marker[0] != "locked" or marker[2:] !=
                        ["none", "start-not-run", "quote-unproven"]):
                    raise RuntimeError(
                        f"Loaded construction accepted unexpected new input: {stdout}")
                state, _ = _player_entry(payload, "ConstructionStates")
                _construction_order(state, project_id)
                if _without_timestamp(payload) != _without_timestamp(first_payload):
                    raise RuntimeError(
                        "Construction campaign changed during paused reload/recapture")
            evidence = folder.parent / (folder.name +
                ("-construction-loaded.bmp" if replay else
                 "-construction-started.bmp"))
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(stdout)

        return {"nativeConstructionPlayerInput": True,
                "nativeConstructionCancellationQuotePrepared": True,
                "nativeConstructionOrderReload": True,
                "constructionScenarioAuthoredForValidation": True,
                "constructionCaptures": captures,
                "constructionDiagnostics": diagnostics}
