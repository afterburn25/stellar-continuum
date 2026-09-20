"""Reviewed separate rings, Pluto and system skies, with exact package membership."""
import hashlib
import json
from pathlib import PurePosixPath


def native_environment_asset_files(root):
    rings = json.loads((root/'data/planets/ring-types-v1.json').read_text(encoding='utf-8'))
    skies = json.loads((root/'data/stellar/starfields-v1.json').read_text(encoding='utf-8'))
    audit = json.loads((root/'data/stellar/starfield-asset-audit-v1.json').read_text(encoding='utf-8'))
    approved_skies = {a['id']: a for a in audit['images'] if a['accepted']}
    expected = set()
    ring_ids = set()
    for ring in rings['assets']:
        if ring['status'] != 'accepted':
            continue
        identity = ring['id']
        if not identity or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in identity) or identity in ring_ids:
            raise RuntimeError('Invalid reviewed ring identity')
        ring_ids.add(identity)
        expected.add(f'assets/visual/rings/{identity}/radial.png')
    seen = set()
    for sky in skies['assets']:
        name = sky['path']
        path = PurePosixPath(name)
        review = approved_skies.get(sky['id'])
        if (sky['id'] in seen or review is None or review['path'] != name or
                path.is_absolute() or '..' in path.parts or '\\' in name or
                not name.startswith('assets/visual/starfields/') or name in expected):
            raise RuntimeError('Unreviewed or unsafe system background')
        seen.add(sky['id'])
        expected.add(name)
    if seen != set(approved_skies):
        raise RuntimeError('Incomplete approved background pool')
    for name in ('albedo','normal','properties','clouds','emission','thumbnail'):
        expected.add(f'assets/visual/planets/sol-pluto-v2/{name}.png')
    manifest = json.loads((root/'export/native-environment-assets.json').read_text(encoding='utf-8'))
    if manifest['schemaVersion'] != 1 or len(manifest['files']) != len(expected):
        raise RuntimeError('Incomplete environment package manifest')
    result = {}
    for record in manifest['files']:
        name = record['path']
        if name not in expected or name in result:
            raise RuntimeError('Unreviewed or duplicate environment path')
        path = root/name
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream,'sha256').hexdigest()
        if digest != record['sha256']:
            raise RuntimeError(f'Reviewed environment asset changed: {name}')
        if name.startswith('assets/visual/starfields/'):
            source = next(a for a in approved_skies.values() if a['path'] == name)
            if digest != source['sha256']:
                raise RuntimeError('Starfield original pixels changed')
        result[name] = path
    for name in ('data/planets/ring-types-v1.json','data/planets/giant-asset-audit-v1.json',
                 'data/planets/ring-asset-audit-v1.json','data/planets/deprecated-giant-art-v1.json',
                 'data/stellar/starfields-v1.json','data/stellar/starfield-asset-audit-v1.json',
                 'export/native-environment-assets.json'):
        result[name] = root/name
    return result
