# Planet classification and visual presentation

Planet identity v1 derives a player-facing description from the existing saved physical catalog. It does not change simulation enums, species requirements, colonization permissions, research, yields, or save schemas.

## Physical precedence

The first matching rule in PlanetClassifier wins. Numerical boundaries are game presentation conventions, not a claim that astronomy has a universal 20-class taxonomy.

| Order | Class | Evidence used |
|---|---|---|
| 1 | Dwarf Planet | Authoritative dwarf body kind |
| 2 | Ice Moon | Moon, below 210 K, with solvent or atmospheric volatile evidence |
| 3 | Large Moon | Remaining moon with radius at least 0.35 Earth |
| 4 | Barren Moon | Remaining smaller satellite |
| 5 | Ice Giant / Gas Giant | Non-solid envelope; ice-family proxy below 170 K, 50 Earth masses and 6 Earth radii; reserved Sol giants retain their established identities |
| 6 | Greenhouse | Solid, at least 450 K and 1,000 kPa, with atmosphere |
| 7 | Cryogenic Hydrocarbon | Hydrocarbon solvent at or below 180 K with reducing/inert chemistry |
| 8 | Volcanic | Remaining solid world at least 1,000 K; thermal interpretation, not a measured eruption rate |
| 9 | Airless Rocky | Vacuum or pressure below 0.1 kPa |
| 10 | Ice | Remaining cold solid world below 200 K with volatile evidence |
| 11 | High-Pressure Ocean / Ocean | Water-immersed environment; 250 kPa divides these families |
| 12 | High-Gravity Super-Earth | At least 1.6 g, or mass at least 3 Earth and radius at least 1.2 Earth |
| 13 | Exotic | Ammonia/other solvent, unusual immersion, or extreme radiation with unknown chemistry |
| 14 | Reducing-Atmosphere | Remaining reducing atmosphere |
| 15 | Toxic | Substantial CO2/inert/other atmosphere; an Earthlike open-air label, never universal alien unsuitability |
| 16 | Terran | Water, oxygen-bearing air, 260–320 K, 40–200 kPa, 0.3–1.6 g, low radiation |
| 17 | Desert | Remaining warm solid world without available solvent |
| 18 | Arid | Remaining non-immersed solid terrain; moisture coverage is unresolved |

Every result includes its reason. Moon kind takes precedence over environmental descriptors; secondary modifiers preserve atmosphere, ice and thermal context. A species can thrive on a world labeled Toxic or Reducing-Atmosphere. Species mechanics continue to evaluate physical facts independently.

Mercury is Airless Rocky and Venus is Greenhouse. Both remain terrestrial/rocky in the astronomical sense. Mercury's extremely thin exosphere and Venus's dense greenhouse atmosphere support this distinction: [NASA Mercury facts](https://science.nasa.gov/mercury/facts/), [NASA Venus facts](https://science.nasa.gov/venus/venus-facts/), [NASA terrestrial planets](https://science.nasa.gov/exoplanets/terrestrial/).

## One identity, several views

PlanetPresentationResolver consumes observer-filtered facts for orbit and exact owned-colony facts for ground. FNV-1a with explicit byte order mixes campaign seed, system/body identity and physical parameters. It does not use process-randomized string hashes. A reload reconstructs the same identity; changed physical conditions reclassify and refresh it.

The immutable catalog contains **20 families and five named variants per family**. The exact 100 entries, colors, cloud coverage, terrain pattern and families are exported to assets/visual/planets/identity-manifest.json. Full physical identity supplies additional continuous variation; the shader seed is not limited to the former 256-body cycle.

The actual SystemScene3D, orbital descent, PlanetSurfaceView and 2D inspector portrait consume the shared identity. The 2D shader is mechanically derived from the sphere shader; scripts/sync_planet_disc_shader.py --check prevents drift. Orbit and ground share mineral palettes, geological pattern, liquid character and atmospheric chemistry. Oceans use ocean-dominated material and flat distant water, giants prohibit ground access, airless worlds have no cloud layer or scattering. Existing colony construction heights and picking stay unchanged through the entire buildable area; class-specific scenic terrain begins outside that area.

Crater bowls, dune ridges, polygonal ice fractures, highlands, basalt, salt basins, storm bands and thermal fissures provide morphological variation. Geological patterns and land coverage are illustrative: the simulation currently has no measured geology, global elevation map or continent inventory. No illustrative fissure grants volcanic resources.

Development lights use actual known owned population, with bounded brightness bands to avoid rebuilding a scene for every fractional population change. A world with zero known population has no generated city lights. The canonical photographed Earth retains its established photograph treatment; an unregistered night texture is not painted across that photograph.

## Survey protection and Sol

The partial-survey branch returns no PlanetPresentation before reading hidden environmental fields. Unknown and partial portraits remain neutral; no approved map, true class, atmosphere or city-light intensity is loaded. The physical inspector reports unconfirmed fields accordingly.

Sol's authored image files are unchanged. Canonical selection requires the reserved system/preset, body ID and name, including the reserved Moon ID. Renaming a procedural planet Earth does not activate the Earth photograph. Existing Sol source mapping and attribution remain in SOL_VISUAL_SOURCES.md. Some inherited Sol assets are lower resolution; this change does not claim to have upgraded their source resolution.

## Inspection and verification

Developer tools provide **Planet visual gallery**. Choose class, variant, stellar environment and orbit/ground view. Ordinary selected planets show a portrait, classification and physical statistics; Developer campaigns additionally show the seed, visual families, modifiers, reason and sky.

The standalone tools/PlanetVisualGallery.tscn uses the production renderers with explicitly synthetic physical specimens. Set STELLAR_PLANET_CAPTURE_DIR to capture all classes and the celestial cases at native 1920×1080 and 1280×720. The capture report records dimensions, process memory, Godot object/node counts and video memory. No player save is opened by the gallery.

Regression groups in PlanetIdentityChecks cover all 20 physical examples, 100 variant identifiers, precedence boundaries, canonical guards, serialization round trips, physical changes, population gates and hidden surveys. The large-catalog check resolves 20,000 bodies across 2,500 systems without creating textures.

## Asset production status

Runtime procedural materials and the native 2048×2048 packed mineral master are supplied. The orbital renderer can lazily load reviewed variant albedo maps from the documented approved paths. No such external generated maps are needed for the current renderer.

ComfyUI was unavailable in this development environment. The 200 exact candidate jobs and two-stage API workflow are prepared, but **AI-generated orbital/surface master artwork remains pending**. Surface landscape candidates are reference/matte-production inputs, not terrain textures automatically pasted over interactive ground. See PLANET_ART_COMFYUI_WORKFLOW.md for generation, licensing, review and integration.
