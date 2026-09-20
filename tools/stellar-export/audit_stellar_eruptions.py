"""Read-only provenance/sequence audit; never infers cross-job variant matches."""
from pathlib import Path
import argparse, collections, hashlib, json, re
from PIL import Image

JOB = re.compile(r'([0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12})_(\d+)(?: \(\d+\))?$', re.I)
STAGES = ('buildup', 'rise', 'peak', 'decay')

def audit(source):
    rows = []
    for path in sorted(source.rglob('*')):
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        family = relative.parts[0]
        spectral = re.fullmatch(r'([OBAFGKM])-Class Stars', family)
        frame = re.search(r'Frame ([1-4])', str(relative.parent))
        variant = JOB.search(path.stem)
        with path.open('rb') as stream:
            sha = hashlib.file_digest(stream, 'sha256').hexdigest()
        row = dict(path=relative.as_posix(), starClass=spectral[1] if spectral else 'generic',
                   eruptionType='FLARE' if spectral else {'CME': 'CME', 'Super Flares': 'SUPERFLARE', 'Small prominence loop': 'SMALL_PROMINENCE'}.get(family, 'UNMAPPED'),
                   stage=STAGES[int(frame[1])-1] if frame else 'unlabelled',
                   variantId=(variant[1].lower() + ':' + variant[2]) if variant else None,
                   sha256=sha, mapped=bool(spectral or family in ('CME','Super Flares','Small prominence loop')),
                   sequenceComplete=False, transparencyPreprocessed=False, used=False)
        try:
            with Image.open(path) as img:
                img.load()
                row.update(width=img.width, height=img.height, mode=img.mode,
                           pixelSha256=hashlib.sha256(img.convert('RGBA').tobytes()).hexdigest(),
                           alphaRange=list(img.getchannel('A').getextrema()) if 'A' in img.getbands() else [255,255])
        except Exception as error:
            row['error'] = str(error)
        rows.append(row)
    hashes = collections.defaultdict(list)
    identities = collections.defaultdict(list)
    for row in rows:
        hashes[row.get('pixelSha256', row['sha256'])].append(row['path'])
        identities[(row['starClass'], row['eruptionType'], row['variantId'])].append(row)
    for records in identities.values():
        complete = {r['stage'] for r in records} >= set(STAGES)
        for r in records:
            r['sequenceComplete'] = complete
    folders = []
    for family in sorted({r['path'].split('/')[0] for r in rows}):
        subset = [r for r in rows if r['path'].split('/')[0] == family]
        folders.append(dict(folder=family, count=len(subset), stages=dict(collections.Counter(r['stage'] for r in subset)), completeSequences=len({r['variantId'] for r in subset if r['sequenceComplete']})))
    return dict(schemaVersion=1, sourceRoot=str(source), files=rows, folders=folders,
                duplicatePixelGroups=[paths for paths in hashes.values() if len(paths)>1],
                ambiguities=['Job UUID plus image index is the only unique variant identity in the supplied filenames; unrelated job UUIDs are not paired.',
                             'Generic prominence, superflare and CME folders have no stage or spectral labels.'])

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = audit(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'files':len(result['files']), 'folders':result['folders'], 'duplicatePixelGroups':len(result['duplicatePixelGroups']), 'ambiguities':result['ambiguities']}, indent=2))
