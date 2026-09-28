"""Non-destructive QA contact sheets and representative copies of original captures."""
import argparse
import json
import shutil
from pathlib import Path
from PIL import Image, ImageDraw

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument("capture",type=Path);args=parser.parse_args()
manifest=json.loads((root/"assets/visual/planets/identity-manifest.json").read_text())
for view in ("orbit","surface"):
    sheet=Image.new("RGB",(1600,840),"#071119");draw=ImageDraw.Draw(sheet)
    for index,family in enumerate(manifest["families"]):
        stem=family["variants"][0]["id"][:-3];path=args.capture/f"{stem}-{view}-1920x1080.png"
        x=index%5*320;y=index//5*210
        if path.exists():
            with Image.open(path) as original:
                # Crop to the renderer only for inspection; permanent evidence below is copied unchanged.
                panel=original.crop((18,118,1594,1033));panel.thumbnail((316,182))
                sheet.paste(panel,(x,y+24))
        draw.text((x+5,y+5),family["class"]+(" · no ground" if not path.exists() else ""),fill="#8dc9dd")
    sheet.save(args.capture/f"contact-{view}.jpg",quality=94)
target=root/"docs/evidence/planet-identity";target.mkdir(parents=True,exist_ok=True)
selected=[
    "terran-orbit-1920x1080.png","ocean-orbit-1920x1080.png","desert-surface-1920x1080.png",
    "ice-moon-surface-1280x720.png","sky-3-surface-1920x1080.png","sky-4-orbit-1920x1080.png",
    "volcanic-orbit-1920x1080.png","unknown-world-1280x720.png","inhabited-night-lights-1920x1080.png"]
for name in selected:shutil.copyfile(args.capture/name,target/name)
shutil.copyfile(args.capture/"validation-summary.json",target/"validation-summary.json")
shutil.copyfile(args.capture/"capture-report.json",target/"capture-report.json")
print(f"Created two inspection sheets and copied {len(selected)} untouched representative captures.")
