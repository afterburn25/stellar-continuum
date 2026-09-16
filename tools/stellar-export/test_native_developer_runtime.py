import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_developer_runtime import validate_native_developer_export
from test_native_diplomacy_runtime import FIXTURE, payload


def diagnostic():
    return {"mode": 1, "demo_speed": 1, "tools_panel": 1, "command": 1,
            "envelope_save": 1, "fresh_campaign": 1, "backup_kept": 1}


def envelope(tools_used=True):
    return {"DeveloperFormatVersion": 1, "Mode": "Developer",
            "ToolsUsed": tools_used, "Campaign": payload(3)}


class NativeDeveloperRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-developer-test-") \
                as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps(FIXTURE), encoding="utf-8")
            calls = []

            def launch(args, *, cwd, **unused):
                shot = Path(args[args.index("--developer-smoke") + 1])
                save = Path(args[args.index("--save-path") + 1])
                self.assertEqual(cwd, shot.parent)
                self.assertEqual(cwd, save.parent)
                calls.append(args)
                state = diagnostic()
                if fault == "mode": state["mode"] = 0
                if fault == "demo_speed": state["demo_speed"] = 0
                if fault == "tools": state["tools_panel"] = 0
                if fault == "command": state["command"] = 0
                if fault == "fresh": state["fresh_campaign"] = 0
                if fault == "backup": state["backup_kept"] = 0
                if fault == "malformed": state["mode"] = "yes"
                record = payload(3)
                if fault == "save": record["Galaxy"]["Systems"] = [1]
                if fault == "envelope_marker": record["Mode"] = "Developer"
                save.write_text(json.dumps(record), encoding="utf-8")
                if fault != "envelope":
                    primary = envelope(tools_used=False)
                    backup = envelope(tools_used=True)
                    if fault == "tools_used": primary["ToolsUsed"] = True
                    if fault == "envelope_payload":
                        primary["Campaign"]["FormatVersion"] = 16
                    if fault == "backup_payload":
                        backup["Campaign"]["FormatVersion"] = 16
                    (save.parent / "developer-autosave.json").write_text(
                        json.dumps(primary), encoding="utf-8")
                    if fault != "backup_missing":
                        (save.parent /
                         "developer-autosave.json.bak").write_text(
                             json.dumps(backup), encoding="utf-8")
                varied = bytes(range(256)) if fault != "blank" else bytes(200)
                if fault != "capture":
                    shot.write_bytes(b"BM" + b"\0" * 52 + varied * 40)
                return subprocess.CompletedProcess(
                    args, 0,
                    f"gpu_driver=vulkan systems=20 image_uploads=5 save=ok "
                    f"developer={json.dumps(state, separators=(',', ':'))}",
                    "")

            with mock.patch("native_developer_runtime.subprocess.run",
                            side_effect=launch):
                result = validate_native_developer_export(
                    package, {}, fixture)
            self.assertEqual(len(calls), 2)
            self.assertIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertEqual(len(result["developerCaptures"]), 2)
            self.assertTrue(result["nativeDeveloperMode"])
            self.assertTrue(result["nativeDeveloperTools"])

    def test_complete_actual_contract(self): self.exercise()
    def test_mode_required(self):
        with self.assertRaises(RuntimeError): self.exercise("mode")
    def test_demo_speed_required(self):
        with self.assertRaises(RuntimeError): self.exercise("demo_speed")
    def test_tools_panel_required(self):
        with self.assertRaises(RuntimeError): self.exercise("tools")
    def test_command_required(self):
        with self.assertRaises(RuntimeError): self.exercise("command")
    def test_fresh_campaign_required(self):
        with self.assertRaises(RuntimeError): self.exercise("fresh")
    def test_backup_kept_required(self):
        with self.assertRaises(RuntimeError): self.exercise("backup")
    def test_backup_missing_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("backup_missing")
    def test_backup_payload_required(self):
        with self.assertRaises(RuntimeError): self.exercise("backup_payload")
    def test_malformed_metric_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("malformed")
    def test_damaged_payload_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("save")
    def test_envelope_required(self):
        with self.assertRaises(RuntimeError): self.exercise("envelope")
    def test_envelope_tools_used_required(self):
        with self.assertRaises(RuntimeError): self.exercise("tools_used")
    def test_envelope_payload_required(self):
        with self.assertRaises(RuntimeError): self.exercise("envelope_payload")
    def test_enveloped_player_save_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("envelope_marker")
    def test_blank_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("blank")
    def test_missing_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")


if __name__ == "__main__":
    unittest.main()
