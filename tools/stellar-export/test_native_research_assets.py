import json
from pathlib import Path
import shutil
import tempfile
import unittest
from native_research_assets import native_research_asset_files

ROOT = Path(__file__).resolve().parents[2]

class ResearchArtPackagingTests(unittest.TestCase):
    def test_complete_current_research_only(self):
        files = native_research_asset_files(ROOT)
        self.assertEqual(len(files), 44)
        self.assertFalse(any("human_station" in path for path in files))

    def test_corrupt_missing_and_redirect_fail(self):
        with tempfile.TemporaryDirectory(prefix="stellar-research-art-") as directory:
            root = Path(directory)
            declaration = root / "export/native-research-assets.json"
            declaration.parent.mkdir(parents=True)
            original = json.loads((ROOT / "export/native-research-assets.json").read_text())
            for record in original["assets"]:
                target = root / record["path"]
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / record["path"], target)
            declaration.write_text(json.dumps(original))
            self.assertEqual(len(native_research_asset_files(root)), 44)
            target = root / original["assets"][-1]["path"]
            data = target.read_bytes()
            target.write_bytes(data + b"bad")
            with self.assertRaisesRegex(RuntimeError, "differs from reviewed"):
                native_research_asset_files(root)
            target.unlink()
            with self.assertRaisesRegex(RuntimeError, "Missing or duplicate"):
                native_research_asset_files(root)
            target.write_bytes(data)
            original["assets"][-1]["path"] = "../outside.png"
            declaration.write_text(json.dumps(original))
            with self.assertRaisesRegex(RuntimeError, "Unreviewed"):
                native_research_asset_files(root)

if __name__ == "__main__":
    unittest.main()
