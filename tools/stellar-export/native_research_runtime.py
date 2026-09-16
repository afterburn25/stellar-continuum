"""Relocated native player-input research and durable progress checks."""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


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
    with tempfile.TemporaryDirectory(prefix="stellar-native-research-") as temporary:
        work = Path(temporary)
        save = work / "research.player17.json"
        system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
        clean_env = dict(env, PATH=str(system_root/"System32") + os.pathsep + str(system_root))
        diagnostics = []
        captures = []
        before = None
        for loading in (False, True):
            capture = work / ("loaded.bmp" if loading else "started.bmp")
            args = [str(folder/"stellar-continuum-native.exe"), "--asset-root", str(folder),
                    "--save-path", str(save), "--research-smoke", str(capture)]
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
            if not loading and " shortcut=1" not in result.stdout:
                raise RuntimeError(
                    "Native research did not exercise the T/R candidate shortcuts")
            if not capture.is_file() or capture.stat().st_size < 54 or capture.read_bytes()[:2] != b"BM":
                raise RuntimeError("Native research did not capture its rendered workspace")
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
            diagnostics.append(result.stdout.strip())
        return {"nativeResearchPlayerInput": True, "nativeResearchProgressReload": True,
                "researchCaptures": captures, "researchDiagnostics": diagnostics}
