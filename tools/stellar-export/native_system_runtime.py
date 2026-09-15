"""Relocated native orbital input, imagery and paused save/reload proof."""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_frame_profile import validate_profile_frames, validate_steady_profile


def _system_diagnostic(stdout):
    state = re.search(r"\bsystem=id=(\d+):body=(\d+):visible=(\d+):scale=([^:\s]+)"
                      r":entry=1:hit=1:pan=1:zoom=1:reset=1:back=1"
                      r":pause_retained=1:speed_retained=1:gesture_cleared=1"
                      r":paused=1:day_unchanged=1(?:\s|$)", stdout)
    uploads = re.search(r"\bimage_uploads=(\d+)(?:\s|$)", stdout)
    if not state or not uploads:
        raise RuntimeError("Native system did not confirm orbital input and image rendering")
    system_id, body_id, visible = map(int, state.groups()[:3])
    try:
        scale = float(state.group(4))
    except ValueError as error:
        raise RuntimeError("Native system reported invalid camera scale") from error
    # The fresh human home view is canonical Sol (0), selecting Earth (3).
    if (system_id != 0 or body_id != 3 or visible < 1 or
            not math.isfinite(scale) or scale <= 0 or int(uploads.group(1)) < 1):
        raise RuntimeError("Native system did not render and select the expected known orbital view")


def validate_native_system_export(folder: Path, env: dict[str, str], *, profile_frames: int = 0):
    validate_profile_frames(profile_frames)
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics, profiles = [], [], []
    baseline = None
    with tempfile.TemporaryDirectory(prefix="stellar-native-system-") as temporary:
        work = Path(temporary)
        save = work / "system.player17.json"
        for width, height, reload in ((1280, 720, False), (1920, 1080, True)):
            capture = work / f"system-{width}x{height}.bmp"
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--width", str(width), "--height", str(height),
                    "--system-smoke", str(capture)]
            if profile_frames:
                args.extend(("--profile-frames", str(profile_frames)))
            if reload:
                args.append("--load")
            result = subprocess.run(args, cwd=work, env=clean_env, capture_output=True,
                                    text=True, timeout=120 + (profile_frames // 30 if profile_frames else 0))
            if result.returncode != 0:
                raise RuntimeError(f"Native orbital input failed ({result.returncode}): {result.stderr}")
            if any(token not in result.stdout for token in ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
                raise RuntimeError("Native orbital view did not confirm Vulkan, fresh campaign and save")
            _system_diagnostic(result.stdout)
            if profile_frames:
                profiles.append(validate_steady_profile(result.stdout, profile_frames))
            if not capture.is_file() or capture.stat().st_size < 54 or capture.read_bytes()[:2] != b"BM":
                raise RuntimeError("Native orbital view did not capture the rendered frame")
            if not save.is_file():
                raise RuntimeError("Native orbital view did not write its isolated Player17 save")
            payload = json.loads(save.read_text(encoding="utf-8"))
            days = payload.get("SimulationDays")
            if (payload.get("FormatVersion") != 17 or
                    len(payload.get("Galaxy", {}).get("Systems", [])) != 500 or
                    isinstance(days, bool) or not isinstance(days, (int, float)) or
                    not math.isfinite(days) or days != 0):
                raise RuntimeError("Native orbital browsing changed the fresh paused campaign")
            payload.pop("SavedAtUtc", None)
            if baseline is not None and payload != baseline:
                raise RuntimeError("Native orbital reload changed the paused Player17 payload")
            baseline = payload
            evidence = folder.parent / f"{folder.name}-system-{width}x{height}.bmp"
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
    result = {"nativeSystemPlayerInput": True, "nativeSystemBodyImages": True,
            "nativeSystemPausedReload": True, "systemCaptures": captures,
            "systemDiagnostics": diagnostics}
    if profile_frames:
        result["systemProfiles"] = profiles
    return result
