from pathlib import Path
import unittest
from import_phenomenon_assets import CATEGORIES, classify, validate_manifest
from native_galaxy_art_runtime import native_galaxy_art_asset_files

ROOT = Path(__file__).resolve().parents[2]

class PhenomenonAssetTests(unittest.TestCase):
    def test_all_originals_classified_and_packaged(self):
        manifest, categories = validate_manifest(ROOT)
        self.assertEqual(len(manifest['assets']), 48)
        self.assertEqual(set(categories), set(CATEGORIES))
        self.assertEqual(categories['mixed nebula background'], 3)
        self.assertEqual(categories['large cinematic nebula'], 5)
        packaged = native_galaxy_art_asset_files(ROOT)
        self.assertTrue(all(a['path'] in packaged for a in manifest['assets']))

    def test_filename_normalization(self):
        for category, (family, blend) in CATEGORIES.items():
            for filename in (category+' 2.png', category.upper().replace(' ', '_')+'_02.png', category.replace(' ', ' - ')+' 2.png'):
                self.assertEqual(classify(filename), (category,2,family,blend))

    def test_unknown_and_path_names_rejected(self):
        for filename in ('space.png','Nebula 9.png','Dark Nebula.png','../Dark Nebula 2.png','nested\\Dark Nebula 2.png'):
            with self.assertRaises(ValueError): classify(filename)

if __name__ == '__main__': unittest.main()
