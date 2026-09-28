#!/usr/bin/env python3
"""Validate the exact rendered UI capture contract, input proof, PNGs, and runtime logs."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

from validate_godot_smoke import validate_log

CAPTURE_DIMENSIONS = {
    "30-responsive-1080p.png": (1920, 1080), "31-responsive-1440p.png": (2560, 1440),
    "32-responsive-4k.png": (3840, 2160),
}
CAPTURES = (
    "production-economy-priority.png", "production-ship-queue-720p.png",
    "26-system-sky-sol.png", "27-system-sky-variant.png", "28-selected-ship-route.png", "29-orbital-shipyard.png",
    "30-responsive-1080p.png", "31-responsive-1440p.png", "32-responsive-4k.png", "33-responsive-720p.png",
    "01-main-menu.png", "01a-new-game-options.png", "02-region-map.png", "03-research-card.png",
    "04-industry-card.png", "05-relations.png", "06-demo-confirmation.png", "01b-audio-settings.png",
    "07-demo-guidance.png", "08-ships-card.png", "09-colonies.png",
    "10-system-planets.png", "11-region-map-demo.png", "12-menu-drawer.png",
    "13-earth-selected.png", "14-galaxy-overview.png", "15-zoomed-region.png",
    "15b-system-overview.png",
    "16-earth-focus.png", "17-surface-placement.png", "18-surface-colony.png", "19-developer-tools.png",
    "20-economy.png",
    "21-mars-surface.png",
    "22-shipyard-artwork.png",
)
SECTIONS = ("economy", "research", "industry", "ships", "explore", "colonies", "inspection",
            "logistics", "relations", "menu")
REQUIRED_CHECKS = {
    "normal-startup-menu-paused", "menu-blocks-gameplay-keyboard", "cinematic-splash-loading-present",
    "menu-blocks-gameplay-pointer", "continue-resumes-normal-campaign",
    "audio-settings-and-original-score-present",
    "audio-streams-survive-repeated-garbage-collection",
    "guided-expedition-pacing-visible",
    "navigation-default-closed", "drawer-close-returns-map", "controls-fit-1280x720",
    "map-selection-positive-control", "map-order-positive-control",
    "drawer-blocks-map-selection", "drawer-blocks-map-orders",
    "rail-blocks-map-input", "dock-blocks-map-input",
    "developer-confirmation-wraps-inside-viewport", "cancel-developer-preserves-player-campaign",
    "confirm-starts-developer-at-24x", "developer-guidance-visible-with-objective",
    "research-card-starts-project", "research-horizon-hides-unknown-possibilities", "industry-card-starts-project",
    "notification-center-retains-player-orders",
    "accepted-actions-trigger-visual-feedback",
    "exploration-page-uses-visual-mission-state",
    "logistics-page-uses-visual-network-state",
    "relations-page-uses-visual-contact-state",
    "inspection-page-uses-visual-intelligence-state",
    "early-game-shipyard-locks-cleanly", "named-ship-design-starts-build", "home-selects-known-star",
    "open-system-enters-home-orbits", "command-feedback-visible-over-system-view",
    "back-to-region-preserves-selection", "menu-preserves-developer-state",
    "resume-restores-developer-speed", "player-save-unchanged-by-developer",
    "normal-human-earth-sol-start", "developer-human-earth-sol-start", "sol-catalog-worlds-visible",
    "home-orbit-shows-infrastructure-plan",
    "orbital-infrastructure-opens-industry",
    "locked-orbital-infrastructure-explains-requirements",
    "earth-selected-by-mouse", "developer-sol-identity-survives-reload",
    "icon-only-controls-visible", "project-icons-crisp",
    "economy-page-reconciles-live-cash-flow",
    "owned-colony-land-opens-surface",
    "human-sol-starting-settlements-visible",
    "civilization-portraits-load-in-real-runtime",
}
REQUIRED_CHECKS.update(f"drawer-{section}-exclusive" for section in SECTIONS)
CAMERA_CHECKS = {
    "galaxy-overview-reachable-by-wheel", "galaxy-overview-shows-public-catalog",
    "galaxy-overview-shows-distant-galaxy-field",
    "regional-wheel-button-zoom-parity", "galaxy-region-zoom-roundtrip-restores",
    "regional-map-enlarged-stars-and-local-sky",
    "regional-pan-inverse-hit", "drawer-blocks-camera-wheel",
    "system-wheel-button-zoom-parity", "system-pan-inverse-hit",
    "planet-focus-by-real-double-click", "planet-focus-back-restores-system-camera",
    "system-back-restores-region-camera", "unknown-system-entry-preserves-privacy",
    "unknown-body-materials-redacted", "resize-preserves-star-hit",
    "resize-preserves-body-hit", "resize-restores-minimum-layout",
    "camera-transitions-settle-smoothly",
    "focused-menu-blocks-camera", "planet-wheel-button-route-parity",
    "system-wheel-keeps-optional-planet-focus",
    "wheel-enters-system-and-restores-region",
}
REQUIRED_CHECKS.update(CAMERA_CHECKS)
SURFACE_CHECKS = {
    "earth-surface-opens-from-real-breadcrumb", "surface-controls-fit-1280x720",
    "surface-compact-playback-visible",
    "surface-build-palette-collapses-by-default",
    "surface-build-palette-preserves-world-view",
    "surface-world-palette-from-environment",
    "surface-camera-input-and-hud-shielding", "surface-valid-free-placement-preview",
    "surface-real-ground-click-places-unfunded-site", "surface-collision-rejected-without-charge",
    "surface-save-keeps-normal-campaign-separate", "surface-back-restores-orbit-without-map-input",
    "surface-ordinary-progress-completes-powered-buildings", "surface-real-save-reload-retains-buildings",
    "surface-output-visible-and-authoritative",
    "surface-trade-hub-placed-through-real-palette",
    "surface-building-selection-and-cancellation",
    "surface-building-upgrade-through-real-selection",
    "mars-small-settlement-opens-without-invented-city",
    "mars-habitat-placed-through-real-build-menu",
}
REQUIRED_CHECKS.update(SURFACE_CHECKS)
MODE_CHECKS = {
    "player-mode-tools-unavailable", "mode-menu-controls-fit-1280x720", "developer-opening-tools-unused",
    "new-game-choice-presents-locked-story-and-sandbox",
    "sandbox-setup-fits-and-precedes-confirmation",
    "mode-roundtrip-preserves-independent-campaigns", "developer-tools-open-without-automatic-command",
    "developer-tools-block-gameplay-input", "developer-tools-controls-reachable-1280x720",
    "explicit-developer-grant-is-marked-and-isolated", "developer-tool-provenance-survives-mode-roundtrip",
    "shipyard-design-artwork-loaded",
}
REQUIRED_CHECKS.update(MODE_CHECKS)
REQUIRED_CHECKS.update({
    "bottom-command-toolbar-removed", "ship-icon-selection-right-click-and-timed-travel",
    "metric-route-feedback-visible-at-720p",
    "research-workspace-drag-zoom-tabs-locks-and-input-shielding",
    "research-workspace-search-select-only-and-escape-close",
    "research-card-action-visible-and-clickable-at-720p-and-1080p",
    "duplicate-campaign-confirmation-starts-one-loading-transition",
    "startup-artwork-stays-hidden-during-gameplay-refresh-and-navigation",
    "startup-artwork-stays-hidden-during-manual-save",
    "startup-artwork-stays-hidden-during-scheduled-autosave",
    "responsive-720p-1080p-1440p-4k-reflow-and-input",
    "planet-inspector-organized-stats-and-mouse-selection", "system-skies-distinct-and-stable-on-return",
    "industry-priority-save-persisted", "industry-priority-load-reflected-in-economy-panel",
    "queued-ship-cancel-before-promotion-conserves-population-materials-and-refund",
    "shipyard-unsaved-queued-cancellation-removes-order-and-refunds-visible-costs",
    "shipyard-load-replaces-campaign-and-restores-saved-orders-and-economy",
    "active-ship-cancel-refunds-paid-remainder-and-promotes-queue",
    "promoted-ship-cancel-returns-population-and-paid-authorization",
    "shipyard-orders-save-preserves-stable-identities",
    "cancelled-ship-order-identities-are-no-longer-actionable",
})
REQUIRED_CHECKS.update(f"industry-priority-pointer-{priority}" for priority in
                       ("Balanced", "InfrastructureFirst", "ShipbuildingFirst"))
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def png_size(data: bytes, expected=(1280, 720)) -> tuple[int, int]:
    """Read a complete, CRC-checked PNG rather than trusting its extension or log."""
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
            if (width, height) != expected:
                raise ValueError(f"expected capture {expected[0]}x{expected[1]}, got {width}x{height}")
            if depth != 8 or color not in (2, 6) or compression or filtering or interlace:
                raise ValueError("expected ordinary 8-bit RGB/RGBA capture")
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
    if (len(decoded) != expected_size or not decoder.eof or decoder.unused_data
            or decoder.unconsumed_tail):
        raise ValueError("PNG image data is truncated or has an unexpected size")
    stride = 1 + width * channels
    if any(decoded[row * stride] > 4 for row in range(height)):
        raise ValueError("invalid PNG row filter")
    return width, height


def validate_capture(directory: Path, expected_sha: str) -> list[str]:
    failures = []
    try:
        manifest = json.loads((directory / "capture-manifest.json").read_text(encoding="utf-8"))
        log = (directory / "godot-capture.log").read_text(encoding="utf-8")
        import_log = (directory / "godot-import.log").read_text(encoding="utf-8")
    except (OSError, ValueError) as error:
        return [f"Cannot read capture evidence: {error}"]
    failures.extend(validate_log(import_log))
    failures.extend(validate_log(log, require_runtime_ready=True))
    lines = {line.strip() for line in log.splitlines()}
    if "STELLAR_SCREENSHOT_CAPTURE_COMPLETE" not in lines:
        failures.append("Screenshot driver did not finish.")
    if not re.fullmatch(r"[0-9a-f]{40}", expected_sha) or manifest.get("git_sha") != expected_sha:
        failures.append("Capture does not match the exact workflow commit.")
    if manifest.get("schema_version") != 2 or manifest.get("input_mode") not in {
            "Input.ParseInputEvent", "Viewport.PushInput (visible)"}:
        failures.append("Capture does not prove the real-input schema.")
    checks = manifest.get("checks", [])
    if not isinstance(checks, list) or any(not isinstance(item, str) for item in checks):
        failures.append("Invalid check evidence.")
        checks = []
    if len(checks) != len(set(checks)):
        failures.append("Duplicate check evidence.")
    missing = REQUIRED_CHECKS - set(checks)
    if missing:
        failures.append("Missing required checks: " + ", ".join(sorted(missing)))
    for check in REQUIRED_CHECKS:
        if f"STELLAR_UI_CHECK_PASS {check}" not in lines:
            failures.append(f"Missing runtime check marker: {check}")
    mouse_count = manifest.get("mouse_actions")
    mouse_lines = [line for line in log.splitlines() if line.startswith("STELLAR_MOUSE_INPUT ")]
    if not isinstance(mouse_count, int) or mouse_count < 35 or mouse_count != len(mouse_lines):
        failures.append("Insufficient or inconsistent real mouse input evidence.")
    captures = manifest.get("captures", [])
    if not isinstance(captures, list) or any(not isinstance(item, dict) for item in captures):
        return failures + ["Invalid screenshot evidence."]
    names = [item.get("file") for item in captures]
    if (any(not isinstance(name, str) for name in names)
            or len(names) != len(CAPTURES) or set(names) != set(CAPTURES)):
        return failures + ["Screenshot manifest differs from the required views."]
    if {path.name for path in directory.glob("*.png")} != set(CAPTURES):
        failures.append("Screenshot directory contains missing or unexpected images.")
    for capture in captures:
        name = capture["file"]
        try:
            data = (directory / name).read_bytes()
            width, height = png_size(data, CAPTURE_DIMENSIONS.get(name, (1280, 720)))
            if len(data) < 4096:
                raise ValueError("PNG is unexpectedly small")
            if (capture.get("width"), capture.get("height")) != (width, height):
                raise ValueError("manifest dimensions disagree with PNG")
            if capture.get("bytes") != len(data) or capture.get("sha256") != hashlib.sha256(data).hexdigest():
                raise ValueError("manifest checksum/length disagree with PNG")
            marker = f"STELLAR_SCREENSHOT_CAPTURED {name} {width}x{height} {len(data)} bytes"
            if marker not in lines:
                raise ValueError("runtime capture marker missing or inconsistent")
        except (OSError, ValueError, zlib.error, struct.error) as error:
            failures.append(f"{name}: {error}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--expected-sha", required=True)
    args = parser.parse_args()
    failures = validate_capture(args.directory, args.expected_sha)
    for failure in failures:
        print(f"Screenshot validation failed: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"Validated {len(CAPTURES)} exact-head rendered captures and {len(REQUIRED_CHECKS)} real-input checks.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
