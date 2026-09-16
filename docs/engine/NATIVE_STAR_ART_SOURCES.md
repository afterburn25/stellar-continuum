# Native star artwork sources

The C++ client ships this approved generated star-disc set. All files are
1024x1024 RGBA PNGs on transparent backgrounds; each photosphere disc is
centered with a 233 px radius (0.455 of the texture half-width, matching
`stellar_texture_size` geometry in `native_celestial_appearance.cpp`) and a
luminance-driven corona fades to fully transparent before the canvas edge.
Eleven sprites cover every `StellarClass` except `BlackHole`, which stays
procedural (lensed horizon + accretion disc).

## Generation provenance

All eleven discs were rendered locally with **FLUX.1-schnell**
(`black-forest-labs/FLUX.1-schnell`, Apache-2.0 license — the dev variant
was avoided because it is non-commercial). The pipeline ran the
`city96/FLUX.1-schnell-gguf` Q5_K_S transformer, the
`mcmonkey/google_t5-v1_1-xxl_encoderonly` T5 encoder (4-bit NF4),
`openai/clip-vit-large-patch14` and the `nerualdreming/flux_vae` VAE, all
through a locally assembled Diffusers `FluxPipeline` (4 inference steps,
guidance 0). The deterministic script is `D:\gen-venv\gen_stars.py`
(`regen_fdwarf.py` for the F-class reroll); each render used a
"photorealistic astronomy photograph" wrapper with "glowing circular disc
centered on pure black space background, soft thin corona glow, no lens
flare streaks, no planets, no text".

## Post-processing (deterministic, `D:\gen-venv\finalize_stars.py`)

1. Radial luminance profile disc fit (`fit_disc`, 0.30*peak threshold).
2. Centered rescale onto the 1024 px canvas at the engine's 233 px disc.
3. Per-class hue alignment: each channel is scaled so the inner-disc mean
   hue matches the engine spectral palette (`star_color` in
   `native_system_workspace.cpp`) while preserving the render's own
   luminance structure. `star-f-dwarf` additionally receives a luminance
   unsharp mask because the F-class render is nearly featureless.
4. Alpha: opaque disc with a 4 px rolloff, corona alpha = clamped
   luminance outside the disc, and a radial window forcing alpha to zero
   before the canvas edge so nothing hard-clips the texture.

## Per-asset seeds

| Asset | Script | Variant | Seed |
|---|---|---|---|
| star-m-dwarf | `gen_stars.py` | v1 | `0x622B + 0*617` |
| star-k-dwarf | `gen_stars.py` | v1 | `0x622B + 1*617` |
| star-g-dwarf | `gen_stars.py` | v2 | `0x633C + 2*617` |
| star-f-dwarf | `regen_fdwarf.py` | r2v1 | `0x722B` |
| star-a-white | `gen_stars.py` | v2 | `0x633C + 4*617` |
| star-hot-blue | `gen_stars.py` | v2 | `0x633C + 5*617` |
| star-giant | `gen_stars.py` | v1 | `0x622B + 6*617` |
| star-white-dwarf | `gen_stars.py` | v2 | `0x633C + 7*617` |
| star-neutron | `gen_stars.py` | v0 | `0x611A + 8*617` |
| star-protostar | `gen_stars.py` | v1 | `0x622B + 9*617` |
| star-pulsar | `gen_stars.py` | v2 | `0x633C + 10*617` |

## Observer rules

Sprite selection is presentation-only and observer-safe: the primary
star's `StellarClass` is already disclosed by the system snapshot before
the workspace draws it, and the renderer falls back to the deterministic
procedural disc when no asset root is set or the class has no approved
sprite (black holes always stay procedural). Animated flare strands are
drawn over the sprite unchanged, keeping each system's seeded prominences.

Only these eleven PNGs and this source note are declared by
`export/native-star-art-assets.json`; packaging verifies their paths and
SHA-256 fingerprints.
