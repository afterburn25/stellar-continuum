#!/usr/bin/env python3
"""Validate the focused, real-input startup-failure recovery-screen evidence."""
import argparse
import hashlib
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path


REQUIRED_CHECKS = {
    "startup-failure-pauses-partial-world",
    "startup-failure-layer-shields-partial-world",
    "startup-failure-exit-is-readable-and-reachable",
    "startup-failure-backdrop-blocks-gameplay-input",
    "startup-failure-preserves-campaign-save-before-exit",
}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def fail(message: str) -> None:
    raise ValueError(message)


def png_size(data: bytes) -> tuple[int, int]:
    if not data.startswith(PNG_SIGNATURE):
        fail("invalid PNG signature")
    offset = len(PNG_SIGNATURE)
    dimensions = None
    compressed = bytearray()
    ended = False
    while offset < len(data):
        if offset + 12 > len(data):
            fail("truncated PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 12 + size
        if end > len(data):
            fail("truncated PNG payload")
        payload = data[offset + 8:offset + 8 + size]
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != crc:
            fail("PNG CRC mismatch")
        if dimensions is None:
            if kind != b"IHDR" or size != 13:
                fail("PNG must begin with IHDR")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if depth != 8 or color not in (2, 6) or compression or filtering or interlace:
                fail("expected non-interlaced 8-bit RGB/RGBA PNG")
            dimensions = (width, height, 3 if color == 2 else 4)
        if kind == b"IDAT":
            compressed.extend(payload)
        if kind == b"IEND":
            if payload or end != len(data):
                fail("invalid PNG end")
            ended = True
            break
        offset = end
    if not ended or dimensions is None or not compressed:
        fail("incomplete PNG")
    width, height, channels = dimensions
    expected_size = height * (1 + width * channels)
    decoder = zlib.decompressobj()
    decoded = decoder.decompress(bytes(compressed), expected_size + 1)
    if len(decoded) != expected_size or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        fail("truncated or oversized PNG image data")
    stride = 1 + width * channels
    if any(decoded[row * stride] > 4 for row in range(height)):
        fail("invalid PNG row filter")
    return width, height


def validate(directory: Path, save_path: Path, log_path: Path, support_directory: Path,
             expected_sha: str) -> None:
    evidence_path = directory / "startup-failure-ui.json"
    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    if evidence.get("schema_version") != 1:
        fail("startup-failure evidence schema is not 1")
    if not re.fullmatch(r"[0-9a-f]{40}", expected_sha) or evidence.get("git_sha") != expected_sha:
        fail("startup-failure evidence is not bound to the expected source SHA")
    if evidence.get("scope") != "actual IntegratedMain startup failure at 1280x720":
        fail("startup-failure evidence has the wrong scope")
    if evidence.get("injection") != "--stellar-startup-failure-ui":
        fail("startup-failure evidence did not use the actual IntegratedMain injection")
    if evidence.get("input_mode") != "Input.ParseInputEvent" or evidence.get("exit_activated") is not True:
        fail("the actual Exit safely button was not activated through real input")
    if evidence.get("viewport") != {"width": 1280, "height": 720}:
        fail("startup-failure evidence was not captured at 1280x720")
    bounds = evidence.get("exit_bounds", {})
    values = [bounds.get(key) for key in ("x", "y", "width", "height")]
    if not all(isinstance(value, (int, float)) and math.isfinite(value) for value in values):
        fail("Exit safely bounds are missing")
    x, y, width, height = values
    if x < 0 or y < 0 or width < 180 or height < 44 or x + width > 1280 or y + height > 720:
        fail("Exit safely is clipped or below its required target size")
    checks = evidence.get("checks")
    if not isinstance(checks, list) or len(checks) != len(set(checks)) or not REQUIRED_CHECKS.issubset(checks):
        fail("startup-failure evidence is missing required unique checks")

    capture = evidence.get("capture", {})
    if capture.get("file") != "startup-failure-ui.png":
        fail("startup-failure capture filename is wrong")
    png = (directory / capture["file"]).read_bytes()
    if len(png) < 4096 or not png.startswith(PNG_SIGNATURE):
        fail("startup-failure screenshot is missing or invalid")
    if png_size(png) != (1280, 720):
        fail("startup-failure screenshot dimensions are not 1280x720")
    if capture.get("bytes") != len(png) or capture.get("sha256") != hashlib.sha256(png).hexdigest():
        fail("startup-failure screenshot bytes or hash do not match the evidence")

    save = evidence.get("save", {})
    save_bytes = save_path.read_bytes()
    if save.get("file") != "autosave.json" or save.get("bytes") != len(save_bytes):
        fail("isolated save size changed during the startup-failure run")
    if save.get("sha256") != hashlib.sha256(save_bytes).hexdigest():
        fail("isolated save bytes changed during the startup-failure run")

    exit_text = (directory / "exit-code.txt").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"1", exit_text):
        fail("startup-failure process did not record the required exit code 1")

    log = log_path.read_text(encoding="utf-8", errors="replace")
    required_log = (
        "Integrated campaign initialization failed.",
        "Requested startup failure smoke.",
        "Deterministic nested startup failure evidence.",
        "startupSeed=",
        "playerSave=",
        "developerSave=",
        "STELLAR_FOCUSED_STARTUP_FAILURE_UI_READY",
        "STELLAR_FOCUSED_STARTUP_FAILURE_EXIT_ACTIVATED",
    )
    for marker in required_log:
        if marker not in log:
            fail(f"Godot log is missing {marker!r}")
    forbidden = (
        "STELLAR_RUNTIME_READY", "STELLAR_SCREENSHOT_CAPTURE_COMPLETE",
        "Screenshot capture failed", "Startup failure presentation also failed",
        "Startup diagnostic logging also failed", "Startup audio cleanup",
        "Cannot instantiate C# script", "Unhandled exception",
    )
    for marker in forbidden:
        if marker in log:
            fail(f"Godot log contains unexpected marker {marker!r}")
    error_lines = [line.strip() for line in log.splitlines()
                   if re.match(r"\s*(?:SCRIPT |USER |FATAL )?ERROR:", line)]
    if not error_lines or any("Integrated campaign initialization failed." not in line for line in error_lines):
        fail("Godot emitted errors beyond the intended startup failure")

    support_logs = sorted(support_directory.glob("game-*.log"))
    fatal_logs = [path for path in support_logs
                  if "[startup-fatal]" in path.read_text(encoding="utf-8", errors="replace")]
    if len(fatal_logs) != 1:
        fail("expected exactly one isolated startup-fatal support log")
    support = fatal_logs[0].read_text(encoding="utf-8", errors="replace")
    if "[startup-fatal]" not in support or any(marker not in support for marker in required_log[:6]):
        fail("support log does not preserve the full nested startup diagnostic and paths")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--support-directory", type=Path, required=True)
    parser.add_argument("--expected-sha", required=True)
    args = parser.parse_args()
    try:
        validate(args.directory, args.save, args.log, args.support_directory, args.expected_sha)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"Startup-failure capture rejected: {error}", file=sys.stderr)
        return 1
    print(f"Startup-failure capture validated: {args.directory}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
