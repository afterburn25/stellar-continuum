"""Strict evidence parser for the opt-in native scientist voice smoke check."""
from __future__ import annotations

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
