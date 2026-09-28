"""Import original phenomenon artwork without changing any source pixels."""
from pathlib import Path
import argparse, hashlib, json, re, shutil, struct

CATEGORIES = {
    'blue reflection nebula': ('reflection', 'luminous'),
    'cleaner gas cloud for star map use': ('diffuse_gas', 'luminous'),
    'dark nebula': ('dark_nebula', 'obscuring'),
    'diffuse space gas cloud': ('diffuse_gas', 'luminous'),
    'emission nebula': ('emission', 'luminous'),
    'faint nebula': ('diffuse_gas', 'luminous'),
    'large cinematic nebula': ('mixed_nebula', 'luminous'),
    'mixed nebula background': ('mixed_nebula', 'luminous'),
    'molecular cloud stellar nursery': ('molecular_cloud', 'luminous'),
    'rare energetic space phenomenon': ('rare_energetic', 'luminous'),
    'star forming region h ii region': ('hii_region', 'luminous'),
    'supernova remnant': ('supernova_remnant', 'luminous'),
}

def classify(filename):
    if '/' in filename or '\\' in filename:
        raise ValueError(f'Artwork filename must be a basename: {filename}')
    normalized = re.sub(r'[-_\s]+', ' ', Path(filename).stem).strip().lower()
    match = re.fullmatch(r'(.+?) (\d+)', normalized)
    if not match or match[1] not in CATEGORIES:
        raise ValueError(f'Unrecognized phenomenon filename: {filename}')
    category, variant = match[1], int(match[2])
    return category, variant, *CATEGORIES[category]

def validate_manifest(root):
    manifest = json.loads((root/'data/stellar/phenomenon-art-v1.json').read_text())
    if manifest.get('schemaVersion') != 1:
        raise ValueError('Unsupported phenomenon art manifest version')
    seen, mappings, categories = set(), set(), {}
    for asset in manifest['assets']:
        category, variant, family, blend = classify(asset['filename'])
        key = (category, variant)
        if key in mappings or asset['assetId'] in seen:
            raise ValueError(f'Duplicate file mapping: {asset["filename"]}')
        mappings.add(key); seen.add(asset['assetId'])
        if (family, blend, variant) != (asset['functionalType'], asset['blendMode'], asset['variantIndex']):
            raise ValueError(f'Incorrect classification: {asset["filename"]}')
        path = root/'assets/visual/phenomena'/asset['filename']
        if asset['path'] != path.relative_to(root).as_posix():
            raise ValueError('Unapproved artwork path')
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != asset['sha256']:
            raise ValueError(f'Artwork hash mismatch: {path}')
        dimensions = struct.unpack('>II', data[16:24])
        if list(dimensions) != asset['dimensions'] or abs(dimensions[0]/dimensions[1]-asset['aspectRatio']) > 1e-9:
            raise ValueError(f'Artwork dimensions mismatch: {path}')
        if asset['systemViewEligible'] != (family not in ('supernova_remnant','rare_energetic')):
            raise ValueError('Invalid local eligibility')
        categories[category] = categories.get(category,0)+1
    if set(categories) != set(CATEGORIES):
        raise ValueError(f'Missing categories: {set(CATEGORIES)-set(categories)}')
    disk = {p.name for p in (root/'assets/visual/phenomena').glob('*.png')}
    if disk != {a['filename'] for a in manifest['assets']}:
        raise ValueError('Unmapped or missing phenomenon files')
    return manifest, categories

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--source', type=Path)
    args = parser.parse_args()
    if args.source:
        destination = args.root/'assets/visual/phenomena'
        destination.mkdir(parents=True,exist_ok=True)
        assets = []
        for source in sorted(args.source.glob('*.png')):
            category, variant, family, blend = classify(source.name)
            data = source.read_bytes(); width, height = struct.unpack('>II', data[16:24])
            path = destination/source.name
            if not path.exists() or path.read_bytes()!=data:
                shutil.copy2(source,path)
            assets.append(dict(assetId=category.replace(' ','-')+f'-{variant}',filename=source.name,
                sourceCategory=re.sub(r'\s+\d+$','',source.stem),functionalType=family,variantIndex=variant,
                systemViewEligible=family not in ('supernova_remnant','rare_energetic'),galaxyViewEligible=True,
                blendMode=blend,aspectRatio=width/height,dimensions=[width,height],
                path=path.relative_to(args.root).as_posix(),sha256=hashlib.sha256(data).hexdigest()))
        (args.root/'data/stellar/phenomenon-art-v1.json').write_text(json.dumps(dict(schemaVersion=1,assets=assets,roles={'undisclosed_core_fog':'cleaner-gas-cloud-for-star-map-use-2'}),indent=2)+'\n')
    manifest,categories=validate_manifest(args.root)
    for category,count in categories.items(): print(f'{category}: {count}')
    print(f'Validated {len(manifest["assets"])} original phenomenon assets')
