import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from native_galaxy_runtime import _bmp, validate_native_galaxy_export
from test_native_frame_profile import cold_profile


def diagnostic(mode="fresh"):
    return {
        "mode": mode, "fitted_scale": 1.0, "regional_scale": 5.1,
        "wheel_input": True, "system_entry": True, "paused": True,
        "day_unchanged": True, "decoded_sources": 4,
        "overview": {"deep_field": 1, "galaxy_layer": 1, "star_background": 0,
                     "regional_nebula": 0, "regional_points": 0,
                     "catalog_markers": 500, "known_markers": 2,
                     "unknown_markers": 498, "revealed_unknown_labels": 0,
                     "labels": {"candidates": 0, "measured": 0, "placed": 0,
                                "selected_requested": 0, "selected_placed": 0,
                                "label_overlaps": 0, "obstacle_overlaps": 0,
                                "hud_overlaps": 0, "star_overlaps": 0,
                                "outside_viewport": 0}},
        "regional": {"deep_field": 0, "galaxy_layer": 0, "star_background": 1,
                     "regional_nebula": 1, "regional_points": 0,
                     "catalog_markers": 4, "known_markers": 1,
                     "unknown_markers": 3, "revealed_unknown_labels": 0,
                     "labels": {"candidates": 2, "measured": 2, "placed": 2,
                                "selected_requested": 1, "selected_placed": 1,
                                "label_overlaps": 0, "obstacle_overlaps": 0,
                                "hud_overlaps": 0, "star_overlaps": 0,
                                "outside_viewport": 0}},
        "system": {"background_images": 0, "regional_points": 0},
    }


def steady_profile(samples):
    metrics = {"mean_ms": 2.0, "p50_ms": 1.0, "p95_ms": 3.0,
               "p99_ms": 4.0, "max_ms": 5.0}
    value = {"samples": samples}
    for phase in ("interval", "update", "scene", "submission", "throttle", "present"):
        value[phase] = dict(metrics)
    value["readback"] = {key: 0.0 for key in metrics}
    return value


class NativeGalaxyRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None, profile_frames=0):
        with tempfile.TemporaryDirectory(prefix="stellar-galaxy-test-") as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            calls = []

            def launch(args, *, cwd, **unused):
                reload = "--load" in args
                overview = Path(args[args.index("--galaxy-art-smoke") + 1])
                save = Path(args[args.index("--save-path") + 1])
                self.assertEqual(cwd, overview.parent)
                self.assertEqual(cwd, save.parent)
                calls.append(args)
                state = diagnostic("paused_reload" if reload else "fresh")
                if fault == "overview": state["overview"]["galaxy_layer"] = 0
                if fault == "regional": state["regional"]["star_background"] = 0
                if fault == "overview_replaced": state["overview"]["star_background"] = 1
                if fault == "legacy_points": state["regional"]["regional_points"] = 356
                if fault == "legacy_nebula": state["regional"]["regional_nebula"] = 0
                if fault == "system": state["system"]["background_images"] = 1
                if fault == "labels": state["overview"]["revealed_unknown_labels"] = 1
                if fault == "wheel": state["wheel_input"] = False
                if fault == "scale": state["fitted_scale"] = float("nan")
                if fault == "decoded": state["decoded_sources"] = 2
                if fault == "knowledge": state["overview"]["known_markers"] = 3; state["overview"]["unknown_markers"] = 497
                if fault == "labels_missing": state["overview"].pop("labels")
                if fault == "labels_bool": state["overview"]["labels"]["placed"] = True
                if fault == "labels_negative": state["overview"]["labels"]["measured"] = -1
                if fault == "labels_budget": state["overview"]["labels"]["measured"] = 129
                if fault == "labels_counts": state["overview"]["labels"]["placed"] = 5; state["overview"]["labels"]["measured"] = 4
                if fault == "labels_selected": state["overview"]["labels"]["selected_placed"] = 3; state["overview"]["labels"]["selected_requested"] = 2
                if fault == "labels_overlap": state["overview"]["labels"]["hud_overlaps"] = 1
                if fault == "labels_offscreen": state["overview"]["labels"]["outside_viewport"] = 1
                if fault == "labels_extra": state["overview"]["labels"]["extra"] = 1
                if fault == "labels_zero_regional": state["regional"]["labels"]["placed"] = 0
                if isinstance(fault, str) and fault.startswith("labels_audit_"):
                    state["overview"]["labels"][fault.removeprefix("labels_audit_")] = 1
                if fault == "labels_missing_field": state["overview"]["labels"].pop("placed")
                if fault == "labels_measured_candidates": state["overview"]["labels"]["measured"] = 1; state["overview"]["labels"]["candidates"] = 0
                if fault == "labels_selected_placed": state["overview"]["labels"]["selected_placed"] = 1; state["overview"]["labels"]["placed"] = 0
                if fault == "labels_selected_requested": state["overview"]["labels"]["selected_requested"] = 1; state["overview"]["labels"]["candidates"] = 0
                payload = {"FormatVersion": 17, "SavedAtUtc": "later" if reload else "early",
                           "SimulationDays": 0,
                           "Galaxy": {"Systems": list(range(500)),
                                      "PlayerCivilizationId": 4,
                                      "Knowledge": [{"CivilizationId": 4,
                                                     "KnownSystemIds": [0, 1]}]}}
                if fault == "time": payload["SimulationDays"] = 1
                if reload and fault == "reload": payload["Galaxy"]["Systems"][4] = -1
                save.write_text(json.dumps(payload), encoding="utf-8")
                for suffix in ("", "-regional", "-system"):
                    path = overview.with_name(overview.stem + suffix + overview.suffix)
                    if fault != "capture" or suffix != "-system": path.write_bytes(b"BM")
                uploads = 0 if fault == "uploads" else 20
                stdout = f"gpu_driver=vulkan systems=500 image_uploads={uploads} save=ok "
                if profile_frames:
                    self.assertEqual(args[args.index("--profile-frames") + 1], str(profile_frames))
                    stdout += " steady_profile=" + json.dumps(steady_profile(profile_frames))
                    if fault != "cold_profile":
                        stdout += " cold_profile=" + json.dumps(cold_profile())
                stdout += f" galaxy_art={json.dumps(state, separators=(',', ':'))}"
                return subprocess.CompletedProcess(args, 0, stdout, "")

            def fake_bmp(path, width, height, stdout=None):
                if not path.is_file(): raise RuntimeError("missing capture")
                if fault == "same_capture": return b"same"
                return (path.name + str(width) + str(height)).encode()

            with mock.patch("native_galaxy_runtime.subprocess.run", side_effect=launch), \
                 mock.patch("native_galaxy_runtime._bmp", side_effect=fake_bmp):
                result = validate_native_galaxy_export(package, {}, profile_frames=profile_frames)
            self.assertEqual(len(calls), 2)
            self.assertNotIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertEqual(len(result["galaxyCaptures"]), 6)
            self.assertTrue(result["nativeGalaxyFittedArtwork"])
            if profile_frames:
                self.assertEqual(len(result["galaxyProfiles"]), 2)
                self.assertEqual(result["galaxyColdProfiles"], [cold_profile(), cold_profile()])
            else:
                self.assertNotIn("galaxyProfiles", result)
                self.assertNotIn("galaxyColdProfiles", result)

    def test_complete_actual_contract(self): self.exercise()
    def test_requested_steady_profile_is_forwarded_and_validated(self): self.exercise(profile_frames=120)
    def test_requested_profile_requires_cold_diagnostics(self):
        with self.assertRaises(RuntimeError): self.exercise("cold_profile", profile_frames=120)
    def test_missing_overview_art_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("overview")
    def test_missing_regional_background_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("regional")
    def test_incorrect_background_layers_rejected(self):
        for fault in ("overview_replaced", "legacy_points", "legacy_nebula"):
            with self.subTest(fault=fault), self.assertRaises(RuntimeError): self.exercise(fault)
    def test_system_art_leak_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("system")
    def test_unknown_label_leak_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("labels")
    def test_label_contract_rejects_malformed_counts(self):
        for fault in ("labels_missing", "labels_bool", "labels_negative", "labels_budget",
                      "labels_counts", "labels_selected", "labels_overlap", "labels_offscreen",
                      "labels_extra", "labels_zero_regional", "labels_missing_field",
                      "labels_measured_candidates", "labels_selected_placed",
                      "labels_selected_requested"):
            with self.subTest(fault=fault), self.assertRaises(RuntimeError):
                self.exercise(fault)

    def test_each_label_audit_violation_is_rejected(self):
        for field in ("label_overlaps", "obstacle_overlaps", "hud_overlaps",
                      "star_overlaps", "outside_viewport"):
            with self.subTest(field=field):
                with self.assertRaises(RuntimeError):
                    self.exercise("labels_audit_" + field)
    def test_missing_wheel_input_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("wheel")
    def test_nonfinite_scale_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("scale")
    def test_missing_source_decode_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("decoded")
    def test_missing_uploads_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("uploads")
    def test_saved_knowledge_mismatch_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("knowledge")
    def test_time_advance_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("time")
    def test_reload_mutation_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("reload")
    def test_missing_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")
    def test_identical_view_captures_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("same_capture")

    def test_bmp_rejects_truncated_pixels(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "bad.bmp"
            width, height, bits = 8, 4, 24
            row = ((width * bits + 31) // 32) * 4
            size = 54 + row * height
            header = bytearray(54)
            struct.pack_into("<2sIHHI", header, 0, b"BM", size, 0, 0, 54)
            struct.pack_into("<IiiHHIIiiII", header, 14, 40, width, height, 1,
                             bits, 0, row * height, 0, 0, 0, 0)
            path.write_bytes(header + bytes(row * height - 1))
            with self.assertRaises(RuntimeError): _bmp(path, width, height)

    def test_bmp_rejects_blank_pixels(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "blank.bmp"
            width, height, bits = 8, 4, 24
            row = ((width * bits + 31) // 32) * 4
            size = 54 + row * height
            header = bytearray(54)
            struct.pack_into("<2sIHHI", header, 0, b"BM", size, 0, 0, 54)
            struct.pack_into("<IiiHHIIiiII", header, 14, 40, width, height, 1,
                             bits, 0, row * height, 0, 0, 0, 0)
            path.write_bytes(header + bytes(row * height))
            with self.assertRaises(RuntimeError): _bmp(path, width, height)


if __name__ == "__main__":
    unittest.main()
