"""Strict evidence parser for the native audio-settings smoke check."""
from __future__ import annotations

import json
import math
import re


_FIELDS = {
    "context", "opened", "previewed", "muted", "cancel_restored", "saved",
    "reopened", "master", "music", "effects", "initial_master",
    "initial_music", "initial_effects", "initial_muted",
}
_SETTINGS_FIELDS = {"schemaVersion", "master", "music", "effects", "muted"}
_EPSILON = 1e-5


def _object_without_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate object field")
        result[key] = value
    return result


def _json_object(text: str, label: str) -> dict:
    try:
        value = json.loads(text, object_pairs_hook=_object_without_duplicates)
    except (TypeError, ValueError, UnicodeDecodeError) as error:
        raise RuntimeError(f"Native audio settings {label} is malformed") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"Native audio settings {label} is not an object")
    return value


def _float(value, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RuntimeError(f"Native audio settings reported invalid {field}")
    try:
        normalized = float(value)
    except OverflowError as error:
        raise RuntimeError(f"Native audio settings reported invalid {field}") from error
    if not math.isfinite(normalized):
        raise RuntimeError(f"Native audio settings reported invalid {field}")
    return normalized


def _expect_float(value, expected: float, field: str):
    if abs(_float(value, field) - expected) > _EPSILON:
        raise RuntimeError(f"Native audio settings reported wrong {field}")


def parse_native_audio_settings_check(stdout: str, *, fresh: bool) -> dict:
    """Parse the one-line proof emitted by ``--audio-settings-check``."""
    rows = re.findall(r"^audio_settings_check=(.*)$", stdout, flags=re.MULTILINE)
    if len(rows) != 1:
        raise RuntimeError("Native audio settings did not report exactly one diagnostic")
    state = _json_object(rows[0], "diagnostic")
    if set(state) != _FIELDS:
        raise RuntimeError("Native audio settings diagnostic has an unexpected schema")
    if state.get("context") != ("startup" if fresh else "pause"):
        raise RuntimeError("Native audio settings diagnostic used the wrong context")
    for field in ("opened", "previewed", "muted", "cancel_restored", "saved", "reopened"):
        if state.get(field) is not True:
            raise RuntimeError(f"Native audio settings did not prove {field}")
    if state.get("initial_muted") is not False:
        raise RuntimeError("Native audio settings did not begin unmuted")
    for field, expected in (("master", .25), ("music", .5), ("effects", .75),
                            ("initial_master", .78 if fresh else .25),
                            ("initial_music", .64 if fresh else .5),
                            ("initial_effects", .82 if fresh else .75)):
        _expect_float(state.get(field), expected, field)
    return state


def verify_native_audio_settings_file(path) -> bytes:
    """Validate the deterministic persisted settings document and return its bytes."""
    try:
        with path.open("rb") as source:
            data = source.read(4097)
    except OSError as error:
        raise RuntimeError("Native audio settings did not persist its settings file") from error
    if len(data) > 4096:
        raise RuntimeError("Native audio settings persistence exceeds 4 KiB")
    try:
        text = data.decode("utf-8")
    except UnicodeError as error:
        raise RuntimeError("Native audio settings persistence is not UTF-8") from error
    state = _json_object(text, "persistence")
    if (set(state) != _SETTINGS_FIELDS or type(state.get("schemaVersion")) is not int or
            state.get("schemaVersion") != 1):
        raise RuntimeError("Native audio settings persistence has an unexpected schema")
    if state.get("muted") is not False:
        raise RuntimeError("Native audio settings persistence did not restore mute state")
    for field, expected in (("master", .25), ("music", .5), ("effects", .75)):
        _expect_float(state.get(field), expected, field)
    return data
