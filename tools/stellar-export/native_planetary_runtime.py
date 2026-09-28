"""Exercise the player-facing planetary replacement, including slot save/reload."""
from __future__ import annotations
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from native_bmp import validate_bmp
from native_colony_runtime import _diagnostic, _owned_colony, _normalized

def validate_native_planetary_export(folder: Path, env: dict[str,str]):
    captures, diagnostics, payloads = [], [], []
    with tempfile.TemporaryDirectory(prefix="stellar-native-planetary-") as temporary:
        work=Path(temporary);save=work/"planetary.player17.json"
        for width,height,load in ((1280,720,False),(1920,1080,True),(2560,1440,True)):
            label="reload" if load else "fresh"
            capture=work/f"planetary-{width}x{height}.bmp"
            args=[str(folder/"stellar-continuum-native.exe"),"--asset-root",str(folder),
                  "--save-path",str(save),"--width",str(width),"--height",str(height),
                  "--planetary-reload-smoke" if load else "--planetary-smoke",str(capture)]
            if load:args.append("--load")
            result=subprocess.run(args,cwd=work,env=env,capture_output=True,text=True,timeout=120)
            if result.returncode:raise RuntimeError(f"Planetary {label} failed: {result.stdout}\n{result.stderr}")
            match=re.search(r"^planetary=(\{.*\})$",result.stdout,re.MULTILINE)
            if not match:raise RuntimeError("Planetary input evidence is missing")
            proof=json.loads(match.group(1));expected={"mode":label}
            if load:expected["slots_restored"]=True
            else:expected.update(review_readonly=True,cancel_readonly=True,modal_isolated=True,slot_reserved=True,timed=True)
            if proof!=expected:raise RuntimeError("Planetary input proof is incomplete")
            if "save=ok " not in result.stdout or "gpu_driver=vulkan " not in result.stdout:
                raise RuntimeError("Planetary runtime did not save/render through Vulkan")
            state=_diagnostic(result.stdout,"paused_reload" if load else "fresh")
            payload=json.loads(save.read_text(encoding="utf-8-sig"));colony=_owned_colony(payload,state)
            sites=colony["SurfaceBuildings"]
            slots=[b.get("SlotIndex") for b in sites]
            if not slots or any(type(s) is not int or s<0 for s in slots) or len(set(slots))!=len(slots):
                raise RuntimeError("Saved planetary slots are missing or duplicated")
            if not any(not b["IsComplete"] and b["IndustryProgress"]==0 for b in sites):
                raise RuntimeError("Paused construction was instant or advanced")
            for source in [capture,*sorted(work.glob(f"{capture.stem}-planetary-*.bmp"))]:
                validate_bmp(source,width,height,"planetary",stdout=result.stdout)
                target=folder.parent/f"{folder.name}-{source.name}";shutil.copy2(source,target);captures.append(str(target))
            diagnostics.append(result.stdout.strip());payloads.append(payload)
        if any(_normalized(payloads[0])!=_normalized(p) for p in payloads[1:]):
            raise RuntimeError("Paused planetary save/reload changed campaign state")
    return {"nativePlanetaryInput":True,"nativePlanetaryPausedReload":True,
            "planetaryCaptures":captures,"planetaryDiagnostics":diagnostics}
