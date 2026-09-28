import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from native_notification_runtime import validate_native_notification_export
from test_native_diplomacy_runtime import FIXTURE, payload


def diagnostic():
    return {"panel": 1, "items": 1, "unread": 1, "diplomacy": 1,
            "contact": 1, "focused_civ": 1}


class NativeNotificationRuntimeTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-notify-test-") as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir()
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps(FIXTURE), encoding="utf-8")
            calls = []

            def launch(args, *, cwd, **unused):
                shot = Path(args[args.index("--notification-smoke") + 1])
                save = Path(args[args.index("--save-path") + 1])
                self.assertEqual(cwd, shot.parent)
                self.assertEqual(cwd, save.parent)
                calls.append(args)
                state = diagnostic()
                territory = {"valid": True, "regions": 1, "claims": 1,
                             "fill_runs": 0, "contour_points": 14,
                             "fill_images": 1, "fog_texels": 100,
                             "unexplored": 19}
                if fault == "territory_invalid": territory["valid"] = False
                if fault == "panel": state["panel"] = 0
                if fault == "items": state["items"] = 0
                if fault == "unread": state["unread"] = 0
                if fault == "no_diplomacy": state["diplomacy"] = 0
                if fault == "no_contact": state["contact"] = 0
                if fault == "counterpart": state["contact"] = 2
                if fault == "focus": state["focused_civ"] = 2
                if fault == "history_flood":
                    state["items"] = 6
                    state["diplomacy"] = 6
                record = payload(3)
                if fault == "save": record["Galaxy"]["Systems"] = [1]
                save.write_text(json.dumps(record), encoding="utf-8")
                varied = bytes(range(256)) if fault != "blank" else bytes(200)
                shot.write_bytes(b"BM" + b"\0" * 52 + varied * 40)
                contact_path = shot.with_name(
                    shot.stem + "-contact" + shot.suffix)
                if fault != "capture":
                    other = (varied if fault == "same_capture"
                             else bytes(reversed(range(256))))
                    contact_path.write_bytes(b"BM" + b"\0" * 52 + other * 40)
                return subprocess.CompletedProcess(
                    args, 0,
                    f"gpu_driver=vulkan systems=20 image_uploads=5 save=ok "
                    f"territory={json.dumps(territory, separators=(',', ':'))} "
                    f"notifications={json.dumps(state, separators=(',', ':'))}",
                    "")

            with mock.patch("native_notification_runtime.subprocess.run",
                            side_effect=launch):
                result = validate_native_notification_export(
                    package, {}, fixture)
            self.assertEqual(len(calls), 2)
            self.assertIn("--load", calls[0])
            self.assertIn("--load", calls[1])
            self.assertEqual(len(result["notificationCaptures"]), 4)
            self.assertTrue(result["nativeNotificationFeed"])
            self.assertTrue(result["nativeNotificationObserverRedaction"])
            self.assertTrue(result["nativeNotificationContactFocus"])
            self.assertTrue(result["nativeNotificationReload"])

    def test_complete_actual_contract(self): self.exercise()
    def test_panel_required(self):
        with self.assertRaises(RuntimeError): self.exercise("panel")
    def test_items_required(self):
        with self.assertRaises(RuntimeError): self.exercise("items")
    def test_unread_required(self):
        with self.assertRaises(RuntimeError): self.exercise("unread")
    def test_diplomacy_bulletin_required(self):
        with self.assertRaises(RuntimeError): self.exercise("no_diplomacy")
    def test_contact_required(self):
        with self.assertRaises(RuntimeError): self.exercise("no_contact")
    def test_wrong_counterpart_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("counterpart")
    def test_wrong_focus_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("focus")
    def test_history_flood_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("history_flood")
    def test_damaged_payload_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("save")
    def test_blank_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("blank")
    def test_same_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("same_capture")
    def test_missing_contact_capture_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("capture")


if __name__ == "__main__":
    unittest.main()
