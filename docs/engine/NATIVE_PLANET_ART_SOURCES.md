# Native planet artwork sources

The C++ client ships this approved generated planet-disc set. All files are
1024x1024 RGBA PNGs with transparent backgrounds; each planet disc is
centered with a 300 px radius so every body renders at one uniform scale.
Thirteen sprites cover non-Sol bodies and nine cover canonical Sol bodies.

## Generation provenance

Twelve discs were rendered locally with **FLUX.1-schnell**
(`black-forest-labs/FLUX.1-schnell`, Apache-2.0 license — the dev variant
was avoided because it is non-commercial). The pipeline ran the
`city96/FLUX.1-schnell-gguf` Q5_K_S transformer, the
`mcmonkey/google_t5-v1_1-xxl_encoderonly` T5 encoder (4-bit NF4),
`openai/clip-vit-large-patch14` and the `nerualdreming/flux_vae` VAE, all
through a locally assembled Diffusers `FluxPipeline` (4 inference steps,
guidance 0). Prompts and seed formulas are recorded in the deterministic
generation scripts under `D:\gen-venv\` (`generate_flux_planets.py`,
`regen_flux.py`–`regen_flux5.py`, `gen_clouds.py`); each render used a
"photorealistic space telescope photograph of a single planet in pure
black empty space, evenly illuminated, no lens flare, no stars" wrapper.

`gaia-world` is procedural: a Mars equirect texture (source:
solarsystemscope.com Mars texture, **CC-BY attribution required**) was
terraform-recolored by `terraform_gaia.py` — dark lowlands became oceans,
mid-tones became vegetation, highlands and caps stayed pale — then
composited with a FLUX-rendered cloud layer (`gen_clouds.py` seeds
`0x5B12`, `0x5C23` used as luminance alpha) and wrapped onto a
supersampled sphere. It carries no real Earth geography.

`wormhole-anomaly` is a gravitational-lensing ring render with soft
luminance-based alpha rather than a hard disc edge.

## Sol-system sprites

Nine canonical Sol bodies ship as the same 300 px pre-lit sprite format:

- `mars`, `jupiter`, `saturn`, `neptune`, `uranus`: real equirectangular
  surface maps from solarsystemscope.com (2k `*.jpg` textures, **CC-BY 4.0 —
  attribution required**) wrapped onto a supersampled sphere with
  near-frontal illumination by `bake_sol_sprites.py`.
- `mercury`, `venus`, `earth`, `moon`: FLUX.1-schnell disc renders
  (`gen_sol.py`; seed `variant_seed + body_index*617` with variant seeds
  `0x511A`/`0x522B`/`0x533C`, picked variants mercury-v0, venus-v0,
  earth-v0, moon-v2), normalized to the uniform disc radius; the moon's
  residual night side was shadow-lifted.

Ring systems are drawn by the engine, not baked into the sprites: Saturn
carries the broad icy profile, Jupiter/Uranus/Neptune the thin faint
profile, and a seeded minority of fully surveyed non-Sol bodies get a
deterministic broad/thin/debris pick (`body_ring` in
`native_system_view.cpp`).

## Post-processing (all deterministic, recorded in `D:\gen-venv\`)

`normalize_final.py` (uniform 300 px disc fit, limb cleanup, per-asset rim
insets to cut chromatic-aberration edges, soft-alpha path for the
wormhole) → `lift_shadows.py` (night-side lift on arid, barren,
continental, desert, frozen, tomb) → `fix_cast.py` (desert speckle
median pass, frozen blue-cast desaturation, hot-pixel despeckle) →
`fix_wedge2.py` (localized clone-stamp of a contaminated desert/frozen
arc from the opposite disc side) → `fix_glare.py` (residual glare
suppression on frozen, arid, desert).

## Per-asset seeds

| Asset | Script | Seed |
|---|---|---|
| arid-world | `regen_flux2.py` v1 | `0xBEEF7` |
| barren-world | `regen_flux3.py` v0 | `0xCAFE1 + 3*617` |
| continental-world | `regen_flux2.py` v0 | `0xDEAD1 + 1*617` |
| cracked-world | `regen_flux4.py` v2 | `0xFACE3 + 1*617` |
| desert-world | `regen_flux3.py` v1 | `0xD00D2 + 5*617` |
| frozen-world | `generate_flux_planets.py` | `0xFA17E` |
| gaia-world | `terraform_gaia.py` + `gen_clouds.py` | Mars tex + `0x5B12`/`0x5C23` |
| inferno-world | `generate_flux_planets.py` | `0xFA17E + 5*311` |
| ocean-world | `generate_flux_planets.py` | `0xFA17E + 7*311` |
| tomb-world | `regen_flux2.py` v0 | `0xDEAD1 + 3*617` |
| tropical-world | `regen_flux3.py` v2 | `0xFEED4 + 4*617` |
| volcano-world | `regen_flux3.py` v2 | `0xFEED4 + 1*617` |
| wormhole-anomaly | `regen_flux4.py` v2 | `0xFACE3 + 2*617` |

## Observer rules

Sprite assignment is presentation-only and observer-safe: a body only
receives a class sprite when it is fully surveyed and its visual class is
known; reconnaissance and unknown bodies keep their procedural discs.
Authoritative body data never comes from the pixels.

Only these twenty-two PNGs and this source note are declared by
`export/native-planet-art-assets.json`; packaging verifies their paths
and SHA-256 fingerprints.
