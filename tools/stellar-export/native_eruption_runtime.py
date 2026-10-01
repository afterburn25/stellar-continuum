"""Live native developer eruption smoke on an isolated developer campaign."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from native_client_runtime import _validate_capture
from native_new_game_runtime import _source_payload, _SEED, _COUNT, _SPECIES
from native_support_runtime import _clean_environment

_PASS = ("live_map_system_same_id_variant_position_timeline_continuity"
         "_and_campaign_payload_passed")


def _proof(stdout: str) -> dict:
    matches = re.findall(r"stellar_eruptions=(\S+) id=(\d+) map=([\d.]+) "
                         r"system=([\d.]+) live_system=([\d.]+) live_map=([\d.]+)",
                         stdout)
    if len(matches) != 1:
        raise RuntimeError("Eruption smoke did not report one continuity proof")
    marker, event_id, map_progress, system_progress, live_system, live_map = matches[0]
    if marker != _PASS:
        raise RuntimeError("Eruption smoke continuity contract failed")
    try:
        value = {"event_id": int(event_id), "map": float(map_progress),
                 "system": float(system_progress), "live_system": float(live_system),
                 "live_map": float(live_map)}
    except ValueError as error:
        raise RuntimeError("Eruption proof fields are malformed") from error
    if not (value["event_id"] >= 0 and 0 < value["map"] <= 1 and
            0 < value["system"] <= 1 and 0 < value["live_system"] <= 1 and
            0 < value["live_map"] <= 1):
        raise RuntimeError("Eruption proof reported out-of-range progress values")
    return value


def validate_native_eruption_export(folder: Path, env: dict[str, str], fixture: Path):
    folder = folder.resolve()
    captures = []
    with tempfile.TemporaryDirectory(prefix="stellar-native-eruption-") as temporary:
        work = Path(temporary)
        anchor = work / "campaign.player17.json"
        anchor.write_text(json.dumps(_source_payload(fixture), ensure_ascii=False),
                          encoding="utf-8")
        anchor_bytes = anchor.read_bytes()
        capture = work / "eruption-1280x720.bmp"
        args = [str(folder / "stellar-continuum-native.exe"), "--asset-root", str(folder),
                "--save-path", str(anchor), "--seed", _SEED, "--width", "1280",
                "--height", "720", "--windowed", "--devtools",
                "--eruption-smoke", str(capture)]
        result = subprocess.run(args, cwd=work, env=_clean_environment(env),
                                capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=240)
        log = folder.parent / (folder.name + "-eruption.stdout.txt")
        log.write_text(result.stdout + "\nSTDERR\n" + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(
                f"Native eruption smoke failed ({result.returncode}): {result.stderr}")
        proof = _proof(result.stdout)
        developer_save = work / "developer" / "campaign.dev17.json"
        if not developer_save.is_file():
            raise RuntimeError("Eruption smoke did not create an isolated developer save")
        payload = json.loads(developer_save.read_text(encoding="utf-8"))
        campaign = payload.get("Campaign") or {}
        galaxy = campaign.get("Galaxy", {})
        metadata = galaxy.get("GenerationMetadata") or {}
        if (payload.get("DeveloperFormatVersion") != 1 or
                payload.get("DeveloperSession") is not True or
                campaign.get("FormatVersion") != 17 or
                galaxy.get("Seed") != int(_SEED) or
                len(galaxy.get("Systems", [])) != _COUNT or
                metadata.get("PlayerSpeciesId") != _SPECIES):
            raise RuntimeError("Eruption developer save is not the requested campaign")
        if anchor.read_bytes() != anchor_bytes:
            raise RuntimeError("Eruption smoke modified the save-path anchor")
        images = ["", "-eruption-a-rising", "-eruption-controls", "-eruption-live-map",
                  "-eruption-map", "-eruption-system", "-setup", "-loading",
                  "-galaxy-types", "-galaxy-selected", "-population",
                  "-population-dropdown", "-menu", "-modes"]
        for suffix in images:
            image = capture.with_stem(capture.stem + suffix)
            _validate_capture(image, 1280, 720)
            destination = folder.parent / (folder.name + "-" + image.name)
            shutil.copy2(image, destination)
            captures.append(str(destination))
    return {"nativeEruption": True, "eruptionProof": proof,
            "eruptionCaptures": captures, "eruptionDiagnostics": str(log)}
