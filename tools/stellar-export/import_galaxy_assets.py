"""Import approved galaxy pairs by filename; bake bounded runtime art and density masks.
Source files are unchanged. Runtime previews contain complete artwork, never a crop.
"""
from pathlib import Path
import argparse, hashlib, json, re, unicodedata, math
from PIL import Image
MORPHOLOGIES = ('spiral','barred_spiral','elliptical','lenticular','irregular','ring')
STATES = ('starburst','active','mature','aging','quiescent')
def identify(name):
    text = re.sub(r'[^a-z0-9]+',' ',unicodedata.normalize('NFKC',Path(name).stem).lower()).strip()
    remaining=text; matches=[]
    for m in sorted(MORPHOLOGIES,key=len,reverse=True):
        pattern=r'\b'+m.replace('_',' ')+r' galaxy\b'
        if re.search(pattern,remaining): matches.append(m);remaining=re.sub(pattern,'',remaining)
    states=[s for s in STATES if s in text.split()]
    variants=[v for phrase,v in (('stars included','stars_included'),('gas dust only','gas_dust_only')) if phrase in text]
    if len(matches)>1 or len(states)>1 or len(variants)>1: raise ValueError('Ambiguous galaxy image: '+name)
    morphology=matches[0] if matches else None;state=states[0] if states else None;variant=variants[0] if variants else None
    if not morphology or not variant: raise ValueError('Unmapped galaxy image: '+name)
    return morphology,state,variant

def import_assets(source,root):
    records=[]; seen=set()
    edits_path=root/'export/galaxy-asset-edits.json'
    edits=json.loads(edits_path.read_text(encoding='utf8'))['edits'] if edits_path.exists() else {}
    used_edits=set()
    target=root/'assets/visual/galaxies'; target.mkdir(parents=True,exist_ok=True)
    masks={}
    for path in sorted(source.glob('*.png')):
        morphology,state,variant=identify(path.name); key=(morphology,state,variant)
        if key in seen: raise ValueError('Duplicate mapping: '+str(key))
        seen.add(key)
        original_path=path; original_dimensions=list(Image.open(path).size)
        edit=edits.get(path.name)
        if edit:
            path=(root/edit['path']).resolve()
            if not path.is_relative_to(root.resolve()): raise ValueError('Edited galaxy source escapes project')
            if hashlib.sha256(path.read_bytes()).hexdigest()!=edit['sha256']: raise ValueError('Edited galaxy source hash mismatch: '+str(path))
            used_edits.add(original_path.name)
        original=Image.open(path).convert('RGBA'); dimensions=list(original.size)
        runtime=original.copy(); runtime.thumbnail((1280,1280),Image.Resampling.LANCZOS)
        name=f'{morphology}-{state or "generic"}-{variant}.png'
        output=target/name; runtime.save(output,optimize=True)
        record={'id':name[:-4],'sourceFilename':original_path.name,'morphology':morphology,'populationState':state,'variant':variant,'path':output.relative_to(root).as_posix(),'sourceSha256':hashlib.sha256(path.read_bytes()).hexdigest(),'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'sourceDimensions':dimensions,'runtimeDimensions':list(runtime.size)}
        if edit:
            record.update(editedSourcePath=edit['path'],originalSourceDimensions=original_dimensions,originalSourceSha256=hashlib.sha256(original_path.read_bytes()).hexdigest())
        records.append(record)
        if variant=='gas_dust_only':
            mask=original.convert('L').resize((96,96),Image.Resampling.BOX)
            masks[record['id']]={'width':96,'height':96,'values':list(mask.getdata())}
    if used_edits!=set(edits): raise ValueError('Edited sources not mapped: '+str(set(edits)-used_edits))
    complete={(m,s) for m,s,v in seen if (m,s,'stars_included') in seen and (m,s,'gas_dust_only') in seen}
    missing=[m for m in MORPHOLOGIES if not any(pair[0]==m for pair in complete)]
    if missing: raise ValueError('Missing same-morphology pairs: '+str(missing))
    # Fit the unchanged 96-star measured neighborhood into real luminous structure.
    # This selects an artwork transform, never moves the measured stars.
    frames={}
    for record in records:
        if record['variant']!='gas_dust_only': continue
        mask=masks[record['id']]; values=mask['values']; aspect=record['runtimeDimensions'][0]/record['runtimeDimensions'][1]
        found=None
        for scale in (2.6,2.8,3.0,3.2,3.5,4.0):
            width=128*math.sqrt(.5)*scale; height=width/aspect
            dx,dy=width/95,height/95
            # Include every bilinear cell touched by the complete neighborhood.
            # Sparse probe points can miss a narrow dark lane between anchors.
            reach=24+math.hypot(dx,dy)
            offsets=[(x,y) for y in range(-math.ceil(reach/dy),math.ceil(reach/dy)+1)
                     for x in range(-math.ceil(reach/dx),math.ceil(reach/dx)+1)
                     if math.hypot(x*dx,y*dy)<=reach]
            candidates=[]
            for iy in range(15,82):
                for ix in range(15,82):
                    u,v=ix/95,iy/95
                    if math.hypot((u-.5)*width,(v-.5)*height)<24+128*math.sqrt(.5)*.14: continue
                    if any(ix+x<0 or ix+x>95 or iy+y<0 or iy+y>95 or values[(iy+y)*96+ix+x]<19 for x,y in offsets): continue
                    rank=(u-(.5+.48/scale))**2+(v-(.5+.2*aspect/scale))**2
                    candidates.append((rank,u,v))
            if candidates:
                _,u,v=min(candidates);found={'homeNeighborhoodUv':[u,v],'worldDiameterScale':scale};break
        if found is None: raise ValueError('Cannot fit measured neighborhood in '+record['id'])
        frames[record['id']]=found
    manifest={'version':'galaxy-art-v1','assets':records,'densityMasks':masks,'footprintFrames':frames}
    (root/'data/stellar/galaxy-visuals-v1.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    export_path=root/'export/native-galaxy-art-assets.json'; export=json.loads(export_path.read_text(encoding='utf8'))
    for record in records: export['assets'][record['id']]={'source':record['path'],'runtimePath':record['path'],'sha256':record['sha256']}
    export_path.write_text(json.dumps(export,indent=2)+'\n',encoding='utf8')
    rows=['# Galaxy artwork mapping','',f'{len(records)} mapped images; 6 complete morphology-only pairs.','No explicit population-state images were supplied: 0/30 exact state pairs, 30/30 covered by validated same-morphology generic pairs.','No duplicate or ambiguous mappings.','', '| Source filename | Morphology | Population | Variant | Mapped |','| --- | --- | --- | --- | --- |']
    for r in records: rows.append(f'| {r["sourceFilename"]} | {r["morphology"]} | Generic | {r["variant"]} | Yes |')
    rows+=['','Preview selection uses Stars Included only. Map structure uses Gas-Dust Only only. Both sides of a state fallback resolve to the same generic pair.','Density masks: cached 96 × 96 luminance grids, embedded in Core configuration; source files unchanged. Runtime art is aspect-preserving, bounded to 1280 pixels on its longest side.','Exact population variants can be added without changing stable morphology IDs.']
    if edits: rows+=['','Irregular and Spiral pairs were reframed to 16:9 using the built-in image-editing tool, as requested. Their runtime images are 1280 × 720. The original files remain unchanged. Full-resolution edited sources and hashes are recorded in `export/galaxy-asset-edits.json`; prompts and provenance are in [the widescreen artwork report](GALAXY_WIDESCREEN_ART.md).']
    (root/'docs/GALAXY_ASSET_REPORT.md').write_text('\n'.join(rows)+'\n',encoding='utf8')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('--root',type=Path,default=Path.cwd());a=p.parse_args();import_assets(a.source,a.root)

