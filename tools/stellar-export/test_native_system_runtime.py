"""Orbital export proof must reject incomplete input, imagery or persistence."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_system_runtime import validate_native_system_export


class NativeSystemExportTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-orbital-test-") as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            calls = []

            def launch(args, *, cwd, env, **unused):
                self.assertNotEqual(cwd, package)
                save = Path(args[args.index("--save-path") + 1])
                capture = Path(args[args.index("--system-smoke") + 1])
                self.assertEqual(save.parent, cwd)
                self.assertEqual(capture.parent, cwd)
                calls.append(args)
                if "--load" in args:
                    self.assertTrue(save.is_file())
                    payload = json.loads(save.read_text())
                    payload["SavedAtUtc"] = "later"
                    if fault == "reload":
                        payload["Galaxy"]["Economies"][0]["Credits"] += 1
                else:
                    payload = {"FormatVersion": 17, "SavedAtUtc": "earlier", "SimulationDays": 0,
                               "Galaxy": {"Systems": list(range(500)), "Economies": [{"Credits": 200}]}}
                if fault == "time":
                    payload["SimulationDays"] = 1
                if fault != "save":
                    save.write_text(json.dumps(payload))
                if fault != "capture":
                    capture.write_bytes(b"BM" + bytes(54))
                body = 4 if fault == "body" else 3
                images = 0 if fault == "images" else 9
                scale = "nan" if fault == "scale" else "0.025"
                pan = 0 if fault == "input" else 1
                stdout = (f"gpu_driver=vulkan systems=500 image_uploads={images} save=ok "
                          f"system=id=0:body={body}:visible=9:scale={scale}"
                          f":entry=1:hit=1:pan={pan}:zoom=1:reset=1:back=1"
                          f":pause_retained=1:speed_retained=1:gesture_cleared=1:paused=1:day_unchanged=1")
                return subprocess.CompletedProcess(args, 0, stdout, "")

            with mock.patch("native_system_runtime.subprocess.run", side_effect=launch):
                result = validate_native_system_export(package, {})
            self.assertEqual(len(calls), 2)
            self.assertNotIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertTrue(result["nativeSystemPlayerInput"])
            self.assertTrue(result["nativeSystemBodyImages"])
            self.assertTrue(result["nativeSystemPausedReload"])

    def test_actual_input_images_and_paused_reload_are_required(self):
        self.exercise()

    def test_wrong_body_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("body")

    def test_no_image_upload_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("images")

    def test_nonfinite_scale_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("scale")

    def test_unproven_mouse_input_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("input")

    def test_browsing_cannot_advance_time(self):
        with self.assertRaises(RuntimeError): self.exercise("time")

    def test_reload_cannot_change_economy(self):
        with self.assertRaises(RuntimeError): self.exercise("reload")

    def test_missing_save_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("save")

    def test_missing_capture_is_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")


if __name__ == "__main__":
    unittest.main()
