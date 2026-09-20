"""Relocated native player-input research and durable progress checks."""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import re

from native_client_runtime import _validate_capture


def _player_research(payload):
    player_id = payload["Galaxy"]["PlayerCivilizationId"]
    players = [entry for entry in payload["AdaptiveResearch"]["Civilizations"]
               if entry["CivilizationId"] == player_id]
    economies = [entry for entry in payload["Galaxy"]["Economies"]
                 if entry["CivilizationId"] == player_id]
    if len(players) != 1 or len(economies) != 1:
        raise RuntimeError("Research save has no unique player research/economy owner")
    # Current Player17 owns the maintained research snapshot version chain.
    state = players[0]["Research"]["Research"]["Research"]["Research"]["Core"]
    return state["ActiveProjects"], economies[0]


def validate_native_research_export(folder: Path, env: dict[str, str]):
    folder = folder.resolve()
    with tempfile.TemporaryDirectory(prefix="stellar-native-research-") as temporary:
        work = Path(temporary)
        save = work / "research.player17.json"
        system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
        clean_env = dict(env, PATH=str(system_root/"System32") + os.pathsep + str(system_root))
        diagnostics = []
        captures = []
        inspector_captures = []
        before = None
        for loading in (False, True):
            capture = work / ("loaded.bmp" if loading else "started.bmp")
            width, height = (1920, 1080) if loading else (1280, 720)
            args = [str(folder/"stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--width", str(width), "--height", str(height),
                    "--research-smoke", str(capture)]
            if loading:
                args.append("--load")
            result = subprocess.run(args, cwd=work, env=clean_env, capture_output=True,
                                    text=True, timeout=90)
            if result.returncode != 0:
                raise RuntimeError(f"Native research input replay failed ({result.returncode}): {result.stderr}")
            if "gpu_driver=vulkan" not in result.stdout or "systems=500 " not in result.stdout:
                raise RuntimeError(f"Native research did not confirm renderer/campaign: {result.stdout}")
            if "save=ok " not in result.stdout:
                raise RuntimeError("Native research did not confirm an actual manual save")
            _validate_capture(capture, width, height)
            inspector = capture.with_name(capture.stem + "-inspector-end.bmp")
            markers = re.findall(r"(?m)^research_inspector=(\{[^\n]+\})$", result.stdout)
            if len(markers) != 1:
                raise RuntimeError("Native research did not report its inspector-end proof")
            try:
                inspector_state = json.loads(markers[0])
            except ValueError as error:
                raise RuntimeError("Native research inspector proof is malformed") from error
            if (set(inspector_state) != {"final_line_visible", "graph_stationary"} or
                    any(type(inspector_state[key]) is not bool or inspector_state[key] is not True
                        for key in inspector_state)):
                raise RuntimeError("Native research inspector proof is invalid")
            if not inspector.is_file():
                raise RuntimeError("Native research inspector-end capture is missing")
            try:
                _validate_capture(inspector, width, height)
            except RuntimeError as error:
                raise RuntimeError("Native research inspector-end capture is invalid") from error
            if not save.is_file():
                raise RuntimeError("Native research did not save its campaign")
            payload = json.loads(save.read_text(encoding="utf-8"))
            if not payload.get("SavedAtUtc"):
                raise RuntimeError("Native research save has no capture timestamp")
            if payload.get("FormatVersion") != 17 or len(payload.get("Galaxy", {}).get("Systems", [])) != 500:
                raise RuntimeError("Native research save is not a Player17 500-system campaign")
            projects, economy = _player_research(payload)
            advanced = [project for project in projects
                        if not project["Paused"] and
                        math.isfinite(project["TotalResearchPoints"]) and
                        project["TotalResearchPoints"] > 0]
            if not advanced or not math.isfinite(economy["LastResearchSpendingPerDay"]) or economy["LastResearchSpendingPerDay"] <= 0:
                raise RuntimeError("Player input did not produce funded, advancing research in the saved campaign")
            payload.pop("SavedAtUtc", None)
            if loading and payload != before:
                raise RuntimeError("Research campaign changed during paused load/recapture")
            before = payload
            evidence = folder.parent / (folder.name + ("-research-loaded.bmp" if loading else "-research-started.bmp"))
            shutil.copy2(capture, evidence)
            captures.append(str(evidence))
            inspector_evidence = folder.parent / (folder.name + ("-research-loaded-inspector-end.bmp" if loading else "-research-started-inspector-end.bmp"))
            shutil.copy2(inspector, inspector_evidence)
            inspector_captures.append(str(inspector_evidence))
            diagnostics.append(result.stdout.strip())
        return {"nativeResearchPlayerInput": True, "nativeResearchProgressReload": True,
                "researchCaptures": captures, "researchInspectorCaptures": inspector_captures,
                "researchDiagnostics": diagnostics}
