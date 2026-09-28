"""Opt-in running-campaign timing with a separate exact paused reload proof."""
from __future__ import annotations

import hashlib
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


def _saved_payload(path: Path, expected_days: float | None = None) -> dict:
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
    if expected_days is not None and not math.isclose(days, expected_days, rel_tol=0., abs_tol=1e-9):
        raise RuntimeError("Active campaign saved a stale or changed simulation day")
    payload.pop("SavedAtUtc", None)
    return payload


def _fleet_workload(payload: dict) -> tuple[dict, dict]:
    """Summarize actual owned fleets; never create ships or issue orders here."""
    galaxy = payload["Galaxy"]
    player = galaxy.get("PlayerCivilizationId")
    fleets, colonies = galaxy.get("Fleets", []), galaxy.get("Colonies", [])
    if not isinstance(fleets, list) or not isinstance(colonies, list):
        raise RuntimeError("Campaign workload has malformed fleet/colony collections")
    own, routed = {}, []
    for fleet in fleets:
        if not isinstance(fleet, dict):
            raise RuntimeError("Campaign workload has a malformed fleet")
        if fleet.get("CivilizationId") != player or not fleet.get("IsActive", True):
            continue
        identity = fleet.get("Id")
        if type(identity) is not int or identity in own:
            raise RuntimeError("Campaign workload has missing or duplicate owned fleet IDs")
        own[identity] = fleet
        if fleet.get("TransitPhase", 0) != 0:
            routed.append(identity)
    if any(not isinstance(colony, dict) for colony in colonies):
        raise RuntimeError("Campaign workload has a malformed colony")
    return {"owned_active_fleets": len(own), "owned_transiting_fleets": len(routed),
            "transiting_fleet_ids": sorted(routed), "total_colonies": len(colonies),
            "owned_colonies": sum(c.get("CivilizationId") == player for c in colonies)}, own


def _fleet_progress(before: dict, after: dict, minimum: int) -> dict:
    start, before_fleets = _fleet_workload(before)
    finish, after_fleets = _fleet_workload(after)
    if start["owned_transiting_fleets"] < minimum:
        raise RuntimeError("Campaign lacks the requested active fleet workload")
    motion = ("X", "Y", "CurrentSystemId", "TransitPhase", "TransitProgress",
              "LocalTransitPositionX", "LocalTransitPositionY")
    moved = [identity for identity in start["transiting_fleet_ids"]
             if identity in after_fleets and any(before_fleets[identity].get(key) !=
                 after_fleets[identity].get(key) for key in motion)]
    if len(moved) < minimum:
        raise RuntimeError("Campaign fleets did not demonstrate the requested transit progress")
    return {"before": start, "after": finish, "moved_fleet_ids": sorted(moved)}


def validate_native_campaign_profile(folder: Path, env: dict[str, str], *,
                                     profile_frames: int = 600,
                                     initial_save: Path | None = None,
                                     minimum_moving_fleets: int = 0) -> dict:
    """Measure fresh/running reloads and verify a paused round trip after each.

    An initial save is copied into isolation, never opened by the game directly.
    Workload checks prove actual fleet progress, not just passing simulation days.
    """
    validate_profile_frames(profile_frames)
    if profile_frames == 0:
        raise RuntimeError("Active campaign profiling requires a nonzero frame count")
    if type(minimum_moving_fleets) is not int or not 0 <= minimum_moving_fleets <= 2500:
        raise RuntimeError("Minimum moving fleets must be an integer from 0 to 2500")
    if minimum_moving_fleets and initial_save is None:
        raise RuntimeError("An active fleet workload requires an explicit initial save")
    original_bytes, initial_payload = None, None
    if initial_save is not None:
        initial_save = initial_save.resolve()
        initial_payload = _saved_payload(initial_save)
        original_bytes = initial_save.read_bytes()
        start, _ = _fleet_workload(initial_payload)
        if start["owned_transiting_fleets"] < minimum_moving_fleets:
            raise RuntimeError("Initial save lacks the requested active fleet workload")
    folder = folder.resolve()
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    profiles, cold_profiles, states, captures, diagnostics, workloads = [], [], [], [], [], []
    before_days = initial_payload["SimulationDays"] if initial_payload is not None else 0.
    with tempfile.TemporaryDirectory(prefix="stellar-active-campaign-") as temporary:
        work = Path(temporary)
        save = work / "active.player17.json"
        if original_bytes is not None:
            save.write_bytes(original_bytes)
        for width, height, reload in ((1280, 720, False), (1920, 1080, True)):
            starting_payload = _saved_payload(save, before_days) if save.is_file() else None
            if starting_payload is not None and minimum_moving_fleets:
                if _fleet_workload(starting_payload)[0]["owned_transiting_fleets"] < minimum_moving_fleets:
                    raise RuntimeError("Reload lacks the requested active fleet workload")
            capture = work / f"active-{width}x{height}.bmp"
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--width", str(width), "--height", str(height),
                    "--campaign-profile", str(capture), "--profile-frames", str(profile_frames)]
            if reload or initial_payload is not None:
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
            if starting_payload is not None:
                workloads.append(_fleet_progress(starting_payload, baseline, minimum_moving_fleets))
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
    if initial_save is not None and initial_save.read_bytes() != original_bytes:
        raise RuntimeError("Campaign profile source save changed during validation")
    return {"nativeActiveCampaignProfile": True, "nativeActiveCampaignPausedReload": True,
            "campaignStates": states, "campaignProfiles": profiles,
            "campaignColdProfiles": cold_profiles, "campaignCaptures": captures,
            "campaignDiagnostics": diagnostics, "campaignWorkloads": workloads,
            "campaignInitialSaveSha256": hashlib.sha256(original_bytes).hexdigest()
                if original_bytes is not None else None}
