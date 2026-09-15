# Native galaxy artwork sources

The C++ client reuses these existing Stellar Continuum images unchanged:

- `assets/visual/space/deep-field-v2.png`: original project deep-space artwork generated with OpenAI image generation on 2026-09-10. It appears only in the distant galaxy overview.
- `assets/visual/space/milky-way-layer-v2.png`: original project transparent barred-spiral artwork generated on the same date. It is decorative; authoritative system positions, distances, routes, ownership and discovery never come from its pixels.

- `assets/visual/space/regional-nebula-b.png`: existing approved project regional star/nebula backdrop, reused unchanged at closer galaxy-map zoom. Source role is recorded in `docs/CINEMATIC_MAP_AND_SURFACE.md`.

Exact prompts and original generation filenames are retained in `docs/CINEMATIC_ASSET_PROVENANCE.md`. The user's Stellaris screenshots are visual references and are not shipped. These illustrations do not assert that generated systems exactly follow painted spiral arms. The bounded native integration does not establish the full Godot dust-shader or graphical parity.

Only these three PNGs and this source note are declared by `export/native-galaxy-art-assets.json`; packaging verifies their paths and SHA-256 fingerprints.
