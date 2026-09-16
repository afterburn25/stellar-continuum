"""Actual native overview, regional zoom and galaxy-free system export proof."""
from __future__ import annotations

import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

from native_frame_profile import validate_cold_profile, validate_profile_frames, validate_steady_profile


def _finite_positive(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or \
            not math.isfinite(value) or value <= 0:
        raise RuntimeError(f"Native galaxy reported invalid {label}")
    return float(value)


_LABEL_FIELDS = ("candidates", "measured", "placed", "selected_requested",
                 "selected_placed", "label_overlaps", "obstacle_overlaps",
                 "hud_overlaps", "star_overlaps", "outside_viewport")


def _validate_labels(view, label):
    labels = view.get("labels")
    if not isinstance(labels, dict):
        raise RuntimeError(f"Native {label} diagnostic lacks labels")
    if set(labels) != set(_LABEL_FIELDS):
        raise RuntimeError(f"Native {label} labels have an unexpected field set")
    for field in _LABEL_FIELDS:
        value = labels.get(field)
        if type(value) is not int or value < 0:
            raise RuntimeError(f"Native {label} reported invalid labels.{field}")
    if labels["measured"] > 128 or labels["placed"] > labels["measured"]:
        raise RuntimeError(f"Native {label} label placement exceeds its budget")
    if labels["measured"] > labels["candidates"]:
        raise RuntimeError(f"Native {label} measured more labels than candidates")
    if labels["selected_requested"] > labels["candidates"] or \
            labels["selected_placed"] > labels["selected_requested"] or \
            labels["selected_placed"] > labels["placed"]:
        raise RuntimeError(f"Native {label} selected label counts are inconsistent")
    for field in ("label_overlaps", "obstacle_overlaps", "hud_overlaps",
                  "star_overlaps", "outside_viewport"):
        if labels[field] != 0:
            raise RuntimeError(f"Native {label} labels have forbidden {field}")
    if label == "regional" and labels["placed"] < 1:
        raise RuntimeError("Native regional zoom placed no labels")


def _diagnostic(stdout: str, expected_mode: str):
    match = re.search(r"(?:^|\s)galaxy_art=(\{[^\n]+\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native galaxy did not report its render and input evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native galaxy diagnostic is malformed") from error
    if state.get("mode") != expected_mode:
        raise RuntimeError("Native galaxy reported the wrong smoke mode")
    for key in ("wheel_input", "system_entry", "paused", "day_unchanged"):
        if state.get(key) is not True:
            raise RuntimeError(f"Native galaxy did not prove {key}")
    fitted = _finite_positive(state.get("fitted_scale"), "fitted scale")
    regional = _finite_positive(state.get("regional_scale"), "regional scale")
    if regional < fitted * 5.:
        raise RuntimeError("Native wheel input did not reach the regional presentation")
    if state.get("decoded_sources") != 3:
        raise RuntimeError("Native galaxy did not cache the exact three approved source images")

    overview = state.get("overview")
    regional_view = state.get("regional")
    system = state.get("system")
    if not all(isinstance(item, dict) for item in (overview, regional_view, system)):
        raise RuntimeError("Native galaxy diagnostic lacks view render state")
    expected_overview = {"deep_field": 1, "galaxy_layer": 1,
                         "regional_nebula": 0, "regional_points": 0}
    expected_regional = {"deep_field": 0, "galaxy_layer": 0,
                         "regional_nebula": 1, "regional_points": 356}
    for key, value in expected_overview.items():
        if overview.get(key) != value:
            raise RuntimeError(f"Native fitted overview has invalid {key}")
    for key, value in expected_regional.items():
        if regional_view.get(key) != value:
            raise RuntimeError(f"Native regional view has invalid {key}")
    if system.get("background_images") != 0 or system.get("regional_points") != 0:
        raise RuntimeError("Galaxy scenery leaked into the system view")
    for view, label, exact_total in ((overview, "overview", 500),
                                     (regional_view, "regional", None)):
        _validate_labels(view, label)
        for key in ("catalog_markers", "known_markers", "unknown_markers",
                    "revealed_unknown_labels"):
            if type(view.get(key)) is not int or view[key] < 0:
                raise RuntimeError(f"Native {label} reported invalid {key}")
        if view["known_markers"] + view["unknown_markers"] != view["catalog_markers"]:
            raise RuntimeError(f"Native {label} marker knowledge partition is inconsistent")
        if exact_total is not None and view["catalog_markers"] != exact_total:
            raise RuntimeError("Native fitted overview omitted canonical catalog markers")
        if label == "regional" and view["catalog_markers"] < 1:
            raise RuntimeError("Native regional zoom contains no canonical marker")
        if view["revealed_unknown_labels"] != 0:
            raise RuntimeError("Native galaxy revealed an unknown system label")
    return state


def _bmp(path: Path, width: int, height: int):
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError("Native galaxy did not capture a BMP frame")
    declared_size, pixel_offset = struct.unpack_from("<II", data, 2)[0], struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    actual_width, actual_height, planes, bits = struct.unpack_from("<iiHH", data, 18)
    compression = struct.unpack_from("<I", data, 30)[0]
    row_bytes = ((actual_width * bits + 31) // 32) * 4 if actual_width > 0 else 0
    required = row_bytes * abs(actual_height)
    if (declared_size != len(data) or pixel_offset < 54 or header_size < 40 or
            actual_width != width or abs(actual_height) != height or planes != 1 or
            bits not in (24, 32) or compression not in (0, 3) or required <= 0 or
            pixel_offset + required > len(data)):
        raise RuntimeError("Native galaxy capture has invalid renderer geometry")
    pixels = data[pixel_offset:pixel_offset + required]
    if min(pixels) == max(pixels):
        raise RuntimeError("Native galaxy capture contains no rendered variation")
    return pixels


def _normalized(payload):
    result = copy.deepcopy(payload)
    result.pop("SavedAtUtc", None)
    return result


def _knowledge(payload, state):
    galaxy = payload.get("Galaxy", {})
    player = galaxy.get("PlayerCivilizationId")
    rows = [row for row in galaxy.get("Knowledge", [])
            if row.get("CivilizationId") == player]
    if len(rows) != 1 or not isinstance(rows[0].get("KnownSystemIds"), list):
        raise RuntimeError("Native galaxy save lacks unique player knowledge")
    known = set(rows[0]["KnownSystemIds"])
    if len(known) != state["overview"]["known_markers"]:
        raise RuntimeError("Native overview marker knowledge differs from the saved observer")
    if state["regional"]["known_markers"] > len(known):
        raise RuntimeError("Native regional view invented known systems")


def validate_native_galaxy_export(folder: Path, env: dict[str, str], *, profile_frames: int = 0):
    validate_profile_frames(profile_frames)
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics, profiles, cold_profiles = [], [], [], []
    baseline = None
    with tempfile.TemporaryDirectory(prefix="stellar-native-galaxy-") as temporary:
        work = Path(temporary)
        save = work / "galaxy.player17.json"
        for width, height, reload in ((1280, 720, False), (1920, 1080, True)):
            overview = work / f"galaxy-{width}x{height}.bmp"
            regional = overview.with_name(overview.stem + "-regional.bmp")
            system = overview.with_name(overview.stem + "-system.bmp")
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--width", str(width), "--height", str(height),
                    "--galaxy-art-smoke", str(overview)]
            if profile_frames:
                args.extend(("--profile-frames", str(profile_frames)))
            if reload:
                args.append("--load")
            result = subprocess.run(args, cwd=work, env=clean_env, capture_output=True,
                                    text=True, encoding="utf-8", errors="strict",
                                    timeout=120 + (profile_frames // 30 if profile_frames else 0))
            if result.returncode != 0:
                raise RuntimeError(f"Native galaxy smoke failed ({result.returncode}):\n{result.stdout}\n{result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
                raise RuntimeError("Native galaxy did not confirm Vulkan, fresh catalog and save")
            uploads = re.search(r"\bimage_uploads=(\d+)(?:\s|$)", result.stdout)
            if not uploads or not 4 <= int(uploads.group(1)) <= 128:
                raise RuntimeError("Native galaxy image uploads are absent or unbounded")
            state = _diagnostic(result.stdout, "paused_reload" if reload else "fresh")
            if profile_frames:
                profiles.append(validate_steady_profile(result.stdout, profile_frames))
                cold_profiles.append(validate_cold_profile(result.stdout))
            pixels = [_bmp(path, width, height) for path in (overview, regional, system)]
            if len({hashlib.sha256(value).digest() for value in pixels}) != 3:
                raise RuntimeError("Native galaxy smoke captures do not show three distinct views")
            if not save.is_file():
                raise RuntimeError("Native galaxy smoke did not write its isolated Player17 save")
            payload = json.loads(save.read_text(encoding="utf-8"))
            days = payload.get("SimulationDays")
            if (payload.get("FormatVersion") != 17 or
                    len(payload.get("Galaxy", {}).get("Systems", [])) != 500 or
                    isinstance(days, bool) or not isinstance(days, (int, float)) or
                    not math.isfinite(days) or days != 0):
                raise RuntimeError("Native galaxy browsing changed the paused fresh campaign")
            _knowledge(payload, state)
            normalized = _normalized(payload)
            if baseline is not None and normalized != baseline:
                raise RuntimeError("Native galaxy reload changed the paused Player17 payload")
            baseline = normalized
            for path in (overview, regional, system):
                evidence = folder.parent / f"{folder.name}-{path.name}"
                shutil.copy2(path, evidence)
                captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
    result = {"nativeGalaxyFittedArtwork": True,
            "nativeGalaxyRegionalTransition": True,
            "nativeGalaxySystemIsolation": True,
            "nativeGalaxyPausedReload": True,
            "galaxyCaptures": captures, "galaxyDiagnostics": diagnostics}
    if profile_frames:
        result["galaxyProfiles"] = profiles
        result["galaxyColdProfiles"] = cold_profiles
    return result
