"""Relocated native supply workspace input and read-only save/reload proof."""
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
    "scroll_bounded", "map_stationary", "refresh", "workspace_isolation", "campaign_unchanged",
}


def parse_supply_check(stdout: str) -> dict:
    records = re.findall(r"^supply_inspection=(.*)$", stdout, re.MULTILINE)
    if len(records) != 1:
        raise RuntimeError("Expected one supply inspection proof")

    def unique_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate supply key")
            result[key] = value
        return result

    try:
        state = json.loads(records[0], object_pairs_hook=unique_pairs)
    except (TypeError, ValueError) as error:
        raise RuntimeError("Malformed supply proof") from error
    if (not isinstance(state, dict) or set(state) != _CHECKS or
            any(value is not True for value in state.values())):
        raise RuntimeError("Supply inspection proof is incomplete")
    return state


def validate_native_supply_export(folder: Path, env: dict[str, str]) -> dict:
    folder = folder.resolve()
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean = dict(env, PATH=str(system_root / "System32") + os.pathsep + str(system_root))
    captures, diagnostics = [], []
    baseline = None
    with tempfile.TemporaryDirectory(prefix="stellar-supply-路径-") as temporary:
        work = Path(temporary)
        cwd = work / "different-cwd-目录"
        cwd.mkdir()
        save = work / "supply.player17.json"
        for width, height, reload in ((1280, 720, False), (1920, 1080, True)):
            capture = work / f"supply-{width}x{height}.bmp"
            args = [str(folder / "stellar-continuum-native.exe"),
                    "--asset-root", str(folder), "--save-path", str(save),
                    "--width", str(width), "--height", str(height),
                    "--smoke", str(capture), "--logistics-check"]
            if reload:
                args.append("--load")
            result = subprocess.run(args, cwd=cwd, env=clean, capture_output=True,
                                    text=True, encoding="utf-8", timeout=150)
            if result.returncode:
                raise RuntimeError(f"Native supply failed ({result.returncode}): "
                                   f"{result.stdout}\n{result.stderr}")
            parse_supply_check(result.stdout)
            if any(token not in result.stdout for token in
                   ("gpu_driver=vulkan ", "systems=500 ", "save=ok ")):
                raise RuntimeError("Supply did not prove Vulkan, 500 systems and saving")
            for suffix in ("", "-supply-ready", "-supply-end"):
                source = capture.with_stem(capture.stem + suffix)
                _bmp(source, width, height)
                target = folder.parent / f"{folder.name}-{source.name}"
                shutil.copy2(source, target)
                captures.append(str(target))
            payload = json.loads(save.read_text(encoding="utf-8"))
            if (payload.get("FormatVersion") != 17 or payload.get("SimulationDays") != 0 or
                    len(payload.get("Galaxy", {}).get("Systems", [])) != 500):
                raise RuntimeError("Supply modified the paused campaign")
            payload.pop("SavedAtUtc", None)
            if baseline is not None and baseline != payload:
                raise RuntimeError("Supply reload changed Player17 state")
            baseline = payload
            diagnostics.append(result.stdout.strip())
    return {"nativeSupplyNetwork": True, "supplyPausedReloadUnchanged": True,
            "supplyCaptures": captures, "supplyDiagnostics": diagnostics}
