import hashlib,json,tempfile,unittest
from pathlib import Path
from native_environment_runtime import native_environment_asset_files

class EnvironmentAssets(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.sky='assets/visual/starfields/Sparse star background/sky.png'
        paths=[self.sky,'assets/visual/rings/thin-ring/radial.png']
        paths += [f'assets/visual/planets/sol-pluto-v2/{n}.png' for n in ('albedo','normal','properties','clouds','emission','thumbnail')]
        self.records=[]
        for p in paths:
            target=self.root/p;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(p.encode())
            self.records.append({'path':p,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
        self.write('data/planets/ring-types-v1.json',{'assets':[{'id':'thin-ring','status':'accepted'},{'id':'rejected-ring','status':'rejected'}]})
        self.write('data/stellar/starfields-v1.json',{'assets':[{'id':'sky','path':self.sky}]})
        self.write('data/stellar/starfield-asset-audit-v1.json',{'images':[{'id':'sky','path':self.sky,'accepted':True,'sha256':self.records[0]['sha256']}]})
        for p in ('giant-asset-audit-v1','ring-asset-audit-v1','deprecated-giant-art-v1'):self.write(f'data/planets/{p}.json',{})
        self.write('export/native-environment-assets.json',{'schemaVersion':1,'files':self.records})
    def write(self,p,value):
        file=self.root/p;file.parent.mkdir(parents=True,exist_ok=True);file.write_text(json.dumps(value),encoding='utf-8')
    def test_exact_reviewed_membership(self):
        result=native_environment_asset_files(self.root)
        self.assertEqual(len(result),15)
        self.assertTrue(all('rejected-ring' not in p for p in result))
    def test_rejected_ring_cannot_enter_package(self):
        self.records[1]['path']='assets/visual/rings/rejected-ring/radial.png'
        self.write('export/native-environment-assets.json',{'schemaVersion':1,'files':self.records})
        with self.assertRaisesRegex(RuntimeError,'Unreviewed'):native_environment_asset_files(self.root)
    def test_missing_ring_is_rejected(self):
        (self.root/'assets/visual/rings/thin-ring/radial.png').unlink()
        with self.assertRaises(FileNotFoundError):native_environment_asset_files(self.root)
    def test_modified_sky_is_rejected(self):
        (self.root/self.sky).write_bytes(b'changed')
        with self.assertRaisesRegex(RuntimeError,'changed'):native_environment_asset_files(self.root)
    def test_rehash_cannot_disguise_modified_source(self):
        (self.root/self.sky).write_bytes(b'changed');self.records[0]['sha256']=hashlib.sha256(b'changed').hexdigest()
        self.write('export/native-environment-assets.json',{'schemaVersion':1,'files':self.records})
        with self.assertRaisesRegex(RuntimeError,'original pixels'):native_environment_asset_files(self.root)
    def test_unapproved_sky_path_is_rejected(self):
        self.write('data/stellar/starfields-v1.json',{'assets':[{'id':'sky','path':'../outside.png'}]})
        with self.assertRaisesRegex(RuntimeError,'Unreviewed'):native_environment_asset_files(self.root)
    def test_duplicate_or_incomplete_package_rejected(self):
        self.records[1]=self.records[0]
        self.write('export/native-environment-assets.json',{'schemaVersion':1,'files':self.records})
        with self.assertRaisesRegex(RuntimeError,'duplicate'):native_environment_asset_files(self.root)

if __name__=='__main__':unittest.main()
