import hashlib
import json
from pathlib import Path
import unittest
from import_galaxy_assets import identify, MORPHOLOGIES, STATES
from native_galaxy_art_runtime import native_galaxy_art_asset_files

ROOT=Path(__file__).resolve().parents[2]
class GalaxyAssetTests(unittest.TestCase):
    def test_filename_identity(self):
        for morphology in MORPHOLOGIES:
            for state in (None,*STATES):
                for words,variant in [('Stars Included','stars_included'),('Gas-Dust Only','gas_dust_only')]:
                    name=f'{morphology} GALAXY — {state or ""} -- {words}.png'
                    self.assertEqual(identify(name),(morphology,state,variant))
        for name in ('unknown.png','Spiral galaxy Elliptical galaxy stars included.png','Spiral galaxy active aging stars included.png','Ring galaxy stars included gas dust only.png'):
            with self.assertRaises(ValueError): identify(name)
    def test_all_approved_files_and_pairing(self):
        manifest=json.loads((ROOT/'data/stellar/galaxy-visuals-v1.json').read_text())
        assets=manifest['assets'];self.assertEqual(len(assets),12)
        seen=set()
        for asset in assets:
            identity=identify(asset['sourceFilename'])
            self.assertEqual(identity,(asset['morphology'],asset['populationState'],asset['variant']))
            self.assertNotIn(identity,seen);seen.add(identity)
            self.assertEqual(hashlib.sha256((ROOT/asset['path']).read_bytes()).hexdigest(),asset['sha256'])
            self.assertAlmostEqual(asset['sourceDimensions'][0]/asset['sourceDimensions'][1],asset['runtimeDimensions'][0]/asset['runtimeDimensions'][1],delta=.003)
            if asset['variant']=='gas_dust_only':
                mask=manifest['densityMasks'][asset['id']]
                self.assertEqual(mask['width']*mask['height'],len(mask['values']))
                self.assertTrue(any(v==0 for v in mask['values']))
                self.assertTrue(any(v>50 for v in mask['values']))
                self.assertIn(asset['id'],manifest['footprintFrames'])
        self.assertEqual(seen,{(m,None,v) for m in MORPHOLOGIES for v in ('stars_included','gas_dust_only')})
        packaged=native_galaxy_art_asset_files(ROOT)
        self.assertTrue(all(a['path'] in packaged for a in assets))
    def test_widescreen_revisions_keep_both_variants(self):
        manifest=json.loads((ROOT/'data/stellar/galaxy-visuals-v1.json').read_text())
        edits=json.loads((ROOT/'export/galaxy-asset-edits.json').read_text())['edits']
        self.assertEqual(len(edits),4)
        for asset in manifest['assets']:
            if asset['morphology'] not in ('irregular','spiral'): continue
            self.assertEqual(asset['runtimeDimensions'],[1280,720])
            edit=edits[asset['sourceFilename']]
            self.assertEqual(asset['editedSourcePath'],edit['path'])
            self.assertEqual(asset['sourceSha256'],edit['sha256'])
            self.assertEqual(hashlib.sha256((ROOT/edit['path']).read_bytes()).hexdigest(),edit['sha256'])
            self.assertNotEqual(asset['originalSourceDimensions'],asset['sourceDimensions'])
if __name__=='__main__': unittest.main()
