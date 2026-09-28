import json
import os
from pathlib import Path
import tempfile
import unittest
from pack_paths import load_pack


class PackPathsTests(unittest.TestCase):
    def test_relative_manifest_and_stale_host_paths_use_pack_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            resources = {
                "pythonPath": "python/Scripts/python.exe" if os.name == "nt" else "python/bin/python",
                "workerPath": "kokoro_worker.py", "modelPath": "kokoro-v1.0.onnx",
                "voicesPath": "voices-v1.0.bin",
            }
            for name in resources.values():
                resource = root / name
                resource.parent.mkdir(parents=True, exist_ok=True)
                resource.touch()
            manifest = root / "pack.json"
            for values in (resources, {key: str(root / "old-host" / name) for key, name in resources.items()}):
                manifest.write_text(json.dumps(values), encoding="utf-8")
                resolved = load_pack(manifest)
                for key, name in resources.items():
                    self.assertEqual(resolved[key], str((root / name).resolve()))

    def test_explicit_custom_resources_remain_supported_and_missing_files_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            custom = root / "custom-resource"
            custom.touch()
            manifest = root / "pack.json"
            manifest.write_text(json.dumps({key: str(custom) for key in
                ("pythonPath", "workerPath", "modelPath", "voicesPath")}), encoding="utf-8")
            self.assertEqual(set(load_pack(manifest).values()), {str(custom.resolve())})
            custom.unlink()
            with self.assertRaises(FileNotFoundError):
                load_pack(manifest)


if __name__ == "__main__":
    unittest.main()
