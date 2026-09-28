# Jupiter and Mars authored surface test — 18 September 2026

The user supplied four isolated planet renders for each world and requested
conversion to rotatable globes. Selected references:

- Jupiter: `afterburn25._Jupiter_single_isolated_photorealistic_planet_en_0fc908a4-1590-439c-8f00-b04c9fae9c1a_1.png`.
- Mars: `afterburn25._Mars_single_isolated_photorealistic_planet_entir_bd23724e-1972-42da-921d-9493d976dd25_3.png`.

`jupiter-map.png` and `mars-map.png` are 1774 × 887 full equirectangular
colour maps created with OpenAI image generation using those references.
The broad directional lighting was reduced and the unseen hemisphere was
inferred to match the visible surface. Mars received a second lighting correction.
They are the exact, unchanged PNGs from the 20260918 standalone planet test pack.
Original reference authorship/rights remain with their respective owner; the
user supplied these files for this game. They are not NASA survey datasets.

The native solar-system discs, inspection portraits and 3D planetary screen use
these same maps. The engine supplies globe geometry and dynamic lighting.
There is no measured height or normal map. Fine relief shading in the artwork
is still baked into the colour, so it cannot respond correctly to every light
direction. Far-side geography is artistic inference, not recovered observation.
Minor polar distortion and imperfect seam continuity remain possible.

## Remaining Sol artwork — 18 September 2026

Seven further 1774 x 887 PNG equirectangular maps were created with the built-in
OpenAI image generation tool from user-supplied images. No source images were
overwritten. The final PNGs are copied unchanged into `assets/visual/sol/`.

| Map | User reference selection |
|---|---|
| mercury-map.png | Mercury -Close Up.png, with Mercury.png as support |
| venus-map.png | Visible-light Venus.png; UV Venus.png supports cloud structure only |
| earth-map.png | Earth - alternate orientation.png and Earth.png; existing NASA world.topo.bathy.200401.3x5400x2700.jpg guides longitude/coastline alignment |
| moon-map.png | Luna - Earths Moon.png |
| saturn-map.png | Saturn.png; rings and their shadows excluded from the surface |
| uranus-map.png | Uranus.png; rings and their shadows excluded from the surface |
| neptune-map.png | Neptune.png |

Earth uses a cloud-free albedo, with the existing separately credited NASA cloud
and night-light maps. Continents retain the existing geographic orientation;
the appearance conversion is artistic and is not an exact scientific raster.
The old source Earth JPEG is retained for provenance and reference compatibility.
Venus uses its visible-light appearance, not a new ultraviolet viewing mode.
Luna's unseen side is inferred cratered highland terrain, not duplicated maria.
Saturn and Uranus use separate annulus geometry and shared artistic ring-band
profiles matched to the references; they are not extracted photographic rings.

Complete generation prompts are in `SOL_ARTWORK_GENERATION.json` beside this
document in source and in Documentation/SolarArtworkPrompts.json in the test pack.
All nine Sol bodies now share full maps between orbital discs and the planetary
globe. Original user reference rights remain with their respective owners.
