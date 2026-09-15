"""Opt-in running-campaign timing with a separate exact paused reload proof."""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_frame_profile import (
    _metric, _token_object, validate_cold_profile, validate_profile_frames,
    validate_steady_profile,
)
from native_galaxy_runtime import _bmp


def validate_campaign_profile(stdout: str, samples: int) -> dict:
    validate_profile_frames(samples)
    if samples == 0:
        raise RuntimeError("Active campaign profiling requires a nonzero frame count")
    value = _token_object(stdout, "campaign_profile")
    if type(value.get("samples")) is not int or value["samples"] != samples:
        raise RuntimeError("Active campaign profile has the wrong sample count")
    if type(value.get("speed_multiplier")) is not int or value["speed_multiplier"] != 8:
        raise RuntimeError("Active campaign did not use the requested player speed")
    for key in ("mid_save_completed", "advanced_after_mid_save", "speed_input",
                "resume_input", "pause_input", "final_saved"):
        if value.get(key) is not True:
            raise RuntimeError(f"Active campaign did not prove {key}")
    before, middle, completed, after = (_metric(value.get(key), key) for key in
        ("before_days", "mid_save_day", "mid_save_completed_day", "after_days"))
    if not before < middle <= completed < after:
        raise RuntimeError("Active campaign did not advance across its manual save")
    return value


def _saved_payload(path: Path, expected_days: float) -> dict:
    if not path.is_file():
        raise RuntimeError("Active campaign did not write its isolated Player17 save")
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (ValueError, UnicodeError) as error:
        raise RuntimeError("Active campaign saved malformed Player17 data") from error
    if not isinstance(payload, dict) or payload.get("FormatVersion") != 17:
        raise RuntimeError("Active campaign did not save Player17")
    galaxy = payload.get("Galaxy")
    systems = galaxy.get("Systems") if isinstance(galaxy, dict) else None
    if not isinstance(systems, list) or len(systems) != 500:
        raise RuntimeError("Active campaign changed the 500-system catalog")
    days = _metric(payload.get("SimulationDays"), "saved SimulationDays")
    if not math.isclose(days, expected_days, rel_tol=0., abs_tol=1e-9):
        raise RuntimeError("Active campaign saved a stale or changed simulation day")
    payload.pop("SavedAtUtc", None)
    return payload


def validate_native_campaign_profile(folder: Path, env: dict[str, str], *,
                                     profile_frames: int = 600) -> dict:
    """Measure fresh/running reloads and verify a paused round trip after each.

    This establishes a baseline for the canonical 500-system early campaign. It
    does not manufacture a populated late-game fixture or certify every GPU.
    """
    validate_profile_frames(profile_frames)
    if profile_frames == 0:
        raise RuntimeError("Active campaign profiling requires a nonzero frame count")
    folder = folder.resolve()
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    profiles, cold_profiles, states, captures, diagnostics = [], [], [], [], []
    before_days = 0.
    with tempfile.TemporaryDirectory(prefix="stellar-active-campaign-") as temporary:
        work = Path(temporary)
        save = work / "active.player17.json"
        for width, height, reload in ((1280, 720, False), (1920, 1080, True)):
            capture = work / f"active-{width}x{height}.bmp"
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--width", str(width), "--height", str(height),
                    "--campaign-profile", str(capture), "--profile-frames", str(profile_frames)]
            if reload:
                args.append("--load")
            result = subprocess.run(args, cwd=work, env=clean_env, capture_output=True,
                                    text=True, timeout=120 + profile_frames // 15)
            if result.returncode != 0:
                raise RuntimeError(f"Active campaign failed ({result.returncode}): {result.stderr}")
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
                raise RuntimeError("Active campaign did not confirm Vulkan, catalog and save")
            images = re.search(r"\bimage_uploads=(\d+)(?:\s|$)", result.stdout)
            if not images or int(images.group(1)) < 3:
                raise RuntimeError("Active campaign did not upload its map artwork")
            state = validate_campaign_profile(result.stdout, profile_frames)
            if not math.isclose(state["before_days"], before_days, rel_tol=0., abs_tol=1e-9):
                raise RuntimeError("Active campaign did not resume from the expected day")
            profiles.append(validate_steady_profile(result.stdout, profile_frames))
            cold_profiles.append(validate_cold_profile(result.stdout))
            _bmp(capture, width, height)
            baseline = _saved_payload(save, state["after_days"])
            before_days = state["after_days"]
            evidence = folder.parent / f"{folder.name}-active-{width}x{height}.bmp"
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            states.append(state)
            diagnostics.append(result.stdout.strip())

            # Restore/resave an independent copy through the existing paused
            # menu smoke. This leaves the next active run's source untouched.
            paused_save = work / f"paused-{width}.player17.json"
            shutil.copy2(save, paused_save)
            paused_capture = work / f"paused-{width}x{height}.bmp"
            paused_args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                           "--save-path", str(paused_save), "--width", str(width), "--height", str(height),
                           "--load", "--smoke", str(paused_capture)]
            paused = subprocess.run(paused_args, cwd=work, env=clean_env, capture_output=True,
                                    text=True, timeout=120)
            if paused.returncode != 0 or any(token not in paused.stdout for token in
                    ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
                raise RuntimeError(f"Active campaign paused reload failed: {paused.stderr}")
            _bmp(paused_capture, width, height)
            if _saved_payload(paused_save, before_days) != baseline:
                raise RuntimeError("Active campaign paused reload changed its Player17 payload")
            if _saved_payload(save, before_days) != baseline:
                raise RuntimeError("Paused validation modified the next active run's source")
            diagnostics.append(paused.stdout.strip())
    return {"nativeActiveCampaignProfile": True, "nativeActiveCampaignPausedReload": True,
            "campaignStates": states, "campaignProfiles": profiles,
            "campaignColdProfiles": cold_profiles, "campaignCaptures": captures,
            "campaignDiagnostics": diagnostics}
