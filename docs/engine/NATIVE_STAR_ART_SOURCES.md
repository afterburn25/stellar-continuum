# Native star artwork sources

The C++ client ships this approved star-disc set. All files are
1024x1024 RGBA PNGs on transparent backgrounds; each photosphere disc is
centered with a ~230 px radius (matching `stellar_texture_size` geometry
in `native_celestial_appearance.cpp`) and a soft corona rim fades to
fully transparent ~60 px past the limb. Eleven sprites cover every
`StellarClass` except `BlackHole`, which stays procedural (lensed
horizon + accretion disc).

## Source imagery: NASA SDO/AIA (public domain)

Eight of the eleven sprites are derived from real Solar Dynamics
Observatory Atmospheric Imaging Assembly full-disc images
(sdo.gsfc.nasa.gov — NASA data is public domain). The deterministic
bake script is `D:\gen-venv\bake_stars.py`; per class it:

1. Erases the SDO timestamp strip, then rotates/mirrors the disc by a
   per-class deterministic angle so each sprite has unique surface
   structure.
2. Fits the photosphere limb as the steepest descent of the radial
   luminance profile and rescales the disc onto the 1024 px canvas.
3. Bakes deterministic umbra/penumbra starspot groups (foreshortened
   near the limb) whose count/size scale with class activity.
4. Compresses local luminance detail for hot stars (they are nearly
   featureless), applies a mild per-channel hue gain toward the engine
   spectral palette (`star_color` in `native_system_workspace.cpp`)
   inside the disc, and tints the corona rim by the class color.
5. Alpha: opaque disc with a soft limb rolloff, corona alpha capped at
   0.30 and fading to zero within ~60 px of the limb; a global radial
   window guarantees fully transparent canvas edges.

| Asset | AIA channel | Notes |
|---|---|---|
| star-m-dwarf | 0304 | 12 large spot groups, deep red |
| star-k-dwarf | 0304 | 8 spot groups, orange-red |
| star-g-dwarf | 0171 | 5 spot groups, golden |
| star-f-dwarf | 0171 | 3 spot groups, pale gold |
| star-a-white | 0193 | 2 small groups, blue-white |
| star-hot-blue | 0193 | 1 small group, flattened detail |
| star-giant | 0304 | 9 large groups, deep orange-red |
| star-white-dwarf | 0193 | no spots, smooth silver-white |

## FLUX.1-schnell renders (compact/exotic objects)

No real photosphere imagery exists for the remaining classes, so
`star-neutron`, `star-protostar`, and `star-pulsar` keep the earlier
FLUX.1-schnell renders (`black-forest-labs/FLUX.1-schnell`, Apache-2.0;
the non-commercial dev variant was avoided). Pipeline: Diffusers
`FluxPipeline` with the `city96/FLUX.1-schnell-gguf` Q5_K_S transformer,
`mcmonkey/google_t5-v1_1-xxl_encoderonly` T5 encoder,
`openai/clip-vit-large-patch14`, `nerualdreming/flux_vae` VAE; 4 steps,
guidance 0. Generator: `D:\gen-venv\gen_stars.py`; normalization:
`D:\gen-venv\finalize_stars.py`.

| Asset | Variant | Seed |
|---|---|---|
| star-neutron | v0 | `0x611A + 8*617` |
| star-protostar | v1 | `0x622B + 9*617` |
| star-pulsar | v2 | `0x633C + 10*617` |

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
