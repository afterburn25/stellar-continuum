import json
from pathlib import Path
import tempfile
import unittest
import zipfile

import native_support_runtime as runtime


def proof(**changes):
    value = {"mode": "success", "menu_started": True, "key_started": True,
             "settings_blocked": True, "canonical_unchanged": True,
             "save_unchanged": True, "paused": True, "first": "one",
             "second": "two", "error": ""}
    value.update(changes)
    return "support=" + json.dumps(value, separators=(",", ":"))


def bundle(path: Path, save=b"{}", names=None, corrupt=False):
    names = names or ["session.log", "system.txt", "campaign.player17.json"]
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        for name in names:
            content = (b"[support] 2026-09-15T00:00:00Z Local diagnostic export requested." if name == "session.log" else
                       b"Runtime=native-c++23\nRendererBackend=vulkan\nViewport=1280x720\nSaveIncluded=yes\n" if name == "system.txt" else save)
            archive.writestr(name, content)
    if corrupt:
        data = bytearray(path.read_bytes())
        data[40] ^= 0x7f
        path.write_bytes(data)


class SupportProofTests(unittest.TestCase):
    def test_complete_success_and_failure_proofs(self):
        self.assertEqual(runtime.support_proof(proof(), "success")["first"], "one")
        failed = proof(mode="failure", first="", second="", error="blocked")
        self.assertEqual(runtime.support_proof(failed, "failure")["error"], "blocked")

    def test_strict_schema_types_and_duplicate_keys(self):
        failures = ["", proof(save_unchanged=1), proof(extra=True), proof(error="unexpected"),
                    "support={\"mode\":\"success\",\"mode\":\"failure\"}",
                    proof() + "\n" + proof(), proof() + "\nsupport=malformed",
                    "support=[]", proof(canonical_unchanged=False), proof(first="two")]
        for value in failures:
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                runtime.support_proof(value, "success")


class SupportBundleValidationTests(unittest.TestCase):
    def test_wrong_viewport_and_missing_save_timestamp_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "support.zip"; bundle(path)
            with self.assertRaises(RuntimeError):
                runtime._validate_bundle(path, b"{}", 1920, 1080)
            save = root / "save.json"
            save.write_text(json.dumps({"FormatVersion":17,"Galaxy":{"Systems":[{}]*500}}), encoding="utf-8")
            with self.assertRaises(RuntimeError): runtime._paused_payload(save)

    def test_valid_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "support.zip"; bundle(path, b"saved")
            runtime._validate_bundle(path, b"saved", 1280, 720)

    def test_corruption_entry_and_save_mismatch_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cases = [("bad-name", ["session.log", "system.txt", "unexpected.json"], False, b"{}"),
                     ("bad-crc", None, True, b"{}"), ("bad-save", None, False, b"different")]
            for label, names, corrupt, expected in cases:
                with self.subTest(label=label):
                    path = root / (label + ".zip"); bundle(path, b"{}", names, corrupt)
                    with self.assertRaises(RuntimeError): runtime._validate_bundle(path, expected, 1280, 720)

    def test_path_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); support = root / "support"; support.mkdir()
            outside = root / "outside.zip"; bundle(outside)
            with self.assertRaises(RuntimeError): runtime._contained_bundle(str(outside), support)


if __name__ == "__main__":
    unittest.main()
