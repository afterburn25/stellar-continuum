"""Fail closed on missing interaction proof, stale artifacts, and corrupt screenshot evidence."""

import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

SCRIPTS = Path(__file__).resolve().parents[2] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import validate_screenshot_capture as capture
import capture_diplomacy

SHA = "a" * 40


def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def sample_png(width=1280, height=720):
    row = bytes([0]) + random.Random(42).randbytes(width * 4)
    return (capture.PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(row * height)) + chunk(b"IEND", b""))


class ScreenshotEvidenceChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.png = sample_png()
        cls.images = {size: sample_png(*size) for size in set(capture.CAPTURE_DIMENSIONS.values())}

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.manifest = {
            "schema_version": 2, "git_sha": SHA, "input_mode": "Input.ParseInputEvent",
            "mouse_actions": 35, "checks": sorted(capture.REQUIRED_CHECKS), "captures": [],
        }
        lines = ["Godot Engine v4.7.2.stable.mono.official", "STELLAR_RUNTIME_READY IntegratedMain"]
        lines.extend("STELLAR_MOUSE_INPUT synthetic-test-fixture" for _ in range(35))
        lines.extend(f"STELLAR_UI_CHECK_PASS {name}" for name in self.manifest["checks"])
        for name in capture.CAPTURES:
            width, height = capture.CAPTURE_DIMENSIONS.get(name, (1280, 720))
            png = self.images.get((width, height), self.png)
            (self.directory / name).write_bytes(png)
            self.manifest["captures"].append({
                "file": name, "width": width, "height": height, "bytes": len(png),
                "sha256": hashlib.sha256(png).hexdigest(),
            })
            lines.append(f"STELLAR_SCREENSHOT_CAPTURED {name} {width}x{height} {len(png)} bytes")
        lines.append("STELLAR_SCREENSHOT_CAPTURE_COMPLETE")
        self.log = "\n".join(lines) + "\n"
        self.write_evidence()
        (self.directory / "godot-import.log").write_text("Godot import complete\n", encoding="utf-8")

    def write_evidence(self):
        (self.directory / "capture-manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")
        (self.directory / "godot-capture.log").write_text(self.log, encoding="utf-8")

    def failures(self):
        self.write_evidence()
        return capture.validate_capture(self.directory, SHA)

    def test_complete_evidence_passes(self):
        self.assertEqual([], self.failures())

    def test_visible_viewport_input_evidence_passes(self):
        self.manifest["input_mode"] = "Viewport.PushInput (visible)"
        self.assertEqual([], self.failures())

    def test_old_commit_or_pressed_signal_only_is_rejected(self):
        self.manifest["git_sha"] = "b" * 40
        self.manifest["input_mode"] = "Pressed signals"
        failures = self.failures()
        self.assertTrue(any("exact workflow commit" in item for item in failures))
        self.assertTrue(any("real-input schema" in item for item in failures))

    def test_missing_or_forged_required_interaction_proof_is_rejected(self):
        required = "drawer-blocks-map-orders"
        self.manifest["checks"].remove(required)
        self.log = self.log.replace(f"STELLAR_UI_CHECK_PASS {required}\n", f'echo "STELLAR_UI_CHECK_PASS {required}"\n')
        failures = self.failures()
        self.assertTrue(any("Missing required checks" in item for item in failures))
        self.assertTrue(any("Missing runtime check marker" in item for item in failures))

    def test_duplicate_checks_and_mouse_count_mismatch_are_rejected(self):
        self.manifest["checks"].append(self.manifest["checks"][0])
        self.manifest["mouse_actions"] = 100
        failures = self.failures()
        self.assertTrue(any("Duplicate" in item for item in failures))
        self.assertTrue(any("mouse input evidence" in item for item in failures))

    def test_prior_graphics_without_solar_identity_or_earth_selection_is_rejected(self):
        for required in ("normal-human-earth-sol-start", "developer-human-earth-sol-start",
                         "sol-catalog-worlds-visible", "earth-selected-by-mouse",
                         "developer-sol-identity-survives-reload", "icon-only-controls-visible", "project-icons-crisp"):
            with self.subTest(required=required):
                self.manifest["checks"].remove(required)
                self.assertTrue(any(required in item for item in self.failures()))
                self.manifest["checks"].append(required)

    def test_truncated_or_swapped_png_is_rejected(self):
        path = self.directory / capture.CAPTURES[0]
        path.write_bytes(self.png[:-9])
        self.assertTrue(any(capture.CAPTURES[0] in item for item in self.failures()))
        path.write_bytes(self.png)
        self.manifest["captures"][0]["sha256"] = "0" * 64
        self.assertTrue(any("checksum" in item for item in self.failures()))

    def test_pre_cinematic_artifact_without_zoom_or_privacy_proof_is_rejected(self):
        for required in capture.CAMERA_CHECKS:
            with self.subTest(required=required):
                self.manifest["checks"].remove(required)
                self.assertTrue(any(required in item for item in self.failures()))
                self.manifest["checks"].append(required)

    def test_camera_check_in_manifest_requires_actual_runtime_marker(self):
        for required in ("regional-wheel-button-zoom-parity", "system-pan-inverse-hit",
                         "resize-preserves-body-hit", "unknown-body-materials-redacted"):
            with self.subTest(required=required):
                original = self.log
                self.log = self.log.replace(f"STELLAR_UI_CHECK_PASS {required}\n", "")
                self.assertTrue(any(f"Missing runtime check marker: {required}" in item
                                    for item in self.failures()))
                self.log = original

    def test_orbital_only_artifacts_cannot_replace_actual_surface_acceptance(self):
        for required in capture.SURFACE_CHECKS:
            with self.subTest(required=required):
                self.manifest["checks"].remove(required)
                self.assertTrue(any(required in item for item in self.failures()))
                self.manifest["checks"].append(required)
        required = "surface-collision-rejected-without-charge"
        self.log = self.log.replace(f"STELLAR_UI_CHECK_PASS {required}\n", "")
        self.assertTrue(any(f"Missing runtime check marker: {required}" in item for item in self.failures()))

    def test_wrong_dimensions_and_undecodable_png_are_rejected(self):
        wrong_size = (capture.PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", 640, 360, 8, 6, 0, 0, 0))
                      + chunk(b"IDAT", zlib.compress(b"not pixels")) + chunk(b"IEND", b""))
        with self.assertRaisesRegex(ValueError, "1280x720"):
            capture.png_size(wrong_size)
        bad_pixels = (capture.PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", 1280, 720, 8, 6, 0, 0, 0))
                      + chunk(b"IDAT", zlib.compress(b"not pixels")) + chunk(b"IEND", b""))
        with self.assertRaisesRegex(ValueError, "image data"):
            capture.png_size(bad_pixels)

    def test_developer_modes_require_real_isolation_and_explicit_command_evidence(self):
        for required in capture.MODE_CHECKS:
            with self.subTest(required=required):
                self.manifest["checks"].remove(required)
                self.assertTrue(any(required in item for item in self.failures()))
                self.manifest["checks"].append(required)
        required = "explicit-developer-grant-is-marked-and-isolated"
        self.log = self.log.replace(f"STELLAR_UI_CHECK_PASS {required}\n", "")
        self.assertTrue(any(f"Missing runtime check marker: {required}" in item for item in self.failures()))

    def test_png_crc_corruption_is_rejected(self):
        damaged = bytearray(self.png)
        damaged[20] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            capture.png_size(bytes(damaged))

    def test_missing_or_extra_image_is_rejected(self):
        (self.directory / capture.CAPTURES[-1]).unlink()
        self.assertTrue(self.failures())
        (self.directory / capture.CAPTURES[-1]).write_bytes(self.png)
        (self.directory / "old-capture.png").write_bytes(self.png)
        self.assertTrue(any("unexpected images" in item for item in self.failures()))

    def test_late_runtime_error_or_aborted_import_cannot_hide_behind_complete_marker(self):
        self.log += "ERROR: Drawer script failed after capture.\n"
        self.assertTrue(any("Godot error" in item for item in self.failures()))
        self.log = self.log.removesuffix("ERROR: Drawer script failed after capture.\n")
        (self.directory / "godot-import.log").write_text("WARNING: Scan thread aborted...\n", encoding="utf-8")
        self.assertTrue(any("Godot error" in item for item in self.failures()))

    def test_cli_missing_evidence_fails(self):
        with tempfile.TemporaryDirectory() as empty:
            result = subprocess.run([sys.executable, "-B", str(SCRIPTS / "validate_screenshot_capture.py"),
                                     empty, "--expected-sha", SHA], capture_output=True, text=True)
            self.assertEqual(1, result.returncode)
            self.assertIn("Cannot read capture evidence", result.stderr)

    def test_diplomacy_capture_accepts_only_documented_vsync_warning(self):
        warning = (capture_diplomacy.VSYNC_WARNING + "\n"
                   "at: set_use_vsync (platform/linuxbsd/x11/gl_manager_x11.cpp:372)\n")
        fatal, count = capture_diplomacy.classify_native_stderr(warning)
        self.assertEqual((fatal, count), ("", 1))
        fatal, count = capture_diplomacy.classify_native_stderr(warning + "Unhandled exception: boom\n")
        self.assertIn("Unhandled exception", fatal)
        self.assertEqual(count, 1)


if __name__ == "__main__":
    unittest.main()
