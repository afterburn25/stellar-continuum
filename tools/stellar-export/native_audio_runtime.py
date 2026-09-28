"""Strict evidence parser for the opt-in native audio smoke check."""
from __future__ import annotations

import json
import re


_FIELDS = {"assets_loaded", "music_starts", "confirm_count", "queued_music_bytes",
           "boot_services", "stopped"}


def parse_native_audio_check(stdout: str, *, fresh: bool) -> dict:
    rows = re.findall(r"^audio_check=(.*)$", stdout, flags=re.MULTILINE)
    if len(rows) != 1:
        raise RuntimeError("Native audio check did not report exactly one diagnostic")
    try:
        state = json.loads(rows[0])
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native audio check diagnostic is malformed") from error
    if not isinstance(state, dict) or set(state) != _FIELDS:
        raise RuntimeError("Native audio check diagnostic has an unexpected schema")
    if state.get("assets_loaded") is not True or state.get("stopped") is not True:
        raise RuntimeError("Native audio check did not load assets and stop cleanly")
    for field in ("music_starts", "confirm_count", "queued_music_bytes", "boot_services"):
        value = state.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise RuntimeError(f"Native audio check reported invalid {field}")
    if state["music_starts"] != 1:
        raise RuntimeError("Native audio check did not start music exactly once")
    if not 1 <= state["queued_music_bytes"] <= 288000:
        raise RuntimeError("Native audio check reported an invalid queued music size")
    if fresh and (state["boot_services"] <= 0 or state["confirm_count"] <= 0):
        raise RuntimeError("Native audio fresh startup did not initialize services and confirm input")
    return state
