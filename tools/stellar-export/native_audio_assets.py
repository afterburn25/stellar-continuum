"""Exact approved native audio; no implicit directory-wide asset packaging."""
from __future__ import annotations

import hashlib
import json


NATIVE_AUDIO_SOURCES = {
    "main-music": ("assets/audio/music/claimed-by-the-void-loop.mp3",
                   "assets/audio/music/claimed-by-the-void-loop.mp3"),
    "ui-hover": ("assets/audio/sfx/ui-hover.wav", "assets/audio/sfx/ui-hover.wav"),
    "ui-confirm": ("assets/audio/sfx/ui-confirm.wav", "assets/audio/sfx/ui-confirm.wav"),
    "discovery-reveal": ("assets/audio/sfx/discovery-reveal.wav", "assets/audio/sfx/discovery-reveal.wav"),
    "construction-complete": ("assets/audio/sfx/construction-complete.wav", "assets/audio/sfx/construction-complete.wav"),
    "ship-launch": ("assets/audio/sfx/ship-launch.wav", "assets/audio/sfx/ship-launch.wav"),
    "strategic-alert": ("assets/audio/sfx/strategic-alert.wav", "assets/audio/sfx/strategic-alert.wav"),
    "credits": ("docs/engine/NATIVE_AUDIO_SOURCES.md", "Licenses/Audio-sources.md"),
}


def native_audio_asset_files(root):
    declaration = json.loads((root / "export/native-audio-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native audio asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_AUDIO_SOURCES):
        raise RuntimeError("Native audio asset set differs from reviewed content")
    files = {}
    for key, (source, destination) in NATIVE_AUDIO_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source or record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native audio {key} path")
        path = root / source
        if not path.is_file():
            raise RuntimeError(f"Missing native audio {key}: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native audio {key} differs from reviewed content: {path}")
        files[destination] = path
    return files
