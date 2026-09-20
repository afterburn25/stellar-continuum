"""Live native New Game: save fence, cancellation, exit, unique slot and reload."""
from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_new_game_runtime import _bmp, _source_payload, _verify_campaign, _resolved_reported


def _state(stdout: str, action: str, audio_check: bool) -> dict:
    matches = re.findall(r"^restart=(\{[^\n]+\})$", stdout, re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError("Restart did not report one lifecycle outcome")
    if "restart_renderer=vulkan" not in stdout:
        raise RuntimeError("Restart did not use the Vulkan presentation path")
    value = json.loads(matches[0])
    expected = {"setup_opened": True, "boot_replayed": False,
                "menu_ready_recalled": False, "exit": action == "exit",
                "returned": action == "cancel", "created": action == "create"}
    if set(value) != set(expected) | {"music_start_count"}:
        raise RuntimeError("Restart evidence schema changed")
    if any(value[key] is not wanted for key, wanted in expected.items()):
        raise RuntimeError("Restart replayed startup or returned the wrong outcome")
    if type(value["music_start_count"]) is not int or (audio_check and value["music_start_count"] != 1):
        raise RuntimeError("Restart music was missing or restarted")
    if action == "cancel" and "restart_cancel=preserved_same_campaign" not in stdout:
        raise RuntimeError("Cancellation did not preserve the live campaign and camera")
    return value


def _unchanged(before: dict, after: dict) -> None:
    left, right = copy.deepcopy(before), copy.deepcopy(after)
    left.pop("SavedAtUtc", None)
    right.pop("SavedAtUtc", None)
    if left != right:
        raise RuntimeError("Restart changed the previous campaign beyond its save timestamp")


def verify_native_restart(folder: Path, fixture: Path, *, audio_check: bool = True) -> dict:
    folder = folder.resolve()
    executable = folder / "stellar-continuum-native.exe"
    captures, results, logs = [], {}, []
    clean = os.environ.copy()
    system = Path(clean.get("SystemRoot", r"C:\Windows"))
    clean["PATH"] = str(system / "System32") + os.pathsep + str(system)
    with tempfile.TemporaryDirectory(prefix="stellar-restart-路径-") as temporary:
        work = Path(temporary)
        cwd = work / "different-cwd"; cwd.mkdir()
        anchor = work / "original.player17.json"
        anchor.write_text(json.dumps(_source_payload(fixture), ensure_ascii=False), encoding="utf-8")

        def run(mode: str, name: str, save: Path, width: int, height: int):
            image = work / (name + ".bmp")
            args = [str(executable), "--asset-root", str(folder), "--save-path", str(save),
                    "--load", "--seed", "143250", "--width", str(width), "--height", str(height),
                    "--windowed", mode, str(image)]
            if audio_check: args.append("--audio-check")
            process = subprocess.run(args, cwd=cwd, env=clean, capture_output=True,
                                     text=True, encoding="utf-8", errors="replace", timeout=120)
            log = folder.parent / (folder.name + "-" + name + ".log")
            log.write_text(process.stdout + "\nSTDERR\n" + process.stderr, encoding="utf-8")
            logs.append(str(log))
            if process.returncode != 0:
                raise RuntimeError(f"Native restart {name} failed ({process.returncode}): {process.stderr}")
            return image, process.stdout

        def capture(path: Path, width: int, height: int):
            _bmp(path, width, height)
            destination = folder.parent / (folder.name + "-" + path.name)
            shutil.copy2(path, destination); captures.append(str(destination))

        # Normalize once through the actual native Player17 load/save boundary.
        baseline_image, _ = run("--smoke", "restart-baseline", anchor, 1280, 720)
        capture(baseline_image, 1280, 720)
        baseline = json.loads(anchor.read_text(encoding="utf-8"))
        for action, size in (("cancel", (1280,720)), ("exit", (1920,1080)), ("create", (1280,720))):
            width, height = size
            mode = "--restart-smoke" if action == "create" else f"--restart-{action}-smoke"
            image, stdout = run(mode, "restart-" + action, anchor, width, height)
            results[action] = _state(stdout, action, audio_check)
            _unchanged(baseline, json.loads(anchor.read_text(encoding="utf-8")))
            capture(image.with_stem(image.stem + "-saved"), width, height)
            capture(image.with_stem(image.stem + "-setup"), width, height)
            if action != "exit": capture(image, width, height)
            if action == "create":
                capture(image.with_stem(image.stem + "-loading"), width, height)
                paths = re.findall(r"^restart_new_save=(.+)$", stdout, re.MULTILINE)
                if len(paths) != 1: raise RuntimeError("Restart did not report its new save")
                generated = _resolved_reported(paths[0], work, "generated save")
                if generated == anchor.resolve() or not generated.is_file():
                    raise RuntimeError("Restart did not use an independent save slot")
                created = json.loads(generated.read_text(encoding="utf-8"))
                _verify_campaign(created, {"system_count": 250, "species_id": "pelagic_high_pressure", "seed": "143250"})
                reload_image, _ = run("--smoke", "restart-reload", generated, 1920, 1080)
                capture(reload_image, 1920, 1080)
                _unchanged(created, json.loads(generated.read_text(encoding="utf-8")))
                _unchanged(baseline, json.loads(anchor.read_text(encoding="utf-8")))
    return {"nativeMidSessionNewGame": True, "exactPreviousCampaignPreserved": True,
            "independentSlotAndPausedReload": True, "outcomes": results,
            "captures": captures, "logs": logs}
