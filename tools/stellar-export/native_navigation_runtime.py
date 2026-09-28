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


def validate_native_navigation_export(folder: Path, env: dict[str, str]):
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
        return {"nativeNavigationInput": True, "nativeNavigationPausedReload": True,
                "navigationCaptures": captures, "navigationDiagnostics": diagnostics}
