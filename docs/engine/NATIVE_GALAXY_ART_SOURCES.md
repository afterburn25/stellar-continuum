# Native galaxy artwork sources

The C++ client packages these approved Stellar Continuum source images:

- `assets/visual/space/deep-field-v2.png`: original project deep-space artwork generated with OpenAI image generation on 2026-09-10. It appears only in the distant galaxy overview.
- `assets/visual/space/spiral-galaxy-v3.png`: the user's replacement `Spiral Galaxy.png`, supplied on 2026-09-16 from `C:/Steller Continuum/Galaxy's/Spiral Galaxy.png`. The packaged PNG is byte-identical to the supplied source. The native renderer converts its near-black matte to transparency once during image preparation, preserving luminous arms and eliminating rectangular borders. Its original 1456:816 aspect ratio is retained. The backdrop fits the generated catalog's extent and scales with it; authoritative positions, physical distances, routes, ownership and discovery never come from its pixels.

- `assets/visual/space/regional-nebula-b.png`: existing approved project regional star/nebula backdrop, reused unchanged at closer galaxy-map zoom. Source role is recorded in `docs/CINEMATIC_MAP_AND_SURFACE.md`.

Prompts and generation filenames for earlier project artwork are retained in `docs/CINEMATIC_ASSET_PROVENANCE.md`; the replacement spiral was supplied directly by the user. The user's Stellaris screenshots are visual references and are not shipped. These illustrations do not assert that generated systems exactly follow painted spiral arms. The bounded native integration does not establish the full Godot dust-shader or graphical parity.

Only these three PNGs and this source note are declared by `export/native-galaxy-art-assets.json`; packaging verifies their paths and SHA-256 fingerprints.
