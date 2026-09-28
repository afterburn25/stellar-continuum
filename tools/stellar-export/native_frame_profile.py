"""Strict validation for bounded native steady-frame profile diagnostics."""
from __future__ import annotations

import json
import math
import re


_PHASES = ("interval", "update", "scene", "submission", "readback", "throttle", "present")
_METRICS = ("mean_ms", "p50_ms", "p95_ms", "p99_ms", "max_ms")
_COLD_METRICS = ("update_ms", "scene_ms", "submission_ms", "readback_ms",
                 "throttle_ms", "present_ms", "render_present_ms")


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
        raise RuntimeError(f"Native frame profile has invalid {label}")
    return float(value)


def _token_object(stdout: str, token: str) -> dict:
    matches = list(re.finditer(rf"(?<!\S){re.escape(token)}=", stdout))
    if len(matches) != 1:
        raise RuntimeError(f"Native {token} token is missing or duplicated")
    start = matches[0].end()
    try:
        value, end = json.JSONDecoder(object_pairs_hook=_object_without_duplicate_members).raw_decode(stdout[start:])
    except (TypeError, ValueError) as error:
        raise RuntimeError(f"Native {token} JSON is malformed or truncated") from error
    if end == 0 or (start + end < len(stdout) and not stdout[start + end].isspace()):
        raise RuntimeError(f"Native {token} token is malformed or truncated")
    if not isinstance(value, dict):
        raise RuntimeError(f"Native {token} must be a JSON object")
    return value


def validate_steady_profile(stdout: str, expected_samples: int) -> dict:
    """Return the one complete `steady_profile={...}` token in native stdout."""
    validate_profile_frames(expected_samples)
    if expected_samples == 0:
        raise RuntimeError("Native steady profile requires a nonzero sample count")
    profile = _token_object(stdout, "steady_profile")
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


def validate_cold_profile(stdout: str) -> dict:
    """Return the exact first-ten-rendered-frame native cold profile."""
    profile = _token_object(stdout, "cold_profile")
    rows = profile.get("rows")
    if not isinstance(rows, list) or len(rows) != 10:
        raise RuntimeError("Native cold profile must contain exactly ten rows")
    previous_uploads = 0
    for expected_frame, row in enumerate(rows, start=1):
        if not isinstance(row, dict):
            raise RuntimeError("Native cold profile row is not an object")
        if isinstance(row.get("frame"), bool) or not isinstance(row.get("frame"), int) or row["frame"] != expected_frame:
            raise RuntimeError("Native cold profile frames are incomplete or unordered")
        values = {name: _metric(row.get(name), f"cold[{expected_frame}].{name}")
                  for name in _COLD_METRICS}
        if values["readback_ms"] != 0.:
            raise RuntimeError("Native cold profile unexpectedly read back a screenshot")
        for name in ("image_uploads_before", "image_uploads_after"):
            if isinstance(row.get(name), bool) or not isinstance(row.get(name), int) or row[name] < 0:
                raise RuntimeError(f"Native cold profile has invalid {name}")
        if row["image_uploads_after"] < row["image_uploads_before"] or row["image_uploads_before"] < previous_uploads:
            raise RuntimeError("Native cold profile image uploads went backwards")
        previous_uploads = row["image_uploads_after"]
        # Each reported subphase is inside render_present, allowing rounding to
        # three decimal places at both ends of the diagnostic.
        total = sum(values[key] for key in ("submission_ms", "readback_ms", "throttle_ms", "present_ms"))
        if total > values["render_present_ms"] + .003:
            raise RuntimeError("Native cold profile render phases exceed the enclosing draw")
    return profile
