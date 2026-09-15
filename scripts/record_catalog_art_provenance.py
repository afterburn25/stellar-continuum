"""Record hashes and production methods after the catalog images have been baked."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/visual/catalog'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--masters', type=Path, required=True)
    parser.add_argument('--date', required=True)
    args = parser.parse_args()
    catalog = json.loads((ART / 'catalog.json').read_text(encoding='utf-8'))
    jobs = json.loads((ART / 'production/jobs.json').read_text(encoding='utf-8'))['jobs']
    entries = []
    for job in jobs:
        master = args.masters / (job['id'] + '.png')
        dimensions = struct.unpack('>II', master.read_bytes()[16:24])
        entries.append(dict(id=job['id'], method='OpenAI built-in image_gen; original text-to-image',
            prompt=job['prompt'], masterFile=master.name, masterSha256=digest(master),
            masterDimensions=list(dimensions), sourceReferences=[],
            status='Original generated concept illustration; runtime optimized'))
    for module in catalog['modules']:
        if 'model' in module:
            entries.append(dict(id=module['art'], method='Godot 4.7.2 native GLB studio render',
                model=module['model'], modelSha256=module['modelSha256'],
                renderer='tools/CatalogArtBake.cs', status='Render of existing project fleet candidate'))
    for entry in entries:
        entry['delivery'] = {}
        for folder, resolution in [('thumbnails', 128), ('portraits', 512)]:
            relative = folder + '/' + entry['id'] + '.png'
            path = ART / relative
            entry['delivery'][folder] = dict(path=relative, width=resolution, height=resolution,
                sha256=digest(path), bytes=path.stat().st_size)
    provenance = dict(schemaVersion=1, createdDate=args.date,
        ownership='Project-directed generated illustrations and renders of project-authored models. No third-party image downloads or reference images.',
        intendedUse='Stellar Continuum commercial-game UI candidates; generated assets are not claimed to be exclusive or independently copyrightable.',
        processing='Square sources resized with Godot Lanczos to 512 and 128 pixels; no crops, repainting or recoloring.',
        sources=json.loads((ART / 'production/source.json').read_text(encoding='utf-8')),
        researchCoverage='370 explicit technology bindings share 21 research-family illustrations. Module unlock bindings reserve the matching module illustration.',
        entries=entries)
    (ART / 'production/provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    print(f'Recorded provenance for {len(entries)} images')

if __name__ == '__main__':
    main()
