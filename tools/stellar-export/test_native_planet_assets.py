import hashlib,json,tempfile,unittest
from pathlib import Path
from native_planet_runtime import native_planet_asset_files

class PlanetAssetManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        (self.root/'data/planets').mkdir(parents=True)
        (self.root/'export').mkdir()
        (self.root/'assets/visual/planets/approved').mkdir(parents=True)
        self.art={'assets':[{'id':'approved','materialId':'approved','status':'accepted','earthGeography':False}], 'rejectedAssets':[{'id':'rejected','status':'rejected','reason':'Wrong surface'}]}
        self.save_art()
        (self.root/'data/planets/planet-types-v1.json').write_text('{}')
        self.records=[]
        for name in ('albedo','normal','properties','clouds','emission','thumbnail'):
            relative=f'assets/visual/planets/approved/{name}.png'
            data=name.encode();(self.root/relative).write_bytes(data)
            self.records.append({'path':relative,'sha256':hashlib.sha256(data).hexdigest()})
        self.save()
    def save(self):
        (self.root/'export/native-planet-assets.json').write_text(json.dumps({'schemaVersion':1,'files':self.records}))
    def save_art(self):
        (self.root/'data/planets/planet-art-v1.json').write_text(json.dumps(self.art))
    def test_complete_approved_set(self):
        self.assertEqual(len(native_planet_asset_files(self.root)),9)
    def test_missing_map_rejected(self):
        self.records.pop();self.save()
        with self.assertRaisesRegex(RuntimeError,'Incomplete'):native_planet_asset_files(self.root)
    def test_duplicate_rejected(self):
        self.records[-1]=self.records[0];self.save()
        with self.assertRaisesRegex(RuntimeError,'duplicated'):native_planet_asset_files(self.root)
    def test_tampering_rejected(self):
        (self.root/self.records[0]['path']).write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError,'changed'):native_planet_asset_files(self.root)
    def test_unreviewed_path_rejected(self):
        self.records[-1]['path']='../unreviewed.png';self.save()
        with self.assertRaisesRegex(RuntimeError,'Unreviewed'):native_planet_asset_files(self.root)
    def test_overlapping_pools_rejected(self):
        self.art['rejectedAssets'][0]['id']='approved';self.save_art()
        with self.assertRaisesRegex(RuntimeError,'Overlapping'):native_planet_asset_files(self.root)
    def test_rejected_status_cannot_be_exported_as_accepted(self):
        self.art['assets'][0]['status']='rejected';self.save_art()
        with self.assertRaisesRegex(RuntimeError,'Invalid accepted'):native_planet_asset_files(self.root)
    def test_rejection_reason_required(self):
        self.art['rejectedAssets'][0]['reason']='';self.save_art()
        with self.assertRaisesRegex(RuntimeError,'invalid rejected'):native_planet_asset_files(self.root)
if __name__=='__main__':unittest.main()
