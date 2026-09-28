"""Regression checks for focused startup-failure UI evidence validation."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_screenshot_capture import chunk, sample_png


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "scripts" / "validate_startup_failure_capture.py"
SHA = "a" * 40


class StartupFailureCaptureChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.capture = self.root / "capture"
        self.support = self.root / "profile" / "logs"
        self.capture.mkdir(parents=True)
        self.support.mkdir(parents=True)
        self.save = self.root / "profile" / "saves" / "autosave.json"
        self.save.parent.mkdir(parents=True)
        save_bytes = (b'{"campaign":"ordinary isolated fixture","padding":"' + b"s" * 6000 + b'"}')
        self.save.write_bytes(save_bytes)
        small_png = sample_png()
        png = small_png[:-12] + chunk(b"tEXt", b"capture-padding" * 400) + small_png[-12:]
        (self.capture / "startup-failure-ui.png").write_bytes(png)
        checks = [
            "startup-failure-pauses-partial-world",
            "startup-failure-layer-shields-partial-world",
            "startup-failure-exit-is-readable-and-reachable",
            "startup-failure-backdrop-blocks-gameplay-input",
            "startup-failure-preserves-campaign-save-before-exit",
        ]
        self.evidence = {
            "schema_version": 1,
            "git_sha": SHA,
            "scope": "actual IntegratedMain startup failure at 1280x720",
            "injection": "--stellar-startup-failure-ui",
            "input_mode": "Input.ParseInputEvent",
            "viewport": {"width": 1280, "height": 720},
            "exit_bounds": {"x": 550, "y": 500, "width": 180, "height": 44},
            "save": {"file": "autosave.json", "bytes": len(save_bytes),
                     "sha256": hashlib.sha256(save_bytes).hexdigest()},
            "exit_activated": True,
            "checks": checks,
            "capture": {"file": "startup-failure-ui.png", "bytes": len(png),
                        "sha256": hashlib.sha256(png).hexdigest()},
        }
        self.write_evidence()
        (self.capture / "exit-code.txt").write_text("1\n", encoding="utf-8")
        diagnostic = (
            "Integrated campaign initialization failed. startupSeed=20260911 "
            "playerSave=C:\\profile\\saves\\autosave.json "
            "developerSave=C:\\profile\\saves\\developer-autosave.json\n"
            "System.InvalidOperationException: Requested startup failure smoke.\n"
            " ---> System.IO.InvalidDataException: Deterministic nested startup failure evidence."
        )
        self.log = self.root / "godot-capture.log"
        self.log.write_text(
            "Godot Engine\nERROR: " + diagnostic + "\n"
            "STELLAR_FOCUSED_STARTUP_FAILURE_UI_READY\n"
            "STELLAR_FOCUSED_STARTUP_FAILURE_EXIT_ACTIVATED\n", encoding="utf-8")
        (self.support / "game-ABC.log").write_text(
            "2026-09-11 [ABC] [startup-fatal] " + diagnostic + "\n", encoding="utf-8")

    def write_evidence(self):
        (self.capture / "startup-failure-ui.json").write_text(
            json.dumps(self.evidence), encoding="utf-8")

    def run_validator(self):
        return subprocess.run([
            sys.executable, str(VALIDATOR), str(self.capture), "--save", str(self.save),
            "--log", str(self.log), "--support-directory", str(self.support),
            "--expected-sha", SHA,
        ], text=True, capture_output=True, check=False)

    def test_accepts_complete_actual_failure_evidence(self):
        result = self.run_validator()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_false_exit_and_changed_save(self):
        self.evidence["exit_activated"] = False
        self.write_evidence()
        self.assertNotEqual(self.run_validator().returncode, 0)
        self.evidence["exit_activated"] = True
        self.write_evidence()
        self.save.write_bytes(self.save.read_bytes() + b"changed")
        self.assertNotEqual(self.run_validator().returncode, 0)

    def test_rejects_ready_marker_and_unexpected_engine_error(self):
        self.log.write_text(self.log.read_text(encoding="utf-8") +
                            "STELLAR_RUNTIME_READY IntegratedMain\nERROR: unrelated failure\n",
                            encoding="utf-8")
        self.assertNotEqual(self.run_validator().returncode, 0)

    def test_rejects_missing_or_wrong_exit_and_source(self):
        (self.capture / "exit-code.txt").unlink()
        self.assertNotEqual(self.run_validator().returncode, 0)
        (self.capture / "exit-code.txt").write_text("0\n", encoding="utf-8")
        self.assertNotEqual(self.run_validator().returncode, 0)
        (self.capture / "exit-code.txt").write_text("1\n", encoding="utf-8")
        self.evidence["git_sha"] = "b" * 40
        self.write_evidence()
        self.assertNotEqual(self.run_validator().returncode, 0)


if __name__ == "__main__":
    unittest.main()
