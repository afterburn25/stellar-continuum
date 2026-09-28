#!/usr/bin/env python3
"""Reject incomplete, stale, corrupt, or internally inconsistent Player expedition evidence."""
import argparse
import hashlib
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path

from validate_godot_smoke import validate_log

REQUIRED_CAPTURES = {
    "player-expedition-01-opening-research.png",
    "player-expedition-02-first-warp-shipyard.png",
    "player-expedition-03-settlement-authorized.png",
    "player-expedition-04-reloaded-colony.png",
}
REQUIRED_CHECKS = {
    "player-expedition-fresh-ordinary-sandbox",
    "player-expedition-active-research-control-retains-focus-and-refreshes-progress",
    "player-expedition-research-pointer-pause-halts-canonical-spend",
    "player-expedition-paused-research-progress-remains-stable",
    "player-expedition-research-pointer-resume-restores-canonical-progress-and-spend",
    "player-expedition-pauses-active-research-for-shipbuilding-capital",
    "player-expedition-first-warp-completed",
    "player-expedition-colony-transit-right-click-order",
    "player-expedition-colony-body-right-click-authorizes-settlement",
    "player-expedition-authorization-save-preserves-ship-id-target-and-embarked-people",
    "player-expedition-exact-body-founded-and-colony-ship-consumed",
    "player-expedition-settlement-observes-canonical-timer",
    "player-expedition-settlement-timed-and-complete",
    "player-expedition-real-load-restores-saved-day-people-ships-and-campaign-instance",
}
REQUIRED_CHECKS.update(f"player-expedition-build-{design}" for design in
                       ("warp_scout", "science_vessel", "colony_ship"))
REQUIRED_CHECKS.update(f"player-expedition-construction-{project}" for project in
                       ("research_network", "industrial_automation", "orbital_launch_complex",
                        "orbital_shipyard", "warp_test_facility"))
REQUIRED_CHECKS.update(f"player-expedition-research-{research}" for research in
                       ("in_space_assembly", "asteroid_prospecting", "asteroid_mining", "vacuum_refining",
                        "orbital_manufacturing", "orbital_shipyard", "gravitational_physics", "field_theory",
                        "warp_metric_theory", "exotic_energy_coupling", "micro_field_distortion",
                        "warp_field_control", "prototype_warp_drive"))
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
COMPLETION = "STELLAR_FOCUSED_PLAYER_EXPEDITION_COMPLETE"


def png_size(data: bytes) -> tuple[int, int]:
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("invalid PNG signature")
    offset = len(PNG_SIGNATURE)
    dimensions = None
    compressed = bytearray()
    ended = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("truncated PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 12 + size
        if end > len(data):
            raise ValueError("truncated PNG payload")
        payload = data[offset + 8:offset + 8 + size]
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != crc:
            raise ValueError("PNG CRC mismatch")
        if dimensions is None:
            if kind != b"IHDR" or size != 13:
                raise ValueError("PNG must begin with IHDR")
            width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if (width, height) != (1280, 720) or depth != 8 or color not in (2, 6) or compression or filtering or interlace:
                raise ValueError("expected a non-interlaced 1280x720 8-bit RGB/RGBA capture")
            dimensions = (width, height, 3 if color == 2 else 4)
        elif kind == b"IHDR":
            raise ValueError("duplicate PNG header")
        if kind == b"IDAT":
            compressed.extend(payload)
        if kind == b"IEND":
            if payload or end != len(data):
                raise ValueError("invalid PNG end")
            ended = True
            break
        offset = end
    if not ended or dimensions is None or not compressed:
        raise ValueError("incomplete PNG")
    width, height, channels = dimensions
    expected_size = height * (1 + width * channels)
    decoder = zlib.decompressobj()
    decoded = decoder.decompress(bytes(compressed), expected_size + 1)
    if len(decoded) != expected_size or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("truncated or oversized PNG image data")
    stride = 1 + width * channels
    if any(decoded[row * stride] > 4 for row in range(height)):
        raise ValueError("invalid PNG row filter")
    return width, height


def fail(message: str) -> None:
    raise SystemExit("player expedition capture invalid: " + message)


def find_galaxy(node):
    if isinstance(node, dict):
        if isinstance(node.get("Fleets"), list) and isinstance(node.get("Colonies"), list):
            return node
        for value in node.values():
            found = find_galaxy(value)
            if found is not None:
                return found
    return None


parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--expected-sha")
args = parser.parse_args()
try:
    manifest = json.loads((args.directory / "player-expedition-manifest.json").read_text(encoding="utf-8"))
except (OSError, ValueError) as error:
    fail(f"cannot read manifest: {error}")

if manifest.get("schema_version") != 3 or manifest.get("seed") != "20260908" or \
        manifest.get("system_count") != 500 or manifest.get("player_mode") is not True:
    fail("evidence is not the fixed ordinary 500-system Player Sandbox schema")
git_sha = manifest.get("git_sha")
if not isinstance(git_sha, str) or not re.fullmatch(r"[0-9a-f]{40}", git_sha):
    fail("manifest does not identify an exact lowercase Git commit")
if args.expected_sha and git_sha != args.expected_sha:
    fail("git SHA does not match the requested revision")
if manifest.get("input_mode") != "Input.ParseInputEvent" or "no Developer mode" not in manifest.get("scope", ""):
    fail("manifest does not prove the ordinary real-input scope")

checks = manifest.get("checks")
if not isinstance(checks, list) or any(not isinstance(check, str) for check in checks) or len(checks) != len(set(checks)):
    fail("check evidence is malformed or duplicated")
missing = REQUIRED_CHECKS - set(checks)
if missing:
    fail("missing checks: " + ", ".join(sorted(missing)))
if not any(check.startswith("player-expedition-scout-right-click-order-") for check in checks) or \
        not any(check.startswith("player-expedition-science-right-click-order-") for check in checks):
    fail("expedition lacks real scout and science map orders")

authorization = manifest.get("authorization_save")
settlement = manifest.get("settlement")
reload = manifest.get("reload")
if not isinstance(authorization, dict) or authorization.get("captured_while_paused") is not True or \
        not isinstance(authorization.get("save_bytes"), int) or authorization["save_bytes"] < 4096 or \
        not re.fullmatch(r"[0-9a-f]{64}", str(authorization.get("save_sha256", ""))) or \
        float(authorization.get("embarked_population_millions", 0)) <= 0:
    fail("paused settlement-authorization save evidence is incomplete")
authorization_file = authorization.get("file")
if authorization_file != "player-expedition-authorization-save.json":
    fail("authorization save artifact name is missing or unexpected")
try:
    authorization_bytes = (args.directory / authorization_file).read_bytes()
except OSError as error:
    fail(f"cannot read preserved authorization save: {error}")
if len(authorization_bytes) != authorization["save_bytes"] or \
        hashlib.sha256(authorization_bytes).hexdigest() != authorization["save_sha256"]:
    fail("preserved authorization save bytes do not match its manifest receipt")
try:
    saved_galaxy = find_galaxy(json.loads(authorization_bytes))
    saved_fleet = next(fleet for fleet in saved_galaxy["Fleets"] if fleet.get("Id") == authorization["fleet_id"])
except (TypeError, ValueError, KeyError, StopIteration) as error:
    fail(f"preserved authorization save lacks its exact fleet: {error}")
if saved_fleet.get("DestinationPlanetaryBodyId") != authorization["body_id"] or \
        saved_fleet.get("SettlementBodyId") is not None or saved_fleet.get("SettlementDaysCompleted") != 0 or \
        abs(float(saved_fleet.get("EmbarkedPopulationMillions", 0)) -
            float(authorization["embarked_population_millions"])) > 1e-6:
    fail("preserved save does not contain the authorized body and embarked population receipt")
if not isinstance(settlement, dict) or settlement.get("colony_ship_consumed") is not True or \
        settlement.get("observation_paused") is not True or \
        settlement.get("colonies_after") != settlement.get("colonies_before", -1) + 1 or \
        settlement.get("fleet_id") != authorization.get("fleet_id") or \
        settlement.get("body_id") != authorization.get("body_id") or \
        settlement.get("authorized_population_millions") != authorization.get("embarked_population_millions") or \
        float(settlement.get("observed_population_millions", 0)) < float(authorization["embarked_population_millions"]) or \
        float(settlement.get("observed_population_millions", 0)) - float(authorization["embarked_population_millions"]) > \
            float(authorization["embarked_population_millions"]) * .001 or \
        not 29 <= float(settlement.get("observed_simulation_days", -1)) - float(settlement.get("authorization_simulation_days", 0)) <= 35:
    fail("founded-colony evidence does not match the authorized body, population, and consumed ship")
if not isinstance(reload, dict) or reload.get("restored_paused") is not True or \
        type(reload.get("seed")) is not int or reload["seed"] != 20260908 or \
        type(reload.get("application_revision_before")) is not int or \
        type(reload.get("application_revision_after")) is not int or \
        reload["application_revision_after"] <= reload["application_revision_before"]:
    fail("saved-campaign reload lacks a distinct successful campaign application")
try:
    saved_day = float(reload["saved_simulation_days"])
    unsaved_day = float(reload["unsaved_advanced_simulation_days"])
    restored_day = float(reload["restored_simulation_days"])
except (KeyError, TypeError, ValueError) as error:
    fail(f"saved-campaign reload timing is malformed: {error}")
if not all(math.isfinite(value) and value >= 0 for value in (saved_day, unsaved_day, restored_day)) or \
        unsaved_day <= saved_day or abs(restored_day - saved_day) > 1e-6:
    fail("saved-campaign reload did not roll visible unsaved time back to the exact saved day")

captures = manifest.get("captures")
if not isinstance(captures, list) or any(not isinstance(capture, dict) for capture in captures):
    fail("capture evidence is malformed")
names = [capture.get("file") for capture in captures]
if len(names) != len(REQUIRED_CAPTURES) or set(names) != REQUIRED_CAPTURES:
    fail("capture list is missing, duplicated, or contains unexpected views")
actual_hashes = set()
for capture in captures:
    path = args.directory / capture["file"]
    try:
        data = path.read_bytes()
        dimensions = png_size(data)
    except (OSError, ValueError, zlib.error, struct.error) as error:
        fail(f"{capture['file']}: {error}")
    digest = hashlib.sha256(data).hexdigest()
    if len(data) < 4096 or capture.get("bytes") != len(data) or capture.get("sha256") != digest or \
            (capture.get("width"), capture.get("height")) != dimensions:
        fail(f"{capture['file']}: manifest length, dimensions, or SHA-256 disagrees with actual PNG")
    actual_hashes.add(digest)
if len(actual_hashes) != len(REQUIRED_CAPTURES):
    fail("two required views contain identical image bytes")

elapsed = manifest.get("elapsed_wall_seconds")
days = manifest.get("simulation_days")
if not isinstance(elapsed, (int, float)) or not 0 < elapsed <= 22 * 60 or \
        not isinstance(days, (int, float)) or not 5900 <= days <= 7500:
    fail("expedition is outside the bounded ordinary opening horizon")

log_files = sorted(args.directory.glob("godot*.log"))
if not log_files:
    fail("runtime/import logs are missing")
runtime_log = None
for log_path in log_files:
    content = log_path.read_text(encoding="utf-8", errors="replace")
    if COMPLETION in {line.strip() for line in content.splitlines()}:
        runtime_log = content
    failures = validate_log(content, require_runtime_ready=False)
    if failures:
        fail(f"{log_path.name}: {'; '.join(failures)}")
if runtime_log is None:
    fail("runtime log lacks the focused completion marker")
runtime_failures = validate_log(runtime_log, require_runtime_ready=True)
if runtime_failures:
    fail("runtime log failed startup validation: " + "; ".join(runtime_failures))
runtime_lines = {line.strip() for line in runtime_log.splitlines()}
for check in REQUIRED_CHECKS:
    if f"STELLAR_UI_CHECK_PASS {check}" not in runtime_lines:
        fail("runtime log lacks check marker: " + check)
mouse_lines = [line for line in runtime_log.splitlines() if line.startswith("STELLAR_MOUSE_INPUT ")]
if manifest.get("mouse_actions") != len(mouse_lines) or len(mouse_lines) < 30:
    fail("real mouse input count is insufficient or inconsistent with runtime log")
if (args.directory / "failure.png").exists():
    fail("failure capture is present")

print("player expedition capture evidence valid")
