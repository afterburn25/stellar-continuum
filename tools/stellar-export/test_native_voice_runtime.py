import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from native_voice_runtime import native_voice_asset_files


class NativeVoiceAssetTests(unittest.TestCase):
    def exercise(self, fault=None):
        with tempfile.TemporaryDirectory(prefix="stellar-voice-decl-") as temporary:
            root = Path(temporary)
            declaration = json.loads(
                (Path(__file__).resolve().parents[2] /
                 "export/native-voice-assets.json").read_text(encoding="utf-8"))
            for key, record in declaration["assets"].items():
                target = root / record["source"]
                target.parent.mkdir(parents=True, exist_ok=True)
                if fault == key:
                    target.write_bytes(b"corrupted")
                else:
                    target.write_bytes(b"content-" + key.encode())
                    record["sha256"] = hashlib.sha256(
                        target.read_bytes()).hexdigest()
            if fault == "missing":
                (root / "data/voice_profiles/roles.json").unlink()
            if fault == "extra":
                declaration["assets"]["bonus"] = dict(record)
            (root / "export").mkdir()
            (root / "export/native-voice-assets.json").write_text(
                json.dumps(declaration), encoding="utf-8")
            return native_voice_asset_files(root)

    def test_declaration_packages_exact_files(self):
        files = self.exercise()
        self.assertEqual(len(files), 3)
        self.assertIn("Data/voice_profiles/events.json", files)
        self.assertIn("Data/voice_profiles/human.json", files)
        self.assertIn("Data/voice_profiles/roles.json", files)

    def test_unreviewed_content_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("human")
    def test_missing_file_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("missing")
    def test_unreviewed_extra_rejected(self):
        with self.assertRaises(RuntimeError): self.exercise("extra")


if __name__ == "__main__":
    unittest.main()
