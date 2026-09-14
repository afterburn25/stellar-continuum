"""Relocated native New Game input, independent slot, and reload proof."""
from __future__ import annotations

import copy
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

_FIELDS = {
    "mode", "entry_opened", "setup_opened", "species_selected",
    "size_selected", "seed_entered", "create_requested",
    "indeterminate_observed", "activated", "saved", "species_id",
    "system_count", "seed", "requested_save_path", "generated_save_path",
    "unique_slot", "setup_screenshot", "loading_screenshot", "statuses",
}
_INTERACTION = (
    "entry_opened", "setup_opened", "species_selected", "size_selected",
    "seed_entered", "create_requested", "indeterminate_observed",
    "activated", "saved", "unique_slot",
)
_SPECIES = "pelagic_high_pressure"
_COUNT = 250
_SEED = "143250"


def _source_payload(fixture: Path) -> dict:
    root = json.loads(fixture.read_text(encoding="utf-8"))
    rows = root.get("Rows")
    if not isinstance(rows, list):
        raise RuntimeError("Player17 fixture has no source-authored rows")
    matches = [row for row in rows if row.get("Name") == "valid-current17"]
    if len(matches) != 1 or not isinstance(matches[0].get("InputJson"), str):
        raise RuntimeError("Player17 fixture has no unique valid-current17 input")
    payload = json.loads(matches[0]["InputJson"])
    if payload.get("FormatVersion") != 17:
        raise RuntimeError("New Game anchor fixture is not current Player17")
    return payload


def _bmp(path: Path, width: int, height: int):
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError("Native New Game did not capture a BMP frame")
    declared = struct.unpack_from("<I", data, 2)[0]
    offset = struct.unpack_from("<I", data, 10)[0]
    header = struct.unpack_from("<I", data, 14)[0]
    actual_width, actual_height, planes, bits = struct.unpack_from("<iiHH", data, 18)
    compression = struct.unpack_from("<I", data, 30)[0]
    row = ((actual_width * bits + 31) // 32) * 4 if actual_width > 0 else 0
    required = row * abs(actual_height)
    if (declared != len(data) or offset < 54 or header < 40 or
            actual_width != width or abs(actual_height) != height or planes != 1 or
            bits not in (24, 32) or compression not in (0, 3) or required <= 0 or
            offset + required > len(data)):
        raise RuntimeError("Native New Game capture has invalid renderer geometry")
    pixels = data[offset:offset + required]
    if not pixels or min(pixels) == max(pixels):
        raise RuntimeError("Native New Game capture contains no rendered variation")


def _diagnostic(stdout: str) -> dict:
    match = re.search(r"(?:^|\s)new_game=(\{[^\n]+\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native New Game did not report startup evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native New Game diagnostic is malformed") from error
    if set(state) != _FIELDS or state.get("mode") != "fresh":
        raise RuntimeError("Native New Game diagnostic has an unexpected schema")
    if any(state.get(key) is not True for key in _INTERACTION):
        raise RuntimeError("Native New Game did not prove every requested input step")
    if (state.get("species_id") != _SPECIES or
            type(state.get("system_count")) is not int or
            state["system_count"] != _COUNT or state.get("seed") != _SEED):
        raise RuntimeError("Native New Game diagnostic changed requested options")
    statuses = state.get("statuses")
    if (not isinstance(statuses, list) or not statuses or
            any(not isinstance(value, str) or not value.strip() for value in statuses)):
        raise RuntimeError("Native New Game did not expose truthful named status")
    return state


def _resolved_reported(value, work: Path, label: str) -> Path:
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"Native New Game reported no {label}")
    path = Path(value)
    if not path.is_absolute():
        path = work / path
    resolved = path.resolve(strict=False)
    try:
        resolved.relative_to(work.resolve())
    except ValueError as error:
        raise RuntimeError(f"Native New Game {label} escaped isolated storage") from error
    return resolved


def _verify_campaign(payload: dict, state: dict):
    galaxy = payload.get("Galaxy", {})
    systems = galaxy.get("Systems")
    metadata = galaxy.get("GenerationMetadata")
    player_id = galaxy.get("PlayerCivilizationId")
    players = [value for value in galaxy.get("Civilizations", [])
               if value.get("Id") == player_id and value.get("IsPlayer") is True]
    day = payload.get("SimulationDays")
    if (payload.get("FormatVersion") != 17 or not isinstance(systems, list) or
            len(systems) != _COUNT or len(players) != 1):
        raise RuntimeError("Generated save is not the requested Player17 campaign")
    if players[0].get("SpeciesId") != _SPECIES:
        raise RuntimeError("Generated player species differs from setup input")
    if galaxy.get("Seed") != int(_SEED):
        raise RuntimeError("Generated campaign seed differs from setup input")
    if (not isinstance(metadata, dict) or metadata.get("EnteredSeed") != _SEED or
            metadata.get("InternalSeed") != int(_SEED) or
            metadata.get("SystemCount") != _COUNT or
            metadata.get("PlayerSpeciesId") != _SPECIES):
        raise RuntimeError("Generated campaign metadata differs from setup input")
    if isinstance(day, bool) or not isinstance(day, (int, float)) or not math.isfinite(day):
        raise RuntimeError("Generated campaign has non-finite simulation time")
    if (state["system_count"] != len(systems) or state["species_id"] !=
            players[0]["SpeciesId"] or state["seed"] != str(galaxy["Seed"])):
        raise RuntimeError("New Game diagnostic does not match its final save")


def _launch(args, cwd: Path, env: dict[str, str], label: str):
    result = subprocess.run(args, cwd=cwd, env=env, capture_output=True,
                            text=True, encoding="utf-8", errors="strict",
                            timeout=180)
    if result.returncode != 0:
        detail = "\n".join(value for value in (result.stdout, result.stderr) if value)
        raise RuntimeError(f"Native New Game {label} failed ({result.returncode}): {detail}")
    if "gpu_driver=vulkan " not in result.stdout or "save=ok" not in result.stdout:
        raise RuntimeError(f"Native New Game {label} lacked Vulkan/save proof")
    systems = re.search(r"(?:^|\s)systems=(\d+)(?:\s|$)", result.stdout)
    if not systems or int(systems.group(1)) != _COUNT:
        raise RuntimeError(f"Native New Game {label} reported the wrong system count")
    return result


def validate_native_new_game_export(folder: Path, env: dict[str, str],
                                    fixture_path: Path):
    anchor_payload = _source_payload(fixture_path)
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics = [], []
    with tempfile.TemporaryDirectory(prefix="stellar-new-game-路径-") as temporary:
        work = Path(temporary)
        cwd = work / "different-cwd-目录"
        cwd.mkdir()
        anchor = work / "已有-campaign.player17.json"
        anchor.write_text(json.dumps(anchor_payload, ensure_ascii=False), encoding="utf-8")
        anchor_bytes = anchor.read_bytes()
        final_capture = work / "new-game-1280x720.bmp"
        setup_capture = work / "new-game-1280x720-setup.bmp"
        loading_capture = work / "new-game-1280x720-loading.bmp"
        exe = str(folder / "stellar-continuum-native.exe")
        fresh = _launch([
            exe, "--asset-root", str(folder), "--save-path", str(anchor),
            "--seed", _SEED, "--width", "1280", "--height", "720",
            "--windowed", "--new-game-smoke", str(final_capture),
        ], cwd, clean, "fresh input")
        state = _diagnostic(fresh.stdout)
        requested = _resolved_reported(state["requested_save_path"], work, "requested path")
        generated = _resolved_reported(state["generated_save_path"], work, "generated path")
        reported_setup = _resolved_reported(state["setup_screenshot"], work, "setup screenshot")
        reported_loading = _resolved_reported(state["loading_screenshot"], work, "loading screenshot")
        if requested != anchor.resolve() or generated == anchor.resolve():
            raise RuntimeError("New Game did not preserve its requested anchor")
        expected_prefix = anchor.name.removesuffix(".player17.json") + "-native-"
        slot_number = (generated.name[len(expected_prefix):-len(".player17.json")]
                       if generated.name.startswith(expected_prefix) and
                       generated.name.endswith(".player17.json") else "")
        if (generated.parent != anchor.parent.resolve() or
                not generated.name.startswith(expected_prefix) or
                not generated.name.endswith(".player17.json") or
                not slot_number.isascii() or not slot_number.isdigit() or
                int(slot_number) < 1):
            raise RuntimeError("New Game generated path is not its isolated native sibling")
        if reported_setup != setup_capture.resolve() or reported_loading != loading_capture.resolve():
            raise RuntimeError("New Game reported arbitrary setup/loading captures")
        if anchor.read_bytes() != anchor_bytes:
            raise RuntimeError("New Game overwrote the pre-existing requested campaign")
        if not generated.is_file():
            raise RuntimeError("New Game did not create its reported independent save")
        for path in (setup_capture, loading_capture, final_capture):
            _bmp(path, 1280, 720)
        generated_payload = json.loads(generated.read_text(encoding="utf-8"))
        _verify_campaign(generated_payload, state)

        reload_capture = work / "new-game-reload-1920x1080.bmp"
        loaded = _launch([
            exe, "--asset-root", str(folder), "--save-path", str(generated),
            "--load", "--width", "1920", "--height", "1080", "--windowed",
            "--smoke", str(reload_capture),
        ], cwd, clean, "paused reload")
        _bmp(reload_capture, 1920, 1080)
        if anchor.read_bytes() != anchor_bytes or not generated.is_file():
            raise RuntimeError("Reload changed the original anchor or removed generated save")
        reloaded_payload = json.loads(generated.read_text(encoding="utf-8"))
        _verify_campaign(reloaded_payload, state)
        before, after = copy.deepcopy(generated_payload), copy.deepcopy(reloaded_payload)
        before.pop("SavedAtUtc", None)
        after.pop("SavedAtUtc", None)
        if before != after:
            raise RuntimeError("Generated campaign changed during paused reload/recapture")

        for path in (setup_capture, loading_capture, final_capture, reload_capture):
            evidence = folder.parent / (folder.name + "-" + path.name)
            shutil.copy2(path, evidence)
            captures.append(str(evidence))
        diagnostics.extend((fresh.stdout.strip(), loaded.stdout.strip()))
        return {
            "nativeNewGamePlayerInput": True,
            "nativeNewGameIndependentSave": True,
            "nativeNewGamePausedReload": True,
            "newGameCaptures": captures,
            "newGameDiagnostics": diagnostics,
        }
