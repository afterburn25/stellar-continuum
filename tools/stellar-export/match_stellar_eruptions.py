"""Authorized cross-job visual matching; analyzes sources without rewriting them.

One-to-one minimum-cost assignment preserves every distinct supplied stage.
Shape, filament occupancy and color histograms guide matches, never filenames
from unrelated MidJourney jobs. The output records that these are inferred.
"""
from pathlib import Path
import argparse, json, math
import numpy as np
from PIL import Image, ImageDraw

STAGES=('buildup','rise','peak','decay')
def assign(cost):
    n=len(cost);u=[0.]*(n+1);v=[0.]*(n+1);p=[0]*(n+1);way=[0]*(n+1)
    for i in range(1,n+1):
        p[0]=i;j0=0;minimum=[math.inf]*(n+1);used=[False]*(n+1)
        while True:
            used[j0]=True;i0=p[j0];delta=math.inf;j1=0
            for j in range(1,n+1):
                if not used[j]:
                    current=cost[i0-1][j-1]-u[i0]-v[j]
                    if current<minimum[j]:minimum[j]=current;way[j]=j0
                    if minimum[j]<delta:delta=minimum[j];j1=j
            for j in range(n+1):
                if used[j]:u[p[j]]+=delta;v[j]-=delta
                else:minimum[j]-=delta
            j0=j1
            if p[j0]==0:break
        while True:
            j1=way[j0];p[j0]=p[j1];j0=j1
            if j0==0:break
    result=[0]*n
    for j in range(1,n+1):result[p[j]-1]=j-1
    return result

def feature(path):
    with Image.open(path) as image:
        rgb=np.asarray(image.convert('RGB').resize((32,32)),dtype=np.float64)/255.
    mass=np.max(rgb,axis=2)**.5
    shape=mass/(np.linalg.norm(mass)+1e-9)
    horizontal=mass.sum(axis=0);horizontal/=np.linalg.norm(horizontal)+1e-9
    vertical=mass.sum(axis=1);vertical/=np.linalg.norm(vertical)+1e-9
    color=rgb.sum(axis=(0,1));color/=np.linalg.norm(color)+1e-9
    return np.concatenate((shape.ravel(),horizontal*.5,vertical*.5,color*.35))

def match(audit, output, contact_directory=None):
    source=Path(audit['sourceRoot']);rows=audit['files'];seen={};features={};sets=[]
    for r in rows:
        digest=r.get('pixelSha256',r['sha256'])
        if digest in seen:
            r['duplicateOf']=seen[digest];r['used']=False;r['rejectionReason']='Identical decoded pixels; canonical copy retained.'
        else:
            seen[digest]=r['path'];r['duplicateOf']=None;r['used']=True
            features[r['path']]=feature(source/r['path'])
    for spectral in 'OBAFGKM':
        groups=[[r for r in rows if r['used'] and r['starClass']==spectral and r['stage']==s] for s in STAGES]
        assert all(len(g)==12 for g in groups),(spectral,list(map(len,groups)))
        sequences=[[r] for r in groups[0]];costs=[[0.] for _ in sequences]
        for group in groups[1:]:
            matrix=[[float(np.sum((features[seq[-1]['path']]-features[r['path']])**2)) for r in group] for seq in sequences]
            for i,j in enumerate(assign(matrix)):
                sequences[i].append(group[j]);costs[i].append(matrix[i][j])
        for i,seq in enumerate(sequences):
            paths=[]
            for stage,r in enumerate(seq):
                target=f'{spectral}/flare-{i+1:02d}-{STAGES[stage]}.png';paths.append(target)
                r.update(runtimePath=target,sequenceVariant=i+1,sequenceComplete=True,pairingMethod='authorized visual similarity; not authored matching IDs',pairingCost=costs[i][stage])
            for kind in ('FLARE','MAJOR_FLARE'):
                sets.append(dict(starClass=spectral,eruptionType=kind,variantId=i+1,textures=paths,sourcePaths=[r['path'] for r in seq],stages=list(STAGES),sequenceComplete=True))
        if contact_directory:
            # Review-only contact sheets; imported source pixels stay untouched.
            sheet=Image.new('RGB',(12*160,4*180),'#151a20');draw=ImageDraw.Draw(sheet)
            for col,seq in enumerate(sequences):
                for row,r in enumerate(seq):
                    with Image.open(source/r['path']) as im:
                        thumb=im.convert('RGB');thumb.thumbnail((156,156));sheet.paste(thumb,(col*160,row*180+22))
                    draw.text((col*160+3,row*180+3),f'{spectral} {col+1:02d} {STAGES[row]}',fill='white')
            sheet.save(contact_directory/f'{spectral}-pairings.jpg')
    for kind in ('SMALL_PROMINENCE','SUPERFLARE','CME'):
        group=[r for r in rows if r['used'] and r['eruptionType']==kind]
        assert len(group)==12
        for i,r in enumerate(group):
            target=f'generic/{kind.lower()}-{i+1:02d}.png'
            r.update(runtimePath=target,sequenceVariant=i+1,sequenceComplete=False,pairingMethod='single supplied image; continuous geometry/timeline animation, no missing stages invented')
            sets.append(dict(starClass='generic',eruptionType=kind,variantId=i+1,textures=[target]*4,sourcePaths=[r['path']]*4,stages=list(STAGES),sequenceComplete=False))
        if contact_directory:
            sheet=Image.new('RGB',(6*256,2*278),'#151a20');draw=ImageDraw.Draw(sheet)
            for i,r in enumerate(group):
                x=(i%6)*256;y=(i//6)*278
                with Image.open(source/r['path']) as im:
                    thumb=im.convert('RGB');thumb.thumbnail((252,252));sheet.paste(thumb,(x,y+22))
                draw.text((x+3,y+3),f'{kind} {i+1:02d}',fill='white')
            sheet.save(contact_directory/f'{kind}-review.jpg')
    result=dict(schemaVersion=1,sourceRoot=str(source),pairingAuthorization='User approved visual-similarity matching; A-class rising replacements audited 2026-09-19.',visualSets=sets,files=rows,duplicatePixelGroups=audit['duplicatePixelGroups'],limitations=['Generic prominence, superflare and CME families provide one image per variant, not four authored stages.','Cross-stage identity is visually inferred because job UUIDs differ.'])
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('audit',type=Path);p.add_argument('output',type=Path);p.add_argument('--contacts',type=Path);args=p.parse_args()
    if args.contacts:args.contacts.mkdir(parents=True,exist_ok=True)
    result=match(json.loads(args.audit.read_text(encoding='utf-8')),args.output,args.contacts)
    print(json.dumps(dict(visualSets=len(result['visualSets']),accepted=sum(r['used'] for r in result['files']),duplicates=sum(not r['used'] for r in result['files']))))
