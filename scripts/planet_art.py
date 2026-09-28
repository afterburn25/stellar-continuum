#!/usr/bin/env python3
"""Create original packed detail and reproducible ComfyUI candidate jobs.
No model downloads, remote services, generation claims, or approved-file overwrites.
Run after: dotnet run --project tests/PlanetArt.Export -- .
"""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
NEGATIVE = ("cartoon, anime, toy, game board, oversaturated, glowing neon terrain, "
            "text, logo, watermark, frame, ships, people, buildings, artificial cities, "
            "blurry, low resolution, duplicated planet, baked interface")


def make_detail():
    import numpy as np
    from PIL import Image
    target = ROOT / "assets/visual/planets/modifiers/mineral-detail-packed.png"
    if target.exists():
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    # Native 2048 periodic Fourier synthesis, not a small image enlarged to HD.
    n = 2048
    rng = np.random.default_rng(928317)
    kx = np.fft.fftfreq(n)[None, :]
    ky = np.fft.fftfreq(n)[:, None]
    radius = np.sqrt(kx * kx + ky * ky)
    channels = []
    for exponent in (1.1, .5, .05):
        signal = np.fft.fft2(rng.standard_normal((n, n)))
        amplitude = np.maximum(radius, 1 / n) ** -exponent
        amplitude[0, 0] = 0
        field = np.fft.ifft2(signal * amplitude).real
        field = np.clip(.5 + field / (field.std() * 7), 0, 1)
        channels.append((field * 255).astype(np.uint8))
    Image.fromarray(np.stack(channels, axis=-1)).save(target)
    return target


def workflow(job, checkpoint):
    w, h = job["dimensions"]
    return {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": checkpoint}},
        "2": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["1", 1], "text": job["positive"]}},
        "3": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["1", 1], "text": job["negative"]}},
        "4": {"class_type": "EmptyLatentImage", "inputs": {"width": w // 2, "height": h // 2, "batch_size": 1}},
        "5": {"class_type": "KSampler", "inputs": {"model": ["1", 0], "positive": ["2", 0], "negative": ["3", 0],
                "latent_image": ["4", 0], "seed": job["seed"], "steps": 32, "cfg": 5.5,
                "sampler_name": "dpmpp_2m_sde", "scheduler": "karras", "denoise": 1.0}},
        "6": {"class_type": "LatentUpscale", "inputs": {"samples": ["5", 0], "upscale_method": "bislerp",
                "width": w, "height": h, "crop": "disabled"}},
        "7": {"class_type": "KSampler", "inputs": {"model": ["1", 0], "positive": ["2", 0], "negative": ["3", 0],
                "latent_image": ["6", 0], "seed": job["seed"], "steps": 18, "cfg": 5.0,
                "sampler_name": "dpmpp_2m_sde", "scheduler": "karras", "denoise": .28}},
        "8": {"class_type": "VAEDecode", "inputs": {"samples": ["7", 0], "vae": ["1", 2]}},
        "9": {"class_type": "SaveImage", "inputs": {"images": ["8", 0],
                "filename_prefix": "stellar-candidates/" + job["id"]}}
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", default="REPLACE_WITH_LICENSED_SDXL_CHECKPOINT.safetensors")
    parser.add_argument("--queue-local", action="store_true", help="Explicitly submit candidate jobs to local ComfyUI.")
    parser.add_argument("--limit", type=int, default=200)
    args = parser.parse_args()
    source = json.loads((ROOT / "assets/visual/planets/identity-manifest.json").read_text())
    target = ROOT / "assets/visual/planets/production"
    target.mkdir(parents=True, exist_ok=True)
    jobs = []
    for family in source["families"]:
        for variant in family["variants"]:
            for view in ("orbital", "surface"):
                # Giant 'surface' jobs are cloud-top vistas, never fictional solid ground.
                gaseous = family["surfaceFamily"] == "none"
                framing = ("seamless 2:1 equirectangular planetary albedo map, no sphere, "
                           "no sky, no perspective, no terminator, uniform diffuse illumination, "
                           "longitude wraps seamlessly at left and right edges") if view == "orbital" else (
                           "wide cinematic cloud-top atmospheric vista, no ground or terrain, no buildings"
                           if gaseous else "wide eye-level planetary landscape, coherent horizon, realistic geology, restrained natural light")
                identifier = variant["id"] + "-" + view
                job = {
                    "id": identifier, "class": family["class"], "variant": variant["id"], "view": view,
                    "seed": int.from_bytes(hashlib.sha256(identifier.encode()).digest()[:8], "little") % (2**53),
                    "dimensions": [2048, 1024] if view == "orbital" else [2560, 1440],
                    "positive": (f"Photorealistic scientific planetary visualization, {family['class']}, "
                                 f"{variant['name']}, {framing}, mineral and terrain detail, "
                                 f"natural albedo palette #{variant['land']} and #{variant['rock']}, "
                                 "no fictional biological habitability assumptions"),
                    "negative": NEGATIVE + (", land continents, mountains" if family["class"] in ("Ocean", "HighPressureOcean") else ""),
                    "approvedTarget": f"assets/visual/planets/{view}/{variant['id']}-" + ("albedo.png" if view == "orbital" else "master.png"),
                    "status": "pending-model-selection-generation-and-review"
                }
                jobs.append(job)
    (target / "jobs.json").write_text(json.dumps(jobs, indent=2) + "\n", encoding="utf-8")
    sample = workflow(jobs[0], args.checkpoint)
    (target / "comfy-api-template.json").write_text(json.dumps(sample, indent=2) + "\n", encoding="utf-8")
    master = make_detail()
    provenance = {
        "asset": master.relative_to(ROOT).as_posix(), "dimensions": [2048, 2048], "channels": ["mineral height", "roughness", "fine grain"],
        "generator": "scripts/planet_art.py / periodic Fourier synthesis / numpy PCG64 seed 928317",
        "source": "Original mathematical synthesis; no external image, model, LoRA or encoder used.",
        "sha256": hashlib.sha256(master.read_bytes()).hexdigest(), "license": "Original project asset; no third-party asset license.",
        "comfyModelUsed": None, "comfyGenerationCompleted": False
    }
    (target / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    for folder in ("orbital", "surface"):
        (ROOT / "assets/visual/planets" / folder).mkdir(exist_ok=True)
        (ROOT / "assets/visual/planets" / folder / "README.md").write_text(
            "Runtime procedural families are active. Approved raster masters may be placed here after seam, resolution, physical consistency and provenance review. Candidate jobs are in ../production/jobs.json. No pending master is claimed as generated.\n", encoding="utf-8")
    for folder in ("starfields", "nebulae", "dust", "sky_objects", "rings", "moons", "giants"):
        path = ROOT / "assets/visual/space" / folder
        path.mkdir(parents=True, exist_ok=True)
        (path / "README.md").write_text("Active master: shared procedural SystemSkyProfile / planet_identity_sky.gdshader, original bounded sphere/ring geometry and surveyed planet materials. No per-system raster background is allocated.\n", encoding="utf-8")
    if args.queue_local:
        if args.checkpoint.startswith("REPLACE_"):
            raise SystemExit("Select a locally installed checkpoint and record its exact hash and license before queuing.")
        for job in jobs[:args.limit]:
            if (ROOT / job["approvedTarget"]).exists():
                continue
            data = json.dumps({"prompt": workflow(job, args.checkpoint)}).encode()
            request = urllib.request.Request("http://127.0.0.1:8188/prompt", data=data, headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(request, timeout=20) as response:
                result = json.load(response)
                if "prompt_id" not in result:
                    raise RuntimeError(result)
                print(job["id"], result["prompt_id"])
    print(f"Prepared {len(jobs)} candidate jobs and original 2048px packed detail. ComfyUI generation not performed unless --queue-local was supplied.")


if __name__ == "__main__":
    main()
