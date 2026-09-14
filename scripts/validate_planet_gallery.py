"""Validate actual native captures, shared material contracts, and bounded scene ownership."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from validate_godot_smoke import validate_log

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("directory",type=Path)
parser.add_argument("--log",type=Path,required=True)
args=parser.parse_args()
report=json.loads((args.directory/"capture-report.json").read_text())
failures=validate_log(args.log.read_text(encoding="utf-8",errors="replace"))
if "STELLAR_PLANET_GALLERY_COMPLETE" not in args.log.read_text():failures.append("Gallery did not complete.")
manifest=json.loads((root/"assets/visual/planets/identity-manifest.json").read_text())
if len(manifest["families"])!=20 or sum(len(f["variants"]) for f in manifest["families"])!=100:failures.append("20/100 family contract failed.")
names={item["file"] for item in report}
for size in ("1920x1080","1280x720"):
    for family in manifest["families"]:
        stem=family["variants"][0]["id"][:-3]
        for view in ("orbit","surface") if family["surfaceFamily"]!="none" else ("orbit",):
            if f"{stem}-{view}-{size}.png" not in names:failures.append(f"Missing {stem} {view} {size}")
    for name in ("unknown-world","partial-survey","inhabited-night-lights",*(f"sky-{n}-{v}" for n in range(1,8) for v in ("surface","orbit"))):
        if f"{name}-{size}.png" not in names:failures.append(f"Missing {name} {size}")
for item in report:
    file=args.directory/item["file"]
    raw=file.read_bytes()
    if raw[:8]!=b"\x89PNG\r\n\x1a\n" or struct.unpack(">II",raw[16:24])!=(item["width"],item["height"]):failures.append(f"Wrong native image dimensions: {file.name}")
    item["sha256"]=hashlib.sha256(raw).hexdigest()
if len(report)!=110:failures.append(f"Expected 110 captures, got {len(report)}")
if max(r["nodes"] for r in report)-min(r["nodes"] for r in report)>18:failures.append("Scene node count grew without a bound.")
summary={"captures":len(report),"resolutions":["1920x1080","1280x720"],
    "nodeRange":[min(r["nodes"] for r in report),max(r["nodes"] for r in report)],
    "videoMemoryMiB":[round(min(r["videoBytes"] for r in report)/1048576,2),round(max(r["videoBytes"] for r in report)/1048576,2)],
    "processMemoryMiB":[round(min(r["processBytes"] for r in report)/1048576,2),round(max(r["processBytes"] for r in report)/1048576,2)],
    "limitations":"Synthetic specimens, RTX 3080 Ti / OpenGL compatibility; this is not a full campaign FPS benchmark.",
    "failures":failures}
(args.directory/"validation-summary.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary,indent=2))
raise SystemExit(1 if failures else 0)
