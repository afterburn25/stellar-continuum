"""Native voice catalogue packaging: the reviewed Data/voice_profiles set is
sealed by export/native-voice-assets.json (exact paths + SHA-256) and mirrored
by cmake/NativeVoiceAssets.cmake for the build tree."""

import hashlib
import json
from pathlib import Path


NATIVE_VOICE_SOURCES = {
    "events": ("data/voice_profiles/events.json",
               "Data/voice_profiles/events.json"),
    "human": ("data/voice_profiles/human.json",
              "Data/voice_profiles/human.json"),
    "roles": ("data/voice_profiles/roles.json",
              "Data/voice_profiles/roles.json"),
}


def native_voice_asset_files(root):
    declaration = json.loads(
        (root / "export/native-voice-assets.json").read_text(encoding="utf-8"))
    if declaration.get("schemaVersion") != 1:
        raise RuntimeError("Unsupported native voice asset declaration")
    records = declaration.get("assets", {})
    if not isinstance(records, dict) or set(records) != set(NATIVE_VOICE_SOURCES):
        raise RuntimeError("Native voice asset set differs from reviewed content")
    files = {}
    for key, (source_path, destination) in NATIVE_VOICE_SOURCES.items():
        record = records[key]
        if not isinstance(record, dict) or record.get("source") != source_path or \
                record.get("runtimePath") != destination:
            raise RuntimeError(f"Unreviewed native voice {key} path")
        source = root / source_path
        if not source.is_file():
            raise RuntimeError(f"Missing native voice {key}: {source}")
        if hashlib.sha256(source.read_bytes()).hexdigest() != record.get("sha256"):
            raise RuntimeError(f"Native voice {key} differs from reviewed content: {source}")
        files[destination] = source
    return files
