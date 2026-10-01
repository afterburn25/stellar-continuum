"""Reviewed Sol moon material maps, with exact package membership."""
import hashlib
import json
from pathlib import PurePosixPath


def native_moon_asset_files(root):
    audit = json.loads((root/'data/planets/moon-asset-audit-v1.json').read_text(encoding='utf-8'))
    expected = set()
    moon_ids = set()
    for image in audit['images']:
        if image['status'] != 'accepted':
            raise RuntimeError('Unreviewed moon image in the audit registry')
        identity = image['id']
        if (not identity.startswith('sol-') or len(identity) <= 4 or
                any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in identity) or
                identity in moon_ids):
            raise RuntimeError('Invalid reviewed moon identity')
        moon_ids.add(identity)
        for name in ('albedo', 'normal', 'properties'):
            expected.add(f'assets/visual/moons/{identity}/{name}.png')
    manifest = json.loads((root/'export/native-moon-assets.json').read_text(encoding='utf-8'))
    if manifest['schemaVersion'] != 1 or len(manifest['files']) != len(expected):
        raise RuntimeError('Incomplete moon package manifest')
    result = {}
    for record in manifest['files']:
        name = record['path']
        path = PurePosixPath(name)
        if (name not in expected or name in result or path.is_absolute() or
                '..' in path.parts or '\\' in name):
            raise RuntimeError('Unreviewed or duplicate moon runtime path')
        source = root/name
        if not source.is_file():
            raise RuntimeError(f'Missing reviewed moon material: {name}')
        with source.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if digest != record['sha256']:
            raise RuntimeError(f'Reviewed moon material changed: {name}')
        result[name] = source
    # The audit registry ships as data; the manifest rides along for provenance.
    result['Data/planets/moon-asset-audit-v1.json'] = root/'data/planets/moon-asset-audit-v1.json'
    result['export/native-moon-assets.json'] = root/'export/native-moon-assets.json'
    return result
