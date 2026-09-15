"""Strict validation for bounded native steady-frame profile diagnostics."""
from __future__ import annotations

import json
import math
import re


_PHASES = ("interval", "update", "scene", "submission", "readback", "throttle", "present")
_METRICS = ("mean_ms", "p50_ms", "p95_ms", "p99_ms", "max_ms")


def validate_profile_frames(samples: int) -> None:
    if isinstance(samples, bool) or not isinstance(samples, int) or not 0 <= samples <= 3600 or \
            samples not in (0,) and samples < 120:
        raise RuntimeError("Native profile frames must be 0 or an integer from 120 to 3600")


def _object_without_duplicate_members(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f"duplicate JSON member {key}")
        value[key] = item
    return value


def _metric(value, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
        raise RuntimeError(f"Native steady profile has invalid {label}")
    return float(value)


def validate_steady_profile(stdout: str, expected_samples: int) -> dict:
    """Return the one complete `steady_profile={...}` token in native stdout."""
    validate_profile_frames(expected_samples)
    if expected_samples == 0:
        raise RuntimeError("Native steady profile requires a nonzero sample count")
    matches = list(re.finditer(r"(?<!\S)steady_profile=", stdout))
    if len(matches) != 1:
        raise RuntimeError("Native steady profile token is missing or duplicated")
    start = matches[0].end()
    try:
        profile, end = json.JSONDecoder(object_pairs_hook=_object_without_duplicate_members).raw_decode(stdout[start:])
    except (TypeError, ValueError) as error:
        raise RuntimeError("Native steady profile JSON is malformed or truncated") from error
    if end == 0 or (start + end < len(stdout) and not stdout[start + end].isspace()):
        raise RuntimeError("Native steady profile token is malformed or truncated")
    if not isinstance(profile, dict):
        raise RuntimeError("Native steady profile must be a JSON object")
    if isinstance(profile.get("samples"), bool) or not isinstance(profile.get("samples"), int) or \
            profile.get("samples") != expected_samples:
        raise RuntimeError("Native steady profile has the wrong sample count")
    for phase in _PHASES:
        values = profile.get(phase)
        if not isinstance(values, dict):
            raise RuntimeError(f"Native steady profile lacks {phase} metrics")
        metrics = {metric: _metric(values.get(metric), f"{phase}.{metric}")
                   for metric in _METRICS}
        if not metrics["p50_ms"] <= metrics["p95_ms"] <= metrics["p99_ms"] <= metrics["max_ms"] or \
                metrics["mean_ms"] > metrics["max_ms"]:
            raise RuntimeError(f"Native steady profile has unordered {phase} metrics")
        if phase == "readback" and any(value != 0. for value in metrics.values()):
            raise RuntimeError("Native steady profile readback must be zero")
    return profile
