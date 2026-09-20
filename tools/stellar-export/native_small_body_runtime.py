"""Exact named user artwork. Runtime creates bounded transparent render resources."""
import hashlib
import json

POOLS=('Isolated rocky asteroid bodies','Isolated icy asteroid bodies',
       'Asteroid dust - particulate layer','Ice dust - frozen particulate layer',
       'Asteroid belt far-view layer','Ice belt far-view layer','Ice cluster layer',
       'Dense debris disk layer','Shattered belt - broken field')

def native_small_body_asset_files(root):
    declaration=json.loads((root/'export/native-small-body-assets.json').read_text(encoding='utf-8'))
    records=declaration.get('assets',[])
    if declaration.get('schemaVersion')!=1 or len(records)!=36:
        raise RuntimeError('Small-body artwork requires all 36 named sources')
    result={}
    for i,record in enumerate(records):
        name=f'{POOLS[i//4]} {i%4+1}.png'
        relative=f'assets/visual/small-bodies/{name}'
        if record.get('source')!=relative or record.get('runtimePath')!=relative or record.get('name')!=name or record.get('pool')!=i//4 or record.get('variant')!=i%4+1:
            raise RuntimeError('Unreviewed small-body asset identity')
        path=root/relative
        if hashlib.sha256(path.read_bytes()).hexdigest()!=record.get('sha256'):
            raise RuntimeError(f'Small-body asset hash mismatch: {name}')
        result[relative]=path
    return result
