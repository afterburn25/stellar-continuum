"""Only reviewed, prepared 3D planet maps are shipped; source renders stay offline."""
import hashlib
import json

def native_planet_asset_files(root):
    art=json.loads((root/'data/planets/planet-art-v1.json').read_text(encoding='utf-8'))
    accepted_ids=set()
    for record in art['assets']:
        if record['status']!='accepted' or record['earthGeography'] or record['id'] in accepted_ids:
            raise RuntimeError('Invalid accepted planet image pool')
        accepted_ids.add(record['id'])
    rejected_ids=set()
    for record in art['rejectedAssets']:
        if record['status']!='rejected' or not record['reason'] or record['id'] in accepted_ids or record['id'] in rejected_ids:
            raise RuntimeError('Overlapping or invalid rejected planet image pool')
        rejected_ids.add(record['id'])
    expected={f"assets/visual/planets/{a['materialId']}/{name}.png"
              for a in art['assets'] for name in ('albedo','normal','properties','clouds','emission','thumbnail')}
    records=json.loads((root/'export/native-planet-assets.json').read_text(encoding='utf-8'))
    if records['schemaVersion']!=1 or len(expected)!=len(records['files']):
        raise RuntimeError('Incomplete prepared planet manifest')
    result={}
    for record in records['files']:
        relative=record['path']
        if relative not in expected or relative in result:
            raise RuntimeError('Unreviewed or duplicated planet runtime path')
        path=root/relative
        with path.open('rb') as f: digest=hashlib.file_digest(f,'sha256').hexdigest()
        if digest!=record['sha256']:raise RuntimeError(f'Prepared planet map changed: {relative}')
        result[relative]=path
    for name in ('planet-art-v1.json','planet-types-v1.json'):
        relative='data/planets/'+name;result[relative]=root/relative
    result['export/native-planet-assets.json']=root/'export/native-planet-assets.json'
    return result
