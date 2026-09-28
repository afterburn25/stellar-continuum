"""Native audio declaration checks against the repository and mutable fixtures."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from native_audio_assets import NATIVE_AUDIO_SOURCES, native_audio_asset_files


ROOT = Path(__file__).resolve().parents[2]


class NativeAudioAssetTests(unittest.TestCase):
    def test_repository_declaration_whitelists_exact_reviewed_files(self):
        files = native_audio_asset_files(ROOT)
        self.assertEqual(set(files), {destination for _, destination in NATIVE_AUDIO_SOURCES.values()})
        self.assertEqual(len(files), 14)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="stellar-native-audio-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        records = {}
        for key, (source, destination) in NATIVE_AUDIO_SOURCES.items():
            path = self.root / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("fixture audio " + key).encode())
            records[key] = {"source": source, "runtimePath": destination,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        declaration = self.root / "export/native-audio-assets.json"
        declaration.parent.mkdir(parents=True)
        declaration.write_text(json.dumps({"schemaVersion": 1, "assets": records}), encoding="utf-8")
        self.declaration = declaration

    def test_missing_and_tampered_files_are_rejected(self):
        for key, (source, _) in NATIVE_AUDIO_SOURCES.items():
            with self.subTest(key=key):
                path = self.root / source
                original = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing native audio"):
                    native_audio_asset_files(self.root)
                path.write_bytes(original + b"changed")
                with self.assertRaisesRegex(RuntimeError, "differs from reviewed content"):
                    native_audio_asset_files(self.root)
                path.write_bytes(original)

    def test_unreviewed_paths_schema_and_assets_are_rejected(self):
        original = self.declaration.read_text(encoding="utf-8")
        for field in ("source", "runtimePath"):
            with self.subTest(field=field):
                declaration = json.loads(original)
                declaration["assets"]["main-music"][field] = "../outside.mp3"
                self.declaration.write_text(json.dumps(declaration), encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, "Unreviewed native audio"):
                    native_audio_asset_files(self.root)
        declaration = json.loads(original)
        declaration["schemaVersion"] = 2
        self.declaration.write_text(json.dumps(declaration), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "Unsupported native audio"):
            native_audio_asset_files(self.root)
        declaration = json.loads(original)
        declaration["assets"]["extra"] = declaration["assets"]["main-music"]
        self.declaration.write_text(json.dumps(declaration), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "set differs from reviewed content"):
            native_audio_asset_files(self.root)


if __name__ == "__main__":
    unittest.main()
