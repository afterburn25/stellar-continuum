import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from native_small_body_runtime import native_small_body_asset_files, POOLS

ROOT=Path(__file__).resolve().parents[2]
class SmallBodyArtworkTests(unittest.TestCase):
    def test_all_named_sources_and_hashes(self):
        files=native_small_body_asset_files(ROOT)
        self.assertEqual(len(files),36)
        self.assertEqual(set(files),{f'assets/visual/small-bodies/{name} {variant}.png' for name in POOLS for variant in range(1,5)})
    def test_missing_or_substituted_identity_rejected(self):
        manifest=json.loads((ROOT/'export/native-small-body-assets.json').read_text())
        with TemporaryDirectory() as directory:
            root=Path(directory);(root/'export').mkdir()
            for change in ('missing','identity'):
                value=json.loads(json.dumps(manifest))
                if change=='missing':value['assets'].pop()
                else:value['assets'][0]['name']='unrelated.png'
                (root/'export/native-small-body-assets.json').write_text(json.dumps(value))
                with self.assertRaises(RuntimeError):native_small_body_asset_files(root)
if __name__=='__main__':unittest.main()
