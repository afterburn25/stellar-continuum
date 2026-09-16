"""Fail closed on changed or undeclared native navigation art."""
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from native_navigation_assets import SOURCES, native_navigation_asset_files


class NativeNavigationAssetTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="stellar-navigation-art-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        source_root = Path(__file__).resolve().parents[2]
        self.declaration = self.root / "export/native-navigation-assets.json"
        for relative in ("export/native-navigation-assets.json",
                         *(path for pair in SOURCES.values() for path in pair)):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_root / relative, target)

    def test_reviewed_assets_export(self):
        files = native_navigation_asset_files(self.root)
        self.assertEqual(set(files), {pair[1] for pair in SOURCES.values()})
        self.assertTrue(all(path.suffix == ".png" for path in files.values()))

    def test_changed_svg_or_png_rejected(self):
        for relative in SOURCES["research"]:
            with self.subTest(path=relative):
                path = self.root / relative
                original = path.read_bytes()
                path.write_bytes(original + b"changed")
                with self.assertRaisesRegex(RuntimeError, "hash mismatch"):
                    native_navigation_asset_files(self.root)
                path.write_bytes(original)

    def test_missing_svg_or_png_rejected(self):
        for relative in SOURCES["shipyard"]:
            with self.subTest(path=relative):
                path = self.root / relative
                original = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing"):
                    native_navigation_asset_files(self.root)
                path.write_bytes(original)

    def test_unapproved_source_or_runtime_rejected(self):
        original = json.loads(self.declaration.read_text(encoding="utf-8"))
        for field in ("source", "runtimePath"):
            with self.subTest(field=field):
                declaration = json.loads(json.dumps(original))
                declaration["assets"]["research"][field] = "../../unapproved.png"
                self.declaration.write_text(json.dumps(declaration), encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, "path"):
                    native_navigation_asset_files(self.root)

    def test_malformed_declarations_fail_cleanly(self):
        original = json.loads(self.declaration.read_text(encoding="utf-8"))
        malformed = [[], {**original, "assets": []}, {**original, "size": 512},
                     {**original, "assets": {**original["assets"], "research": None}},
                     {**original, "assets": {**original["assets"], "extra": {}}}]
        for declaration in malformed:
            with self.subTest(declaration=declaration):
                self.declaration.write_text(json.dumps(declaration), encoding="utf-8")
                with self.assertRaises(RuntimeError):
                    native_navigation_asset_files(self.root)
        self.declaration.write_text("{broken", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "Cannot read"):
            native_navigation_asset_files(self.root)


if __name__ == "__main__":
    unittest.main()
