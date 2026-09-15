"""Exact approved native audio streams; no implicit directory-wide packaging."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


NATIVE_AUDIO_SOURCES = {
    "music": ("assets/audio/music/claimed-by-the-void-loop.mp3",
              "assets/audio/music/claimed-by-the-void-loop.mp3"),
    "ui-hover": ("assets/audio/sfx/ui-hover.wav",
                 "assets/audio/sfx/ui-hover.wav"),
    "ui-confirm": ("assets/audio/sfx/ui-confirm.wav",
                   "assets/audio/sfx/ui-confirm.wav"),
    "discovery-reveal": ("assets/audio/sfx/discovery-reveal.wav",
                         "assets/audio/sfx/discovery-reveal.wav"),
    "construction-complete": ("assets/audio/sfx/construction-complete.wav",
                              "assets/audio/sfx/construction-complete.wav"),
    "ship-launch": ("assets/audio/sfx/ship-launch.wav",
                    "assets/audio/sfx/ship-launch.wav"),
    "strategic-alert": ("assets/audio/sfx/strategic-alert.wav",
                        "assets/audio/sfx/strategic-alert.wav"),
    "license": ("third_party/dr_mp3/LICENSE.txt", "Licenses/dr_mp3-MIT-0.txt"),
}


def native_audio_asset_files(root):
    declaration = json.loads(
        (root / "export/native-audio-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native audio asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_AUDIO_SOURCES):
        raise RuntimeError("Native audio asset set differs from reviewed content")
    files = {}
    for key, (source_path, destination) in NATIVE_AUDIO_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source_path or \
                record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native audio {key} path")
        source = root / source_path
        if not source.is_file():
            raise RuntimeError(f"Missing native audio {key}: {source}")
        if hashlib.sha256(source.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native audio {key} differs from reviewed content: {source}")
        files[destination] = source
    return files


def _diagnostic(stdout: str):
    match = re.search(r"(?:^|\s)audio=(\{[^\n]+\})(?:\s|$)", stdout)
    if not match:
        raise RuntimeError("Native audio did not report its render evidence")
    try:
        state = json.loads(match.group(1))
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native audio diagnostic is malformed") from error
    for key in ("required", "music", "device", "music_frames", "voices",
                "voiced_chunks", "clipped_samples", "settings", "support"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, int):
            raise RuntimeError(f"Native audio reported invalid {key}")
    for key in ("peak", "master", "music_gain", "sfx_gain"):
        value = state.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise RuntimeError(f"Native audio reported invalid {key}")
    if state["required"] != 1:
        raise RuntimeError("Native client did not decode every required audio stream")
    if state["music"] != 1 or state["music_frames"] <= 48000:
        raise RuntimeError("Native client did not start the decoded music loop")
    if state["voices"] < 1 or state["voiced_chunks"] < 1:
        raise RuntimeError("Native audio produced no polyphonic voice output")
    if state["clipped_samples"] != 0 or not 0 < state["peak"] <= 1:
        raise RuntimeError("Native audio output is not bounded")
    if not (0 <= state["master"] <= 1 and 0 <= state["music_gain"] <= 1 and
            0 <= state["sfx_gain"] <= 1):
        raise RuntimeError("Native audio reported out-of-range volume settings")
    if state["settings"] != 1:
        raise RuntimeError("Native audio settings view did not apply and restore")
    if state["support"] != 1:
        raise RuntimeError("Native support bundle did not export from the menu or F8")
    if abs(state["master"] - .78) > .01 or abs(state["music_gain"] - .64) > .01 \
            or abs(state["sfx_gain"] - .82) > .01:
        raise RuntimeError("Native audio did not persist the restored default mix")
    return state


def _capture(path: Path):
    data = path.read_bytes() if path.is_file() else b""
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"Native audio smoke did not capture a rendered frame: {path}")
    if min(data[54:]) == max(data[54:]):
        raise RuntimeError("Native audio smoke capture contains no rendered variation")
    return data


def validate_native_audio_export(folder: Path, env: dict[str, str]):
    system_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
    clean_env = dict(env, PATH=str(system_root / "System32") +
                     os.pathsep + str(system_root))
    with tempfile.TemporaryDirectory(prefix="stellar-native-audio-") as temporary:
        work = Path(temporary)
        capture = work / "audio-ordered.bmp"
        save = work / "audio.player17.json"
        args = [str(folder / "stellar-continuum-native.exe"),
                "--asset-root", str(folder), "--save-path", str(save),
                "--audio-smoke", str(capture)]
        result = subprocess.run(args, cwd=work, env=clean_env,
                                capture_output=True, text=True,
                                encoding="utf-8", errors="strict", timeout=120)
        if result.returncode != 0:
            raise RuntimeError(
                f"Native audio smoke failed ({result.returncode}):\n"
                f"{result.stdout}\n{result.stderr}")
        if any(token not in result.stdout for token in
               ("gpu_driver=vulkan ", "save=ok ")):
            raise RuntimeError("Native audio smoke did not confirm Vulkan and save")
        state = _diagnostic(result.stdout)
        _capture(capture)
        evidence = folder.parent / f"{folder.name}-{capture.name}"
        shutil.copy2(capture, evidence)
        if not save.is_file():
            raise RuntimeError("Native audio smoke did not write its isolated save")
        payload = json.loads(save.read_text(encoding="utf-8"))
        if payload.get("FormatVersion") != 17:
            raise RuntimeError("Native audio smoke damaged the campaign payload")
        # The settings exercise ends on Done, which persists the restored
        # default mix beside the save path.
        settings = save.parent / "audio-settings.json"
        if not settings.is_file():
            raise RuntimeError("Native audio settings did not persist to disk")
        values = json.loads(settings.read_text(encoding="utf-8"))
        expected = {"master": .78, "music": .64, "sfx": .82}
        for key, default in expected.items():
            value = values.get(key)
            if not isinstance(value, (int, float)) or abs(value - default) > .01:
                raise RuntimeError(f"Native audio persisted wrong {key} volume")
        bundles = sorted((save.parent / "support").glob("*.zip"))
        if not bundles:
            raise RuntimeError("Native support bundle did not land on disk")
        import zipfile
        try:
            with zipfile.ZipFile(bundles[-1]) as bundle:
                names = bundle.namelist()
                valid = (bundle.testzip() is None and len(names) == 3 and
                         any(name.endswith(".json") and "player17" in name
                             for name in names))
        except zipfile.BadZipFile:
            valid = False
        if not valid:
            raise RuntimeError("Native support bundle is not a valid three-entry ZIP")
    return {"nativeAudioStreams": True,
            "nativeAudioDevice": bool(state["device"]),
            "nativeAudioCapture": str(evidence),
            "nativeAudioDiagnostics": result.stdout.strip()}
