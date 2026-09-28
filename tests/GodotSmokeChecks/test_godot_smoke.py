"""Regression cases for issue #61: process success alone cannot prove scene health."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "validate_godot_smoke.py"
SPEC = importlib.util.spec_from_file_location("validate_godot_smoke", SCRIPT)
SMOKE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SMOKE)
READY = "STELLAR_RUNTIME_READY IntegratedMain\n"
BANNER = "Godot Engine v4.7.2.stable.mono.official\n"


class GodotSmokeChecks(unittest.TestCase):
    def test_engine_boot_image_is_disabled_for_single_runtime_loading_surface(self):
        project = Path(__file__).resolve().parents[2] / "project.godot"
        settings = project.read_text(encoding="utf-8")
        self.assertIn("boot_splash/show_image=false", settings)

    def test_real_startup_and_benign_shutdown_warning_pass(self):
        self.assertEqual([], SMOKE.validate_log(
            BANNER + READY + "WARNING: ObjectDB instances leaked at exit.\n", True))

    def test_exit_zero_engine_without_initialized_scene_fails(self):
        self.assertTrue(SMOKE.validate_log(BANNER, True))

    def test_reported_issue_61_error_fails_even_with_ready_marker(self):
        error = (
            "ERROR: Cannot instantiate C# script because the associated class could not be found. "
            "Script: 'res://src/Game/Presentation/IntegratedMain.cs'.\n"
        )
        self.assertTrue(SMOKE.validate_log(BANNER + error, True))
        self.assertTrue(SMOKE.validate_log(BANNER + error + READY, True))
        self.assertTrue(SMOKE.validate_log(BANNER + READY + error, True))

    def test_child_script_resource_and_managed_failures_are_rejected(self):
        for error in (
            "SCRIPT ERROR: Parse Error: Could not resolve script.",
            "ERROR: Failed loading resource: res://missing.tres.",
            "USER ERROR: Startup initialization failed.",
            "FATAL ERROR: Engine initialization failed.",
            "Unhandled exception. System.InvalidOperationException: Campaign failed.",
            "System.NullReferenceException: Object reference not set to an instance.",
            "\x1b[31mERROR: Child UI initialization failed.\x1b[0m",
        ):
            with self.subTest(error=error):
                self.assertTrue(SMOKE.validate_log(BANNER + READY + error, True))

    def test_marker_must_be_an_entire_output_line(self):
        self.assertTrue(SMOKE.validate_log(BANNER + 'echo "' + READY.strip() + '"\n', True))

    def test_editor_needs_output_but_not_runtime_marker(self):
        self.assertEqual([], SMOKE.validate_log(BANNER))
        self.assertTrue(SMOKE.validate_log(""))
        self.assertTrue(SMOKE.validate_log(BANNER + "ERROR: Failed loading resource."))
        self.assertTrue(SMOKE.validate_log(BANNER + "WARNING: Scan thread aborted..."))

    def test_cli_rejects_missing_empty_and_false_positive_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "runtime.log"
            for content, expected in ((None, 1), ("", 1), (BANNER, 1), (BANNER + READY, 0),
                                      (BANNER + READY + "ERROR: Late frame failed.\n", 1)):
                with self.subTest(content=content):
                    if content is not None:
                        log.write_text(content, encoding="utf-8")
                    result = subprocess.run(
                        [sys.executable, str(SCRIPT), str(log), "--require-runtime-ready"],
                        capture_output=True, text=True, check=False)
                    self.assertEqual(expected, result.returncode, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
