# Planet art production workflow

## Current production status

No configured ComfyUI endpoint was available, and no checkpoint, LoRA or text encoder was used for this change. Do not describe the 200 prepared specifications as generated images. The active visuals are original runtime shaders plus a mathematically synthesized native 2048×2048 packed mineral texture. Its SHA-256 and generator are recorded in assets/visual/planets/production/provenance.json.

The generated JSON job list contains five orbital and five surface specifications for every class, including cloud-top vistas instead of solid terrain for giants. All names, seeds, positive/negative prompts, native target dimensions and approved paths are explicit.

## Prepare and batch

1. Run dotnet run --project tests/PlanetArt.Export -- . to export the actual runtime catalog.
2. Run python scripts/planet_art.py using Python with numpy and Pillow. This prepares jobs, one standard-node API workflow and the original packed texture. Existing packed/approved files are preserved.
3. Install a locally licensed SDXL-compatible checkpoint in ComfyUI. Record its exact filename, SHA-256, source, license/version, base model, VAE and included text encoders before production. No checkpoint or license is silently assumed.
4. Review assets/visual/planets/production/jobs.json. For a trial run, supply --checkpoint YOUR_FILENAME.safetensors --queue-local --limit 1. The endpoint is local http://127.0.0.1:8188/prompt; no paid remote service is invoked.
5. After the trial passes, rerun with --limit 200 for an overnight batch. SaveImage writes under stellar-candidates with deterministic IDs and ComfyUI's numeric suffix. Jobs whose approvedTarget already exists are skipped.

The standard API graph uses CheckpointLoaderSimple, two CLIPTextEncode nodes, EmptyLatentImage, KSampler, LatentUpscale, a second KSampler, VAEDecode and SaveImage. The checkpoint loader supplies the model's own CLIP and VAE outputs; no separate text encoder or LoRA is used by the template. These formats and local queue behavior follow the [ComfyUI server route documentation](https://docs.comfy.org/development/comfyui-server/comms_routes).

## Sampling and size

| Setting | First pass | Detail pass |
|---|---|---|
| Sampler / scheduler | dpmpp_2m_sde / karras | same |
| Steps | 32 | 18 |
| CFG | 5.5 | 5.0 |
| Denoise | 1.0 | 0.28 |
| Orbital dimensions | 1024×512 latent source | 2048×1024, full equirectangular map |
| Surface dimensions | 1280×720 latent source | 2560×1440 |
| Seed | SHA-256 of class/variant/view ID | same |

LatentUpscale uses bislerp followed by actual denoising. It is not a pixel enlargement claimed as source detail. Do not upscale compressed thumbnails. For 3840×2160 export backgrounds, generate/reconstruct at the target resolution with a validated tiled diffusion/decoder workflow; do not assume the portable standard-node template fits every GPU.

Orbital maps deliberately have 2:1 equirectangular geometry. A square planet-disc image must not be wrapped around a globe. Square 2048×2048 orbital portraits can be rendered from the actual sphere after an albedo map passes review.

## Prompt rules

Every job embeds its exact text. Orbital templates request a seamless unlit albedo map without a sphere, sky, perspective or terminator. Surface templates request an eye-level geological landscape; giant jobs request an atmospheric vista without ground. The negative template rejects cartoons, toys, neon terrain, text, watermarks, blurry low resolution, cities, ships and people. Ocean negatives additionally reject continental landmasses and mountains.

Physical review still overrides prompt wording: airless scenes have no weather sky, greenhouse worlds conceal much of their ground from orbit, cryogenic hydrocarbon worlds use cold chemistry rather than tropical oceans, and uninhabited reference worlds have no city lights.

## Approval and integration

Check licensing and retain the model manifest, original generation metadata, prompt, seed, dimensions and output hash. Inspect at native 100% scale for seams, mirrored continents, repeating storms, inconsistent illumination, unrealistic colors and accidental marks. Review an orbital/surface pair together.

Approved orbital albedo files go to assets/visual/planets/orbital/{variant-id}-albedo.png. PlanetIdentityMaterials lazy-loads that path only for a surveyed noncanonical variant. Missing files use the active procedural material. Keep these as genuine full-surface maps and generate mipmaps during Godot import.

Surface masters go to assets/visual/planets/surface/{variant-id}-master.png as reviewed source references. They require a deliberate terrain/matte extraction pass; landscape art must not replace interactive terrain or its authoritative building heights. Record permanent derivatives in ASSET_MANIFEST.md and the provenance JSON. Never overwrite an approved asset as an unattended batch side effect.

Remaining work in this environment: licensed model selection, ComfyUI trial, generation, visual review, approved master import and any terrain/matte extraction. Runtime functionality does not depend on those unfinished external art jobs.
