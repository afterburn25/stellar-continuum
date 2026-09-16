"""Export proof for native owned-colony input and paused save/reload."""
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


def _finite(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise RuntimeError(f"Native colony reported invalid {label}")
    return float(value)


def _diagnostic(stdout: str, expected_mode: str):
    match = re.search(r"(?:^|\s)colony=(\{[^\n]+\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native colony did not report its owned telemetry and input evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native colony diagnostic is malformed") from error
    if state.get("mode") != expected_mode:
        raise RuntimeError("Native colony reported the wrong smoke mode")
    for key in ("selected", "opened", "back_restored", "pause_retained",
                "speed_retained", "paused", "day_unchanged"):
        if state.get(key) is not True:
            raise RuntimeError(f"Native colony did not prove {key}")
    for key in ("player_id", "system_id", "body_id", "colony_id", "revision", "site_count"):
        if type(state.get(key)) is not int or state[key] < 0:
            raise RuntimeError(f"Native colony reported invalid {key}")
    for key in ("population_millions", "support_ratio", "power_supply", "power_demand",
                "food_reserve_days", "water_reserve_days"):
        _finite(state.get(key), key)
    if state["population_millions"] <= 0 or state["support_ratio"] < 0 or \
            state["power_supply"] < 0 or state["power_demand"] < 0 or \
            state["food_reserve_days"] < 0 or state["water_reserve_days"] < 0:
        raise RuntimeError("Native colony telemetry is outside its physical display range")
    return state


def _bmp(path: Path, stdout: str, width: int, height: int):
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError("Native colony did not capture a BMP frame")
    # The capture is at drawable-pixel size; on high-DPI displays that is a
    # multiple of the requested window size. The smoke reports the actual
    # drawable so the BMP geometry is checked against the real render surface.
    sizes = {(width, height)}
    drawable = re.search(r"(?:^|\s)drawable=(\d+)x(\d+)(?:\s|$)", stdout)
    if drawable:
        sizes.add((int(drawable.group(1)), int(drawable.group(2))))
    declared_size, pixel_offset = struct.unpack_from("<II", data, 2)[0], struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    actual_width, actual_height, planes, bits = struct.unpack_from("<iiHH", data, 18)
    compression = struct.unpack_from("<I", data, 30)[0]
    row_bytes = ((actual_width * bits + 31) // 32) * 4 if actual_width > 0 else 0
    required_pixels = row_bytes * abs(actual_height)
    if (declared_size != len(data) or pixel_offset < 54 or header_size < 40 or
            (actual_width, abs(actual_height)) not in sizes or planes != 1 or
            bits not in (24, 32) or compression not in (0, 3) or
            required_pixels <= 0 or pixel_offset + required_pixels > len(data)):
        raise RuntimeError("Native colony capture has invalid renderer geometry")
    pixels = data[pixel_offset:]
    if not pixels or min(pixels) == max(pixels):
        raise RuntimeError("Native colony capture contains no rendered variation")


def _owned_colony(payload, state):
    galaxy = payload.get("Galaxy", {})
    if galaxy.get("PlayerCivilizationId") != state["player_id"]:
        raise RuntimeError("Native colony diagnostic has the wrong player owner")
    matches = [item for item in galaxy.get("Colonies", [])
               if item.get("Id") == state["colony_id"]]
    if len(matches) != 1:
        raise RuntimeError("Native colony diagnostic does not identify one saved colony")
    colony = matches[0]
    if (colony.get("CivilizationId") != state["player_id"] or
            colony.get("SystemId") != state["system_id"] or
            colony.get("PlanetaryBodyId") != state["body_id"]):
        raise RuntimeError("Native colony diagnostic differs from saved ownership or location")
    bodies = [body for body in galaxy.get("PlanetaryBodies", [])
              if body.get("Id") == state["body_id"] and
              body.get("SystemId") == state["system_id"]]
    systems = [system for system in galaxy.get("Systems", [])
               if system.get("Id") == state["system_id"]]
    if len(bodies) != 1 or len(systems) != 1:
        raise RuntimeError("Native colony body/system identity is absent from the save")
    observers = [row for row in galaxy.get("Knowledge", [])
                 if row.get("CivilizationId") == state["player_id"]]
    if len(observers) != 1:
        raise RuntimeError("Native colony save lacks its unique observer knowledge")
    if state["system_id"] not in observers[0].get("KnownSystemIds", []):
        raise RuntimeError("Native colony diagnostic bypassed the known-system identity gate")
    surveys = [row for row in observers[0].get("SystemSurveys", [])
               if row.get("SystemId") == state["system_id"]]
    if not surveys or max(row.get("Level", 0) for row in surveys) < 2:
        raise RuntimeError("Native colony diagnostic bypassed the known-system gate")
    sites = colony.get("SurfaceBuildings", [])
    if not isinstance(sites, list) or len(sites) != state["site_count"]:
        raise RuntimeError("Native colony diagnostic site count differs from the save")
    population = _finite(colony.get("PopulationMillions"), "saved population")
    if not math.isclose(state["population_millions"], population, rel_tol=1e-9, abs_tol=1e-6):
        raise RuntimeError("Native colony population differs from the save")
    denominator = max(.001, population)
    food = _finite(colony.get("StoredFoodPopulationDaysMillions"), "saved food reserve") / denominator
    water = _finite(colony.get("StoredWaterPopulationDaysMillions"), "saved water reserve") / denominator
    if (not math.isclose(state["food_reserve_days"], food, rel_tol=1e-9, abs_tol=1e-6) or
            not math.isclose(state["water_reserve_days"], water, rel_tol=1e-9, abs_tol=1e-6)):
        raise RuntimeError("Native colony reserve telemetry is not the current saved storage")
    return colony


def _normalized(payload):
    result = copy.deepcopy(payload)
    result.pop("SavedAtUtc", None)
    return result


def validate_native_colony_export(folder: Path, env: dict[str, str]):
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics, payloads, states = [], [], [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-colony-") as temporary:
        work = Path(temporary)
        save = work / "colony.player17.json"
        for width, height, mode, label, load in (
                (1280, 720, "--colony-smoke", "fresh", False),
                (1920, 1080, "--colony-reload-smoke", "paused_reload", True)):
            capture = work / f"colony-{width}x{height}.bmp"
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--width", str(width), "--height", str(height),
                    mode, str(capture)]
            if load:
                args.append("--load")
            result = subprocess.run(args, cwd=work, env=clean, capture_output=True,
                                    text=True, timeout=120)
            if result.returncode != 0:
                details = "\n".join(value for value in (result.stdout, result.stderr) if value)
                raise RuntimeError(f"Native colony {label} failed ({result.returncode}): {details}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
                raise RuntimeError("Native colony did not confirm Vulkan, fresh campaign and save")
            uploads = re.search(r"\bimage_uploads=(\d+)(?:\s|$)", result.stdout)
            if not uploads or int(uploads.group(1)) < 1:
                raise RuntimeError("Native colony did not prove rendered orbital image uploads")
            state = _diagnostic(result.stdout, label)
            _bmp(capture, result.stdout, width, height)
            if not save.is_file():
                raise RuntimeError("Native colony did not write its isolated Player17 save")
            payload = json.loads(save.read_text(encoding="utf-8-sig"))
            day = payload.get("SimulationDays")
            if (payload.get("FormatVersion") != 17 or
                    len(payload.get("Galaxy", {}).get("Systems", [])) != 500 or
                    isinstance(day, bool) or not isinstance(day, (int, float)) or
                    not math.isfinite(day) or day != 0):
                raise RuntimeError("Native colony browsing changed the paused fresh campaign")
            if not payload.get("SavedAtUtc"):
                raise RuntimeError("Native colony did not timestamp its Player17 save")
            _owned_colony(payload, state)
            evidence = folder.parent / f"{folder.name}-colony-{width}x{height}.bmp"
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
            payloads.append(payload)
            states.append(state)
    if _normalized(payloads[0]) != _normalized(payloads[1]):
        raise RuntimeError("Native colony paused reload changed the Player17 payload")
    stable = ("player_id", "system_id", "body_id", "colony_id", "site_count",
              "population_millions", "support_ratio", "power_supply", "power_demand",
              "food_reserve_days", "water_reserve_days")
    if any(states[0][key] != states[1][key] for key in stable):
        raise RuntimeError("Native colony paused reload changed its projected telemetry")
    return {"nativeColonyPlayerInput": True, "nativeColonyPausedReload": True,
            "colonyCaptures": captures, "colonyDiagnostics": diagnostics}
