import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_logistics_runtime import validate_native_logistics_export
from test_native_diplomacy_runtime import FIXTURE, payload


def diagnostic():
    return {"panel": 1, "ready": 1, "nodes": 2, "corridors": 1,
            "supply": 1.5, "demand": 0.75, "delivered": 0.75,
            "shortfall": 0.0}


class NativeLogisticsRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-logistics-test-") \
                as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps(FIXTURE), encoding="utf-8")
            calls = []

            def launch(args, *, cwd, **unused):
                shot = Path(args[args.index("--logistics-smoke") + 1])
                save = Path(args[args.index("--save-path") + 1])
                self.assertEqual(cwd, shot.parent)
                self.assertEqual(cwd, save.parent)
                calls.append(args)
                state = diagnostic()
                if fault == "panel": state["panel"] = 0
                if fault == "ready": state["ready"] = 0
                if fault == "nodes": state["nodes"] = 0
                if fault == "malformed": state["supply"] = "high"
                record = payload(3)
                if fault == "save": record["Galaxy"]["Systems"] = [1]
                save.write_text(json.dumps(record), encoding="utf-8")
                varied = bytes(range(256)) if fault != "blank" else bytes(200)
                if fault != "capture":
                    shot.write_bytes(b"BM" + b"\0" * 52 + varied * 40)
                return subprocess.CompletedProcess(
                    args, 0,
                    f"gpu_driver=vulkan systems=20 image_uploads=5 save=ok "
                    f"logistics={json.dumps(state, separators=(',', ':'))}",
                    "")

            with mock.patch("native_logistics_runtime.subprocess.run",
                            side_effect=launch):
                result = validate_native_logistics_export(
                    package, {}, fixture)
            self.assertEqual(len(calls), 2)
            self.assertIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertEqual(len(result["logisticsCaptures"]), 2)
            self.assertTrue(result["nativeLogisticsPanel"])
            self.assertTrue(result["nativeLogisticsHomeNetwork"])

    def test_complete_actual_contract(self): self.exercise()
    def test_panel_required(self):
        with self.assertRaises(RuntimeError): self.exercise("panel")
    def test_network_ready_required(self):
        with self.assertRaises(RuntimeError): self.exercise("ready")
    def test_nodes_required(self):
        with self.assertRaises(RuntimeError): self.exercise("nodes")
    def test_malformed_metric_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("malformed")
    def test_damaged_payload_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("save")
    def test_blank_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("blank")
    def test_missing_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")


if __name__ == "__main__":
    unittest.main()
