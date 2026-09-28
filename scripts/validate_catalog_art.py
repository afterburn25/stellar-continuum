"""Enforce complete, portable thumbnail coverage using only the Python standard library."""
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/visual/catalog'

def validate():
    catalog = json.loads((ART / 'catalog.json').read_text())
    index = json.loads((ROOT / 'data/research/v1/index.json').read_text())
    expected = {n['id']: n for f in index['domain_files'].values()
                for n in json.loads((ROOT / 'data/research/v1' / f).read_text())['nodes']}
    actual = {n['id']: n for n in catalog['research']}
    assert len(actual) == len(catalog['research']), 'Duplicate research binding'
    assert expected.keys() == actual.keys(), f'Research art coverage mismatch: {expected.keys() ^ actual.keys()}'
    for id, node in expected.items():
        assert actual[id]['name'] == node['name'], f'Stale name: {id}'
    modules = catalog['modules']
    assert len({m['id'] for m in modules}) == len(modules), 'Duplicate module binding'
    assert len({m['art'] for m in modules}) == len(modules), 'Module versions need distinct pictures'
    planned = {n['id']: n['art'] for n in catalog['plannedResearch']}
    assert len(planned) == len(catalog['plannedResearch']), 'Duplicate reserved unlock binding'
    assert not expected.keys() & planned.keys(), 'Planned unlock must be promoted deliberately'
    for module in modules:
        for id in module['research']:
            assert planned[id] == module['art'], f'Module/research artwork mismatch: {id}'
    arts = {n['art'] for key in ('research', 'plannedResearch', 'modules') for n in catalog[key]}
    provenance = json.loads((ART / 'production/provenance.json').read_text(encoding='utf-8'))
    records = {entry['id']: entry for entry in provenance['entries']}
    assert len(records) == len(provenance['entries']) and records.keys() == arts, 'Incomplete provenance'
    size = 0
    for art in arts:
        assert re.fullmatch(r'[a-z0-9_-]+', art), f'Unsafe asset identity: {art}'
        for folder, resolution in [('thumbnails', 128), ('portraits', 512)]:
            path = ART / folder / (art + '.png')
            assert path.is_file(), f'Missing {path.relative_to(ROOT)}'
            content = path.read_bytes()
            assert content[:8] == b'\x89PNG\r\n\x1a\n', f'Invalid PNG: {path}'
            assert struct.unpack('>II', content[16:24]) == (resolution, resolution), f'Incorrect dimensions: {path}'
            assert len(content) > 1000, f'Empty picture: {path}'
            assert records[art]['delivery'][folder]['sha256'] == hashlib.sha256(content).hexdigest(), f'Stale provenance: {path}'
            size += len(content)
    print(f'PASS: {len(actual)} research entries, {len(modules)} module versions, {len(planned)} reserved unlock bindings; {len(arts)} images in two sizes, {size / 1024 / 1024:.1f} MiB')

if __name__ == '__main__': validate()
