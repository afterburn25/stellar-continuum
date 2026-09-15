import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest import mock

import native_navigation_runtime as runtime


PROOF = {"switches": 4, "final_workspace": "relations", "credits_before": 500,
         "credits_after": 500, "no_charge": True, "canonical_payload_unchanged": True,
         "menu_blocked": True, "modal_blocked": True, "pause_retained": True, "day_unchanged": True}


class NavigationProofTests(unittest.TestCase):
    def test_complete_proof(self):
        self.assertEqual(runtime.navigation_proof("ok navigation=" + json.dumps(PROOF)), PROOF)

    def test_missing_duplicate_malformed_proof(self):
        for output in ("ok", " navigation={broken}",
                       (" navigation=" + json.dumps(PROOF) + "\n") * 2):
            with self.subTest(output=output), self.assertRaises(RuntimeError):
                runtime.navigation_proof(output)

    def test_false_and_spoofed_proofs(self):
        changes = [("switches", 3), ("final_workspace", "research"),
                   ("credits_after", 499), ("credits_before", float("nan"))]
        changes.extend((key, value) for key in PROOF if PROOF[key] is True
                       for value in (False, 1, "true", None))
        for key, value in changes:
            with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                runtime.navigation_proof(" navigation=" + json.dumps({**PROOF, key: value}))


class NavigationRuntimeTests(unittest.TestCase):
    def run_replay(self, failure=None):
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary) / "package"
            package.mkdir()
            calls = []

            def launch(args, **kwargs):
                calls.append(args)
                self.assertTrue(Path(args[0]).is_absolute())
                self.assertNotEqual(Path(kwargs["cwd"]), package)
                loading = "--load" in args
                save = Path(args[args.index("--save-path") + 1])
                capture = Path(args[args.index("--navigation-smoke") + 1])
                width = int(args[args.index("--width") + 1])
                height = int(args[args.index("--height") + 1])
                if loading:
                    self.assertTrue(save.is_file())
                payload = {"FormatVersion": 17, "SavedAtUtc": "reload" if loading else "fresh",
                           "Galaxy": {"Systems": list(range(500))}, "Treasury": 500}
                if loading and failure == "state":
                    payload["Treasury"] = 499
                save.write_text(json.dumps(payload), encoding="utf-8")
                if not (loading and failure == "missing capture"):
                    if loading and failure == "size":
                        width = 1280
                    pixels = b"\xff\x30\x10\x00" + b"\0" * (width * height * 4 - 4)
                    header = bytearray(54)
                    header[:2] = b"BM"
                    struct.pack_into("<I", header, 2, 54 + len(pixels))
                    struct.pack_into("<I", header, 10, 54)
                    struct.pack_into("<IiiHH", header, 14, 40, width, height, 1, 32)
                    capture.write_bytes(header + pixels)
                return subprocess.CompletedProcess(args, 0,
                    "native-map smoke ok: gpu_driver=vulkan systems=500 save=ok navigation=" + json.dumps(PROOF), "")

            with mock.patch.object(runtime.subprocess, "run", side_effect=launch):
                result = runtime.validate_native_navigation_export(package, {})
            self.assertEqual(len(calls), 2)
            self.assertEqual(len(set(result["navigationCaptures"])), 2)
            self.assertTrue(result["nativeNavigationPausedReload"])

    def test_relocated_two_resolution_reload(self):
        self.run_replay()

    def test_reload_requires_own_correct_capture(self):
        for failure in ("missing capture", "size"):
            with self.subTest(failure=failure), self.assertRaisesRegex(RuntimeError, "capture"):
                self.run_replay(failure)

    def test_reload_preserves_whole_campaign(self):
        with self.assertRaisesRegex(RuntimeError, "changed during paused reload"):
            self.run_replay("state")


if __name__ == "__main__":
    unittest.main()
