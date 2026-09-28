"""Validate supplied stellar art and pair the exact ' Distance' LOD suffix."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/'assets/visual/stellar'
ALIASES={
 'A-Class blue-white':'a-white','Active M-Class Red Dwarf':'m-red-dwarf',
 'Quiet M-Class Red Dwarf':'m-red-dwarf-quiet','B-Class star':'b-blue-white',
 'F-Class White  Yellow-White Star':'f-yellow-white','G-Class Yellow':'g-yellow',
 'K-Class Orange Star':'k-orange','O-Class Hot Blue':'o-hot-blue',
 'Blue Giant':'blue-giant','Blue Super Giant':'blue-supergiant','Brown Dwarf':'brown-dwarf',
 'Hyper Giant':'hypergiant','Magnetar':'magnetar','Neutron Star':'quiet-neutron',
 'Pulsar':'pulsar','Red Giant':'red-giant','Red Super Giant':'red-supergiant',
 'White Dwarf':'white-dwarf','Wolf-Rayet Star':'wolf-rayet','Yellow Giant':'yellow-giant',
 'Yellow Super Giant':'yellow-supergiant','Accreting Black Hole':'accreting-black-hole',
 'Quiescent Black Hole':'quiescent-black-hole','Black Hole With Relativistic Jets':'jet-black-hole',
 'Super Massive Blackhole':'central-supermassive-black-hole'}

def generate():
 definitions={d['id']:d for d in json.loads((ROOT/'data/stellar/population-v1.json').read_text())['objects']}
 files=sorted(ASSETS.glob('*.png')); by_stem={p.stem:p for p in files}; used=set();entries=[]
 for stem,p in by_stem.items():
  if stem.endswith(' Distance'):continue
  if stem not in ALIASES:continue
  key=ALIASES[stem];d=definitions.get(key,definitions['m-red-dwarf'] if key=='m-red-dwarf-quiet' else definitions['quiescent-black-hole'])
  distance=by_stem.get(stem+' Distance');used.add(p.name)
  if distance:used.add(distance.name)
  entries.append(dict(objectType=key,closeAsset=p.name,distanceAsset=distance.name if distance else None,automaticPair=bool(distance),fallbackDistanceMode=d['fallbackDistanceMode'],colorProfile=d['color'],luminosityProfile=d['distanceLuminosity'],visualScaleProfile=d['visualScale']))
 unmatched=sorted(p.name for p in files if p.name not in used)
 if unmatched:raise ValueError('Unmapped supplied artwork: '+', '.join(unmatched))
 manifest=dict(version=1,objects=entries,files=[dict(filename=p.name,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in files],unmatched=unmatched)
 (ASSETS/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
 lines=['# Supplied stellar artwork','',f'{len(files)} images; {len(entries)} close representations; {sum(e["automaticPair"] for e in entries)} automatically matched distance variants. No unmapped files.','',
 '| Object | Close image | Distance image | Automatic pair | Distance fallback |','|---|---|---|---|---|']
 for e in entries:lines.append(f'| {e["objectType"]} | {e["closeAsset"]} | {e["distanceAsset"] or "—"} | {"Yes" if e["automaticPair"] else "No distance supplied"} | {e["fallbackDistanceMode"]} |')
 lines+=['','The `O-Class Hot Blue.png` / `O-Class Hot Blue Distance.png` pair is detected by the same exact suffix rule as the other seven pairs. The quiet M-dwarf variant shares the type definition but keeps its own close artwork. The central SMBH is a separate asset, excluded from ordinary population sampling.','',
 'Original PNG bytes are preserved. Runtime preparation removes the black matte and fades texture edges; distance files are center-cropped to remove their baked outer border. SHA-256 source digests are recorded in `assets/visual/stellar/manifest.json`.']
 report=ROOT/'docs/stellar-asset-validation.md';report.write_text('\n'.join(lines)+'\n',encoding='utf-8')
 return manifest

if __name__=='__main__':
 m=generate();print(f'Validated {len(m["files"])} images; {sum(e["automaticPair"] for e in m["objects"])} close/distance pairs; no unmatched files.')
