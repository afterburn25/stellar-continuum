"""Publish rejected metadata from the reviewed audit; never import rejected pixels.

Run after updating the art audit. Accepted art remains the separately reviewed
runtime allowlist. Audited aliases are preserved alongside their canonical IDs.
"""
from pathlib import Path
import json

def sync(root):
    path=root/'data/planets/planet-art-v1.json'
    registry=json.loads(path.read_text(encoding='utf-8'))
    audit=json.loads((root/'docs/planet-art/full-audit.json').read_text(encoding='utf-8'))
    types=json.loads((root/'data/planets/planet-types-v1.json').read_text(encoding='utf-8'))
    valid={(s['primary'],s['id']) for s in types['subclasses']}
    aliases={('cracked','shattered'):'blown-apart'}
    rejected=[]
    for row in audit['images']:
        if row['status']!='rejected':continue
        subtype=aliases.get((row['primaryClass'],row['subclass']),row['subclass'])
        if (row['primaryClass'],subtype) not in valid:raise ValueError(f"Unresolved rejected-art classification: {row['id']}")
        rejected.append(dict(id=row['id'],filename=row['filename'],sha256=row['sha256'],primaryClass=row['primaryClass'],subclass=subtype,auditedSubclass=row['subclass'],reason=row['rejectionReason'],duplicateOf=row['duplicateOf'] or '',earthGeography=row['earthGeography'],status='rejected'))
    registry['rejectedAssets']=rejected
    path.write_text(json.dumps(registry,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(f'Published {len(rejected)} rejected image records; no rejected pixels copied.')

if __name__=='__main__':sync(Path(__file__).resolve().parents[2])
