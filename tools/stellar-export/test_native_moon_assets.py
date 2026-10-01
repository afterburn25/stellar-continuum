import hashlib,json,tempfile,unittest
from pathlib import Path
from native_moon_assets import native_moon_asset_files

class MoonAssets(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.records=[]
        for moon in ('sol-io','sol-europa'):
            for name in ('albedo','normal','properties'):
                p=f'assets/visual/moons/{moon}/{name}.png'
                target=self.root/p;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(p.encode())
                self.records.append({'path':p,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
        self.write('data/planets/moon-asset-audit-v1.json',
                   {'images':[{'id':'sol-io','status':'accepted'},{'id':'sol-europa','status':'accepted'}]})
        self.write('export/native-moon-assets.json',{'schemaVersion':1,'files':self.records})
    def write(self,p,value):
        file=self.root/p;file.parent.mkdir(parents=True,exist_ok=True);file.write_text(json.dumps(value),encoding='utf-8')
    def test_exact_reviewed_membership(self):
        result=native_moon_asset_files(self.root)
        self.assertEqual(len(result),8)
        self.assertIn('Data/planets/moon-asset-audit-v1.json',result)
        self.assertIn('export/native-moon-assets.json',result)
    def test_unreviewed_moon_cannot_enter_package(self):
        self.records[0]['path']='assets/visual/moons/sol-unapproved/albedo.png'
        self.write('export/native-moon-assets.json',{'schemaVersion':1,'files':self.records})
        with self.assertRaisesRegex(RuntimeError,'Unreviewed or duplicate'):native_moon_asset_files(self.root)
    def test_missing_material_is_rejected(self):
        (self.root/'assets/visual/moons/sol-io/albedo.png').unlink()
        with self.assertRaisesRegex(RuntimeError,'Missing reviewed moon material'):native_moon_asset_files(self.root)
    def test_modified_material_is_rejected(self):
        (self.root/'assets/visual/moons/sol-io/normal.png').write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError,'changed'):native_moon_asset_files(self.root)
    def test_rejected_moon_blocks_package(self):
        self.write('data/planets/moon-asset-audit-v1.json',
                   {'images':[{'id':'sol-io','status':'rejected'},{'id':'sol-europa','status':'accepted'}]})
        with self.assertRaisesRegex(RuntimeError,'Unreviewed moon image'):native_moon_asset_files(self.root)
    def test_traversal_path_is_rejected(self):
        self.records[0]['path']='assets/visual/moons/../escape.png'
        self.write('export/native-moon-assets.json',{'schemaVersion':1,'files':self.records})
        with self.assertRaisesRegex(RuntimeError,'Unreviewed or duplicate'):native_moon_asset_files(self.root)

if __name__=='__main__':
    unittest.main()
