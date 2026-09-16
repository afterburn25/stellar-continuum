import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest

from native_surface_art_assets import native_surface_art_asset_files

ROOT = Path(__file__).resolve().parents[2]


class SurfaceArtAssetTests(unittest.TestCase):
    def test_exact_packaged_art_and_credits(self):
        files = native_surface_art_asset_files(ROOT)
        self.assertEqual(set(files), {"assets/visual/surface/temperate-ground-albedo-v1.png",
                                     "Licenses/Surface-art-sources.md"})

    def test_tamper_missing_and_redirect_rejected(self):
        with tempfile.TemporaryDirectory(prefix="stellar-surface-assets-") as temporary:
            root = Path(temporary)
            manifest = root / "export/native-surface-art-assets.json"
            manifest.parent.mkdir()
            declaration = json.loads((ROOT / "export/native-surface-art-assets.json").read_text())
            for record in declaration["assets"].values():
                target = root / record["source"]
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / record["source"], target)
            manifest.write_text(json.dumps(declaration))
            for record in declaration["assets"].values():
                target = root / record["source"]
                original = target.read_bytes()
                target.write_bytes(original + b"changed")
                with self.assertRaisesRegex(RuntimeError, "differs from reviewed"):
                    native_surface_art_asset_files(root)
                target.unlink()
                with self.assertRaisesRegex(RuntimeError, "Missing native surface"):
                    native_surface_art_asset_files(root)
                target.write_bytes(original)
            declaration["assets"]["temperate-ground-albedo-v1"]["runtimePath"] = "../outside.png"
            manifest.write_text(json.dumps(declaration))
            with self.assertRaisesRegex(RuntimeError, "Unreviewed native surface"):
                native_surface_art_asset_files(root)


if __name__ == "__main__":
    unittest.main()
