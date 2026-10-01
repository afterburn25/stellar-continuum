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

from native_audio_runtime import parse_native_audio_check
from native_audio_settings_runtime import (parse_native_audio_settings_check,
                                           verify_native_audio_settings_file)
from native_bmp import validate_bmp

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


def _bmp(path: Path, width: int, height: int, stdout: str | None = None):
    return validate_bmp(path, width, height, "New Game", stdout=stdout)


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


def _video_diagnostic(stdout: str, location: str) -> dict:
    rows = re.findall(r"^video_settings_check=(.*)$", stdout, flags=re.MULTILINE)
    if len(rows) != 1:
        raise RuntimeError("Expected exactly one video settings diagnostic")

    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    try:
        report = json.loads(rows[0], object_pairs_hook=unique_object)
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Video settings diagnostic was not valid unique-key JSON") from error
    flags = ("opened", "choice_rows", "previewed", "normal_capture",
             "confirm_capture", "escape_reverted", "kept", "restored")
    expected_keys = {"location", *flags}
    if (not isinstance(report, dict) or set(report) != expected_keys or
            report.get("location") != location):
        raise RuntimeError("Video settings diagnostic did not report the expected fields and location")
    if any(report[name] is not True for name in flags):
        raise RuntimeError("Video settings diagnostic did not complete the visible preview/keep/revert flow")
    return report


def _general_diagnostic(stdout: str, location: str) -> dict:
    rows = re.findall(r"^general_settings_check=(.*)$", stdout, flags=re.MULTILINE)
    if len(rows) != 1:
        raise RuntimeError("Expected exactly one general settings diagnostic")

    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    try:
        report = json.loads(rows[0], object_pairs_hook=unique_object)
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("General settings diagnostic was not valid unique-key JSON") from error
    flags = ("opened", "capture", "text_scale", "cancel_restored",
             "saved", "restored")
    expected_keys = {"location", *flags}
    if (not isinstance(report, dict) or set(report) != expected_keys or
            report.get("location") != location):
        raise RuntimeError("General settings diagnostic did not report the expected fields and location")
    if any(report[name] is not True for name in flags):
        raise RuntimeError("General settings diagnostic did not complete the text-scale exercise")
    return report


def _voice_settings_diagnostic(stdout: str, location: str) -> dict:
    rows = re.findall(r"^voice_settings_check=(.*)$", stdout, flags=re.MULTILINE)
    if len(rows) != 1:
        raise RuntimeError("Expected exactly one voice settings diagnostic")

    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    try:
        report = json.loads(rows[0], object_pairs_hook=unique_object)
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Voice settings diagnostic was not valid unique-key JSON") from error
    flags = ("opened", "previewed", "replay", "stop", "defaults",
             "cancel_restored", "saved", "restored")
    expected_keys = {"location", *flags}
    if (not isinstance(report, dict) or set(report) != expected_keys or
            report.get("location") != location):
        raise RuntimeError("Voice settings diagnostic did not report the expected fields and location")
    if any(report[name] is not True for name in flags):
        raise RuntimeError("Voice settings diagnostic did not complete the full control matrix")
    return report


def _controls_diagnostic(stdout: str, location: str) -> dict:
    rows = re.findall(r"^controls_settings_check=(.*)$", stdout, flags=re.MULTILINE)
    if len(rows) != 1:
        raise RuntimeError("Expected exactly one controls settings diagnostic")

    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    try:
        report = json.loads(rows[0], object_pairs_hook=unique_object)
    except (TypeError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError("Controls settings diagnostic was not valid unique-key JSON") from error
    if location == "startup":
        # Pre-campaign the hub has no input mapper — the Controls category is
        # a static help card whose proof is the open/render/back-out shape.
        if (not isinstance(report, dict) or
                set(report) != {"location", "help_card"} or
                report.get("location") != location or
                report.get("help_card") is not True):
            raise RuntimeError("Controls startup diagnostic did not report the help-card proof")
        return report
    required = ("opened", "capture_cancel", "rebound", "restored")
    conditional = ("scrolled", "axis_captured", "pinned", "noticed",
                   "triggers", "stole", "file_preexisted")
    expected_keys = {"location", *required, *conditional}
    if (not isinstance(report, dict) or set(report) != expected_keys or
            report.get("location") != location):
        raise RuntimeError("Controls settings diagnostic did not report the expected fields and location")
    if any(report[name] is not True for name in required):
        raise RuntimeError("Controls settings diagnostic did not complete the rebind matrix")
    if any(type(report[name]) is not bool for name in conditional):
        raise RuntimeError("Controls settings diagnostic reported non-boolean evidence flags")
    return report


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
                                    fixture_path: Path, *, audio_check=False,
                                    audio_settings_check=False, video_settings_check=False,
                                    general_settings_check=False, voice_settings_check=False,
                                    controls_settings_check=False):
    if audio_settings_check and not audio_check:
        raise RuntimeError("Native audio settings check requires the audio check")
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
        menu_capture = work / "new-game-1280x720-menu.bmp"
        modes_capture = work / "new-game-1280x720-modes.bmp"
        setup_capture = work / "new-game-1280x720-setup.bmp"
        loading_capture = work / "new-game-1280x720-loading.bmp"
        fresh_settings_capture = work / "new-game-1280x720-audio-settings.bmp"
        settings_path = anchor.parent / "audio-settings.json"
        exe = str(folder / "stellar-continuum-native.exe")
        fresh_args = [
            exe, "--asset-root", str(folder), "--save-path", str(anchor),
            "--seed", _SEED, "--width", "1280", "--height", "720",
            "--windowed", "--new-game-smoke", str(final_capture),
        ]
        if audio_check:
            fresh_args.append("--audio-check")
        if audio_settings_check:
            fresh_args.append("--audio-settings-check")
        if video_settings_check: fresh_args.append("--video-settings-check")
        if general_settings_check: fresh_args.append("--general-settings-check")
        if voice_settings_check: fresh_args.append("--voice-settings-check")
        if controls_settings_check: fresh_args.append("--controls-settings-check")
        fresh = _launch(fresh_args, cwd, clean, "fresh input")
        fresh_audio = parse_native_audio_check(fresh.stdout, fresh=True) if audio_check else None
        fresh_audio_settings = (parse_native_audio_settings_check(fresh.stdout, fresh=True)
                                if audio_settings_check else None)
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
        for path in (menu_capture, modes_capture, setup_capture, loading_capture,
                     final_capture):
            _bmp(path, 1280, 720, fresh.stdout)
        settings_bytes = None
        if audio_settings_check:
            _bmp(fresh_settings_capture, 1280, 720, fresh.stdout)
            settings_bytes = verify_native_audio_settings_file(settings_path)
        video_bytes = (anchor.parent / "video-settings.json").read_bytes() if video_settings_check else None
        general_path = anchor.parent / "general-settings.json"
        general_bytes = general_path.read_bytes() if general_settings_check and general_path.is_file() else None
        voice_path = anchor.parent / "voice-settings.json"
        voice_reference = (json.loads(voice_path.read_text(encoding="utf-8"))
                           if voice_settings_check and voice_path.is_file() else None)
        controls_path = anchor.parent / "galaxy-controls.json"
        controls_bytes = controls_path.read_bytes() if controls_settings_check and controls_path.is_file() else None
        generated_payload = json.loads(generated.read_text(encoding="utf-8"))
        _verify_campaign(generated_payload, state)

        reload_capture = work / "new-game-reload-1920x1080.bmp"
        reload_settings_capture = work / "new-game-reload-1920x1080-audio-settings.bmp"
        reload_args = [
            exe, "--asset-root", str(folder), "--save-path", str(generated),
            "--load", "--width", "1920", "--height", "1080", "--windowed",
            "--smoke", str(reload_capture),
        ]
        if audio_check:
            reload_args.append("--audio-check")
        if audio_settings_check:
            reload_args.append("--audio-settings-check")
        if video_settings_check: reload_args.append("--video-settings-check")
        if general_settings_check: reload_args.append("--general-settings-check")
        if voice_settings_check: reload_args.append("--voice-settings-check")
        if controls_settings_check: reload_args.append("--controls-settings-check")
        loaded = _launch(reload_args, cwd, clean, "paused reload")
        reload_audio = parse_native_audio_check(loaded.stdout, fresh=False) if audio_check else None
        reload_audio_settings = (parse_native_audio_settings_check(loaded.stdout, fresh=False)
                                 if audio_settings_check else None)
        _bmp(reload_capture, 1920, 1080, loaded.stdout)
        if audio_settings_check:
            _bmp(reload_settings_capture, 1920, 1080, loaded.stdout)
            if verify_native_audio_settings_file(settings_path) != settings_bytes:
                raise RuntimeError("Native audio settings changed during paused reload")
        if anchor.read_bytes() != anchor_bytes or not generated.is_file():
            raise RuntimeError("Reload changed the original anchor or removed generated save")
        reloaded_payload = json.loads(generated.read_text(encoding="utf-8"))
        _verify_campaign(reloaded_payload, state)
        before, after = copy.deepcopy(generated_payload), copy.deepcopy(reloaded_payload)
        before.pop("SavedAtUtc", None)
        after.pop("SavedAtUtc", None)
        if before != after:
            raise RuntimeError("Generated campaign changed during paused reload/recapture")

        video_checks = {}
        if video_settings_check:
            if (anchor.parent / "video-settings.json").read_bytes() != video_bytes:
                raise RuntimeError("Video preferences changed across cold campaign reload")
            for location, process in (("startup", fresh), ("pause", loaded)):
                video_checks[location] = _video_diagnostic(process.stdout, location)
        general_checks = {}
        if general_settings_check:
            # The check restores byte-exact when a file pre-existed; a file
            # first written by the check itself is equivalently restored
            # when its prefs match saved (proven by restored=true).
            if general_bytes is not None and (
                    not general_path.is_file() or
                    general_path.read_bytes() != general_bytes):
                raise RuntimeError("General preferences changed across the settings checks")
            for location, process in (("startup", fresh), ("pause", loaded)):
                general_checks[location] = _general_diagnostic(process.stdout, location)
        voice_checks = {}
        if voice_settings_check:
            # Slider clicks persist the pointer's x-fraction, and the two legs
            # run at different resolutions — floats compare within the check's
            # own .01 tolerance while toggles/enums stay exact.
            if voice_reference is not None:
                if not voice_path.is_file():
                    raise RuntimeError("Voice preferences were removed by the settings checks")
                restored = json.loads(voice_path.read_text(encoding="utf-8"))
                if set(restored) != set(voice_reference):
                    raise RuntimeError("Voice preferences file changed shape across the settings checks")
                for key, before in voice_reference.items():
                    after = restored[key]
                    if type(before) is float and type(after) is float:
                        if not math.isfinite(after) or abs(after - before) > .01:
                            raise RuntimeError(f"Voice preference {key} drifted across the settings checks")
                    elif before != after:
                        raise RuntimeError(f"Voice preference {key} changed across the settings checks")
            for location, process in (("startup", fresh), ("pause", loaded)):
                voice_checks[location] = _voice_settings_diagnostic(process.stdout, location)
        controls_checks = {}
        if controls_settings_check:
            if controls_bytes is not None and (
                    not controls_path.is_file() or
                    controls_path.read_bytes() != controls_bytes):
                raise RuntimeError("Controls bindings changed across the settings checks")
            for location, process in (("startup", fresh), ("pause", loaded)):
                controls_checks[location] = _controls_diagnostic(process.stdout, location)
        capture_paths = [menu_capture, modes_capture, setup_capture,
                         loading_capture, final_capture, reload_capture]
        if video_settings_check:
            for base, dimensions, stdout in ((final_capture, (1280, 720), fresh.stdout),
                                              (reload_capture, (1920, 1080), loaded.stdout)):
                for suffix in ("-video-settings", "-video-confirm"):
                    path = base.with_stem(base.stem + suffix)
                    _bmp(path, *dimensions, stdout)
                    capture_paths.append(path)
        if audio_settings_check:
            capture_paths.extend((fresh_settings_capture, reload_settings_capture))
        if general_settings_check:
            for base, dimensions, stdout in ((final_capture, (1280, 720), fresh.stdout),
                                              (reload_capture, (1920, 1080), loaded.stdout)):
                path = base.with_stem(base.stem + "-general-settings")
                _bmp(path, *dimensions, stdout)
                capture_paths.append(path)
        if voice_settings_check:
            for base, dimensions, stdout in ((final_capture, (1280, 720), fresh.stdout),
                                              (reload_capture, (1920, 1080), loaded.stdout)):
                path = base.with_stem(base.stem + "-voice-settings")
                _bmp(path, *dimensions, stdout)
                capture_paths.append(path)
        if controls_settings_check:
            for base, dimensions, stdout in ((final_capture, (1280, 720), fresh.stdout),
                                              (reload_capture, (1920, 1080), loaded.stdout)):
                path = base.with_stem(base.stem + "-controls-settings")
                _bmp(path, *dimensions, stdout)
                capture_paths.append(path)
        for path in capture_paths:
            evidence = folder.parent / (folder.name + "-" + path.name)
            shutil.copy2(path, evidence)
            captures.append(str(evidence))
        diagnostics.extend((fresh.stdout.strip(), loaded.stdout.strip()))
        result = {
            "nativeNewGamePlayerInput": True,
            "nativeNewGameIndependentSave": True,
            "nativeNewGamePausedReload": True,
            "newGameCaptures": captures,
            "newGameDiagnostics": diagnostics,
        }
        if audio_check:
            result["nativeNewGameAudioCheck"] = True
            result["newGameAudioChecks"] = {"fresh": fresh_audio, "reload": reload_audio}
        if audio_settings_check:
            result["nativeAudioSettingsCheck"] = True
            result["audioSettingsChecks"] = {"fresh": fresh_audio_settings,
                                               "reload": reload_audio_settings}
        if video_settings_check:
            result["nativeVideoSettingsCheck"] = True
            result["videoSettingsChecks"] = video_checks
        if general_settings_check:
            result["nativeGeneralSettingsCheck"] = True
            result["generalSettingsChecks"] = general_checks
        if voice_settings_check:
            result["nativeVoiceSettingsCheck"] = True
            result["voiceSettingsChecks"] = voice_checks
        if controls_settings_check:
            result["nativeControlsSettingsCheck"] = True
            result["controlsSettingsChecks"] = controls_checks
        return result
