"""Live native developer-index replay on an isolated developer campaign."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_client_runtime import _validate_capture
from native_eruption_runtime import _proof as _eruption_proof
from native_new_game_runtime import _source_payload, _SEED, _SPECIES
from native_support_runtime import _clean_environment

# The developer leg also exercises the new-game-smoke modifiers: a non-default
# galaxy card and system count, plus the developer-only full-exploration setup
# checkbox (the only context where --smoke-full-exploration is legal).
_SYSTEM_COUNT = 500

# Index/panel/reveal surfaces every replay must produce; the remaining catalog
# captures are validated in place but not copied as packaged evidence.
_REQUIRED_CAPTURES = {"-explored-galaxy", "-celestial-index", "-planet-index",
                      "-empires", "-events", "-performance", "-eruption-system",
                      "-mars-globe-front", "-jupiter-globe-front",
                      "-saturn-globe-front"}


def validate_native_developer_export(folder: Path, env: dict[str, str], fixture: Path):
    folder = folder.resolve()
    captures = []
    with tempfile.TemporaryDirectory(prefix="stellar-native-developer-") as temporary:
        work = Path(temporary)
        anchor = work / "campaign.player17.json"
        anchor.write_text(json.dumps(_source_payload(fixture), ensure_ascii=False),
                          encoding="utf-8")
        anchor_bytes = anchor.read_bytes()
        capture = work / "developer-1280x720.bmp"
        args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                "--save-path", str(anchor), "--seed", _SEED, "--width", "1280",
                "--height", "720", "--windowed", "--devtools",
                "--developer-smoke", str(capture),
                "--smoke-galaxy-card", "2", "--smoke-system-count",
                str(_SYSTEM_COUNT), "--smoke-full-exploration"]
        result = subprocess.run(args, cwd=work, env=_clean_environment(env),
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=600)
        log = folder.parent / (folder.name + "-developer.stdout.txt")
        log.write_text(result.stdout + "\nSTDERR\n" + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(
                f"Native developer smoke failed ({result.returncode}): {result.stderr}")
        eruption = _eruption_proof(result.stdout)
        counts = re.findall(r'new_game=\{[^{}]*"system_count":(\d+)', result.stdout)
        if len(counts) != 1 or int(counts[0]) != _SYSTEM_COUNT:
            raise RuntimeError("Developer replay did not honor the smoke system count")
        territories = re.findall(r'"unexplored":(\d+)', result.stdout)
        if len(territories) != 1 or int(territories[0]) != 0:
            raise RuntimeError("Developer replay did not reveal the entire galaxy")
        developer_save = work / "developer" / "campaign.dev17.json"
        if not developer_save.is_file():
            raise RuntimeError("Developer smoke did not create an isolated developer save")
        payload = json.loads(developer_save.read_text(encoding="utf-8"))
        campaign = payload.get("Campaign") or {}
        galaxy = campaign.get("Galaxy", {})
        metadata = galaxy.get("GenerationMetadata") or {}
        if (payload.get("DeveloperFormatVersion") != 1 or
                payload.get("DeveloperSession") is not True or
                campaign.get("FormatVersion") != 17 or
                galaxy.get("Seed") != int(_SEED) or
                len(galaxy.get("Systems", [])) != _SYSTEM_COUNT or
                metadata.get("SystemCount") != _SYSTEM_COUNT or
                metadata.get("PlayerSpeciesId") != _SPECIES):
            raise RuntimeError("Developer save is not the requested campaign")
        if anchor.read_bytes() != anchor_bytes:
            raise RuntimeError("Developer smoke modified the save-path anchor")
        _validate_capture(capture, 1280, 720)
        produced = {image.stem[len(capture.stem):]: image
                    for image in work.glob(capture.stem + "-*.bmp")}
        missing = _REQUIRED_CAPTURES - set(produced)
        if missing:
            raise RuntimeError(
                f"Developer replay did not produce required captures: {sorted(missing)}")
        for image in produced.values():
            _validate_capture(image, 1280, 720)
        for image in [capture] + [produced[suffix]
                                  for suffix in sorted(_REQUIRED_CAPTURES)]:
            destination = folder.parent / (folder.name + "-" + image.name)
            shutil.copy2(image, destination)
            captures.append(str(destination))
    return {"nativeDeveloper": True, "developerEruptionProof": eruption,
            "developerCaptures": captures, "developerDiagnostics": str(log)}
