"""Relocated native economy workspace and Player17 priority persistence proof."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_new_game_runtime import _bmp

_CHECKS = {
    "navigation", "canonical_totals", "single_projection", "paused_cached",
    "scroll_bounded", "research_visible", "map_stationary", "refresh",
    "workspace_isolation", "priority_saved", "only_priority_changed",
}


def parse_economy_check(stdout: str) -> dict:
    records = re.findall(r"^economy_inspection=(.*)$", stdout, re.MULTILINE)
    if len(records) != 1:
        raise RuntimeError("Expected one economy inspection proof")

    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate economy key")
            result[key] = value
        return result

    try:
        state = json.loads(records[0], object_pairs_hook=unique_pairs)
    except (TypeError, ValueError) as error:
        raise RuntimeError("Malformed economy proof") from error
    if (not isinstance(state, dict) or set(state) != _CHECKS or
            any(value is not True for value in state.values())):
        raise RuntimeError("Economy inspection proof is incomplete")
    return state


def check_economy_payload(payload: dict) -> None:
    """Require the paused Player17 payload to retain only the player's final priority."""
    if not isinstance(payload, dict):
        raise RuntimeError("Economy save is not an object")
    galaxy = payload.get("Galaxy")
    systems = galaxy.get("Systems") if isinstance(galaxy, dict) else None
    if (payload.get("FormatVersion") != 17 or payload.get("SimulationDays") != 0 or
            not isinstance(galaxy, dict) or not isinstance(systems, list) or
            len(systems) != 500):
        raise RuntimeError("Economy did not preserve a paused Player17 500-system campaign")
    player_id = galaxy.get("PlayerCivilizationId")
    economies = galaxy.get("Economies")
    if type(player_id) is not int or not isinstance(economies, list):
        raise RuntimeError("Economy save has no Player17 economy collection")
    own = [item for item in economies if isinstance(item, dict) and
           item.get("CivilizationId") == player_id]
    foreign = [item for item in economies if isinstance(item, dict) and
               item.get("CivilizationId") != player_id]
    if len(own) != 1 or len(own) + len(foreign) != len(economies):
        raise RuntimeError("Economy save has no unique player economy owner")
    if type(own[0].get("IndustryPriority")) is not int or own[0]["IndustryPriority"] != 2:
        raise RuntimeError("Economy priority was not saved for the actual player")


def validate_native_economy_export(folder: Path, env: dict[str, str]) -> dict:
    folder = folder.resolve()
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics = [], []
    baseline = None
    with tempfile.TemporaryDirectory(prefix="stellar-economy-路径-") as temporary:
        work = Path(temporary)
        cwd = work / "different-cwd-目录"
        cwd.mkdir()
        save = work / "economy.player17.json"
        for width, height, reload in ((1280, 720, False), (1920, 1080, True)):
            capture = work / f"economy-{width}x{height}.bmp"
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--width", str(width), "--height", str(height),
                    "--smoke", str(capture), "--economy-check"]
            if reload:
                args.append("--load")
            result = subprocess.run(args, cwd=cwd, env=clean, capture_output=True,
                                    text=True, encoding="utf-8", timeout=150)
            if result.returncode:
                raise RuntimeError(f"Native economy failed ({result.returncode}): "
                                   f"{result.stdout}\n{result.stderr}")
            parse_economy_check(result.stdout)
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
                raise RuntimeError("Economy did not prove Vulkan, 500 systems and saving")
            for suffix in ("", "-economy-ready", "-economy-end", "-economy-priority"):
                source = capture.with_stem(capture.stem + suffix)
                _bmp(source, width, height)
                target = folder.parent / f"{folder.name}-{source.name}"
                shutil.copy2(source, target)
                captures.append(str(target))
            try:
                payload = json.loads(save.read_text(encoding="utf-8"))
            except (OSError, ValueError) as error:
                raise RuntimeError("Native economy did not save valid Player17 JSON") from error
            check_economy_payload(payload)
            payload.pop("SavedAtUtc", None)
            if baseline is not None and payload != baseline:
                raise RuntimeError("Economy reload changed Player17 state beyond its saved priority")
            baseline = payload
            diagnostics.append(result.stdout.strip())
    return {"nativeEconomyWorkspace": True, "economyPriorityReloadVerified": True,
            "economyCaptures": captures, "economyDiagnostics": diagnostics}
