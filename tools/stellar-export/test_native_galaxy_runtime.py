import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

from native_galaxy_runtime import _bmp, validate_native_galaxy_export


def diagnostic(mode="fresh"):
    return {
        "mode": mode, "fitted_scale": 1.0, "regional_scale": 5.1,
        "wheel_input": True, "system_entry": True, "paused": True,
        "day_unchanged": True, "decoded_sources": 3,
        "overview": {"deep_field": 1, "galaxy_layer": 1,
                     "regional_nebula": 0, "regional_points": 0,
                     "catalog_markers": 500, "known_markers": 2,
                     "unknown_markers": 498, "revealed_unknown_labels": 0},
        "regional": {"deep_field": 0, "galaxy_layer": 0,
                     "regional_nebula": 1, "regional_points": 356,
                     "catalog_markers": 4, "known_markers": 1,
                     "unknown_markers": 3, "revealed_unknown_labels": 0},
        "system": {"background_images": 0, "regional_points": 0},
    }


class NativeGalaxyRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
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
                if fault == "regional": state["regional"]["regional_nebula"] = 0
                if fault == "system": state["system"]["background_images"] = 1
                if fault == "labels": state["overview"]["revealed_unknown_labels"] = 1
                if fault == "wheel": state["wheel_input"] = False
                if fault == "scale": state["fitted_scale"] = float("nan")
                if fault == "decoded": state["decoded_sources"] = 2
                if fault == "knowledge": state["overview"]["known_markers"] = 3; state["overview"]["unknown_markers"] = 497
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
                stdout = (f"gpu_driver=vulkan systems=500 image_uploads={uploads} save=ok "
                          f"galaxy_art={json.dumps(state, separators=(',', ':'))}")
                return subprocess.CompletedProcess(args, 0, stdout, "")

            def fake_bmp(path, width, height):
                if not path.is_file(): raise RuntimeError("missing capture")
                if fault == "same_capture": return b"same"
                return (path.name + str(width) + str(height)).encode()

            with mock.patch("native_galaxy_runtime.subprocess.run", side_effect=launch), \
                 mock.patch("native_galaxy_runtime._bmp", side_effect=fake_bmp):
                result = validate_native_galaxy_export(package, {})
            self.assertEqual(len(calls), 2)
            self.assertNotIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertEqual(len(result["galaxyCaptures"]), 6)
            self.assertTrue(result["nativeGalaxyFittedArtwork"])

    def test_complete_actual_contract(self): self.exercise()
    def test_missing_overview_art_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("overview")
    def test_missing_regional_nebula_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("regional")
    def test_system_art_leak_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("system")
    def test_unknown_label_leak_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("labels")
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
