"""Strict evidence parser for the opt-in native scientist voice smoke check."""
from __future__ import annotations

import hashlib
import json
import re


_FIELDS = {"available", "played", "unknown_denied", "overlap_prevented",
           "queue_bounded", "stopped"}


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def parse_native_voice_check(stdout: str) -> dict:
    rows = re.findall(r"^voice_check=(.*)$", stdout, flags=re.MULTILINE)
    if len(rows) != 1:
        raise RuntimeError("Native scientist voice check did not report exactly one diagnostic")
    try:
        state = json.loads(rows[0], object_pairs_hook=_unique_object)
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native scientist voice check diagnostic is malformed") from error
    if not isinstance(state, dict) or set(state) != _FIELDS:
        raise RuntimeError("Native scientist voice check diagnostic has an unexpected schema")
    for field in ("available", "unknown_denied", "overlap_prevented", "queue_bounded", "stopped"):
        if state.get(field) is not True:
            raise RuntimeError(f"Native scientist voice check did not prove {field}")
    played = state.get("played")
    if isinstance(played, bool) or not isinstance(played, int) or played < 1:
        raise RuntimeError("Native scientist voice check reported invalid played")
    return state


# Native voice catalogue packaging: the reviewed Data/voice_profiles set is
# sealed by export/native-voice-assets.json (exact paths + SHA-256) and
# mirrored by cmake/NativeVoiceAssets.cmake for the build tree.

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
