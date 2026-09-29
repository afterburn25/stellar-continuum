"""Relocated native navigation replay and paused campaign preservation."""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_client_runtime import _validate_capture


def navigation_proof(stdout):
    markers = re.findall(r" navigation=(\{[^\n]+\})$", stdout, re.MULTILINE)
    if len(markers) != 1:
        raise RuntimeError("Native navigation proof is missing or duplicated")
    try:
        proof = json.loads(markers[0])
    except ValueError as error:
        raise RuntimeError("Native navigation proof is malformed") from error
    flags = ("no_charge", "canonical_payload_unchanged", "menu_blocked", "modal_blocked",
             "pause_retained", "day_unchanged", "keyboard_galaxy_playback",
             "keyboard_system_playback", "keyboard_save_requested", "keyboard_text_preserved")
    if (not isinstance(proof, dict) or type(proof.get("switches")) is not int or
            proof["switches"] != 4 or proof.get("final_workspace") != "relations" or
            type(proof.get("keyboard_blocked_contexts")) is not int or
            proof["keyboard_blocked_contexts"] != 4 or
            any(proof.get(key) is not True for key in flags)):
        raise RuntimeError("Native navigation replay did not satisfy the input/state contract")
    before, after = proof.get("credits_before"), proof.get("credits_after")
    if (type(before) not in (int, float) or type(after) not in (int, float) or
            not math.isfinite(before) or not math.isfinite(after) or before != after):
        raise RuntimeError("Native navigation altered the treasury")
    return proof


def _replay_verified(stdout: str) -> dict:
    match = re.search(r"(?:^|\s)replay_verified=(\{[^{}]*\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native navigation replay did not report replay_verified")
    try:
        proof = json.loads(match.group(1))
    except ValueError as error:
        raise RuntimeError("Native navigation replay_verified is malformed") from error
    if (type(proof.get("commands")) is not int or proof["commands"] <= 0 or
            type(proof.get("checkpoints")) is not int or proof["checkpoints"] <= 0):
        raise RuntimeError("Native navigation replay verified no commands or checkpoints")
    return proof


def validate_native_navigation_export(folder: Path, env: dict[str, str],
                                      *, replay_check: bool = False):
    folder = folder.resolve()
    with tempfile.TemporaryDirectory(prefix="stellar-native-navigation-") as temporary:
        work = Path(temporary)
        save = work / "navigation.player17.json"
        system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
        clean_env = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
        captures, diagnostics = [], []
        before = None
        for loading, (width, height) in ((False, (1280, 720)), (True, (1920, 1080))):
            capture = work / ("loaded.bmp" if loading else "fresh.bmp")
            args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--width", str(width), "--height", str(height),
                    "--navigation-smoke", str(capture)]
            if loading:
                args.append("--load")
            result = subprocess.run(args, cwd=work, env=clean_env, capture_output=True,
                                    text=True, timeout=90)
            if result.returncode:
                raise RuntimeError(f"Native navigation failed ({result.returncode}): {result.stderr}")
            if any(marker not in result.stdout for marker in ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
                raise RuntimeError("Native navigation did not confirm renderer, campaign and save")
            navigation_proof(result.stdout)
            _validate_capture(capture, width, height)
            if not save.is_file():
                raise RuntimeError("Native navigation did not save the campaign")
            payload = json.loads(save.read_text(encoding="utf-8"))
            if (payload.get("FormatVersion") != 17 or not payload.get("SavedAtUtc") or
                    len(payload.get("Galaxy", {}).get("Systems", [])) != 500):
                raise RuntimeError("Native navigation save is not a 500-system Player17 campaign")
            payload.pop("SavedAtUtc")
            if loading and payload != before:
                raise RuntimeError("Native navigation campaign changed during paused reload")
            before = payload
            evidence = folder.parent / (folder.name + ("-navigation-loaded.bmp" if loading else "-navigation-fresh.bmp"))
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            diagnostics.append(result.stdout.strip())
        if not replay_check:
            return {"nativeNavigationInput": True, "nativeNavigationPausedReload": True,
                    "navigationCaptures": captures, "navigationDiagnostics": diagnostics}
        # Record the same smoke on the loaded anchor, restore the anchor, then
        # replay the journal — the recorded F6 must reproduce every save
        # section checkpoint through the real input path.
        journal = work / "navigation.replay"
        anchor = save.read_bytes()
        record = subprocess.run([str(folder / "stellar-continuum-native.exe"),
                                 "--asset-root", str(folder),
                                 "--save-path", str(save), "--load",
                                 "--width", "1280", "--height", "720",
                                 "--navigation-smoke", str(work / "recorded.bmp"),
                                 "--record", str(journal)],
                                cwd=work, env=clean_env, capture_output=True,
                                text=True, timeout=90)
        if record.returncode:
            raise RuntimeError(f"Native navigation record failed ({record.returncode}): {record.stderr}")
        navigation_proof(record.stdout)
        recorded = re.search(r"(?:^|\s)replay=(\{[^{}]*\})(?:\s|$)", record.stdout)
        if not recorded:
            raise RuntimeError("Native navigation record did not report its journal")
        recorded = json.loads(recorded.group(1))
        # The recorded F6 rewrote the anchor's atomic sidecars — restore the
        # pre-run primary and clear them so replay loads the recorded start.
        save.write_bytes(anchor)
        for sidecar in save.parent.glob(save.name + ".*"):
            sidecar.unlink()
        replay = subprocess.run([str(folder / "stellar-continuum-native.exe"),
                                 "--asset-root", str(folder),
                                 "--save-path", str(save), "--load",
                                 "--width", "1280", "--height", "720",
                                 "--replay", str(journal), "--replay-exit"],
                                cwd=work, env=clean_env, capture_output=True,
                                text=True, timeout=90)
        if replay.returncode:
            raise RuntimeError(f"Native navigation replay failed ({replay.returncode}): {replay.stderr}")
        verified = _replay_verified(replay.stdout)
        if (verified["commands"] != recorded.get("commands") or
                verified["checkpoints"] != recorded.get("checkpoints")):
            raise RuntimeError(
                "Native navigation replay verified a different journal than recorded")
        # --replay-info re-verifies each retained expected sidecar against the
        # journaled section hashes — a stale capture poisons later leaf-diffs.
        info = subprocess.run([str(folder / "stellar-continuum-native.exe"),
                               "--asset-root", str(folder),
                               "--replay-info", str(journal)],
                              cwd=work, env=clean_env, capture_output=True,
                              text=True, timeout=90)
        if info.returncode:
            raise RuntimeError(f"Native navigation replay-info failed ({info.returncode}): {info.stderr}")
        inventory = re.search(r"replay_info=(\{[^\n]+\})\s*$", info.stdout, re.MULTILINE)
        if not inventory:
            raise RuntimeError("Native navigation replay-info did not report its inventory")
        inventory = json.loads(inventory.group(1))
        if (inventory.get("commands") != recorded.get("commands") or
                not inventory.get("commands_ordered") or
                not inventory.get("checkpoints_ordered") or
                inventory.get("pointer_out_of_bounds") != 0 or
                inventory.get("unverified_tail_commands") != 0):
            raise RuntimeError("Native navigation replay-info reported an unsound journal")
        checkpoints = inventory.get("checkpoints")
        if (not isinstance(checkpoints, list) or not checkpoints or
                any(not row.get("expected_document") or not row.get("expected_verified")
                    for row in checkpoints)):
            raise RuntimeError("Native navigation replay-info found a stale expected sidecar")
        return {"nativeNavigationInput": True, "nativeNavigationPausedReload": True,
                "nativeNavigationReplayVerified": True,
                "nativeNavigationReplay": verified,
                "nativeNavigationReplayInfoVerified": True,
                "navigationCaptures": captures, "navigationDiagnostics": diagnostics}
