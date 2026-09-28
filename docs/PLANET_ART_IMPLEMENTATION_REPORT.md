<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Full planet art, shared 3D appearance and constrained generation

All **47 supplied folders** are included. Repeated folder attachments did not create another import. Originals in Downloads remain untouched. The latest planet follow-up package is `StellarContinuum-Static-Planets-Smooth-Stars-Test-20260919`; use **Developer Game.cmd → Developer controls → PLANET INDEX**. Select a subtype and use **GO TO EXAMPLE**, or generate a new example. Enable Full Content Coverage when starting a developer campaign to include every supported subtype.

**Current presentation:** the [static planet correction](PLANET_STATIC_ART_REPORT.md)
keeps the imported 3D surfaces, stops all automatic planet rotation and removes
added cloud layers/shadows from every planet and portrait. Earlier descriptions
below of animated clouds or spin describe the superseded implementation. Manual
globe inspection remains. Galaxy-map stars now transition gradually during zoom.

The latest [texture filtering extension](PLANET_TEXTURE_FILTERING_REPORT.md) adds
cached mip chains and trilinear sampling across native 3D materials, reducing
fine surface/ring shimmer during zoom and motion. The earlier oriented portraits,
mutual ring shadows and full taxonomy remain included.

## 1–4. Image inspection and decisions

**1,087 inspected; 665 accepted; 422 rejected; 168 classification corrections.** The source collection contains 1,070 distinct file hashes and 17 exact duplicates. The rejection total includes 70 images flagged for recognizable Earth geography. Corrections and rejections can overlap; they are not additive totals.

Every image has a row in the audit. Review covered source contact sheets, uncertain-image close-ups, and converted surfaces at four longitudes. Automated disc fitting, illumination checks and material validation supplement visual review. Ambiguous frozen, carbon, super-Earth and cloud labels were corrected from visible terrain. Strongly shaded, cropped, malformed, ring-contaminated, geographically Earth-like and otherwise unusable sources were rejected. Individual orbital-scale tree silhouettes were excluded rather than preserved as giant vegetation.

## 5–6. Planet types

Existing rocky/ice/ocean/giant body and environment records remain authoritative. Their new shared visual taxonomy contains **16 broad classes and 65 subclasses**, without duplicating the physical generator per filename. Added explicit families include carbon, greenhouse/toxic, mini-Neptune, hot Jupiter, stripped cores and cracked worlds; supplied named worlds become subtypes. Mislabelled images also provide dormant-volcanic/carbon, cryogenic-hydrocarbon and dry/cold super-Earth variants. The localized icy-hotspot subtype supports the requested cold-world heating exception. See the complete [taxonomy](planet-art/generation-rules.md), including every name, range and artwork count.

## 7. Generation percentages

Barren 20%, Desert 10%, Frozen 12%, Ocean 5%, Temperate 4%, Tundra 5%, Volcanic 7%, Greenhouse 5%, Carbon 2%, Super-Earth 8%, Mini-Neptune 7%, Gas Giant 7%, Ice Giant 5%, Hot Jupiter 1%, Chthonian 0.5%, Cracked 1.5%: **100%**. No rebalance was necessary. Within temperate: Terran 55%, Jungle 15%, Savannah 15%, Gaia 5%, other 10%. Configuration is in `data/planets/planet-types-v1.json` and is embedded at build time.

## 8–10. Orbit, atmosphere and water

Generation evaluates mass, radius, solid/envelope state, stellar flux, luminosity-scaled HZ and snow line, pressure, greenhouse warming, retention, age and stored heat/migration history before selecting art. Eligible class weights receive environment modifiers and are normalized. Gas envelopes interior to the snow line need migration history. Magma requires enough stellar or internal heat; local geology does not imply a globally molten planet. Frozen eccentric moons can have small tidal hot regions.

Visible oceans require the explicit surface-water temperature/pressure test. Extensive ice cannot be selected for a hot mean surface or an incompatible periapsis. Source flags constrain water, vegetation, ice, emission, cloud/atmosphere and rings. Secondary-class reuse is limited to approved, matching surface states. Rejected Earth-geography assets cannot enter the compiled registry. Actual Earth's legacy artwork remains limited to Sol. Physics coefficients and limitations are detailed in [generation rules](planet-art/generation-rules.md).

## 11. 3D conversion

The C++ preparation tool identifies a usable disc, excludes the bright edge, estimates smooth directional illumination and constructs base color. It extracts cloud appearance where separable, reconstructs the atmosphere in the renderer, and derives normal, roughness, water, ice, relief and emissive masks. Gas/cloud-dominated surfaces use source-colored zonal reconstruction. A single view cannot reveal its hidden hemisphere: unseen geography is deterministic source-patch synthesis, or reconstructed bands, explicitly recorded per asset.

Each accepted source produces six PNGs: 1024×512 albedo, normal, packed surface properties, clouds and emission, plus a 192-pixel thumbnail. Height is a shader-relief channel, not kilometer-scale displaced terrain. Prepared PNGs total 3,990 runtime files. Source renders and rejected candidates are excluded from the playable package.

## 12. One world in both views

Core persists `PlanetAppearance`: source/material ID, seed, class/subclass, compatible classes, climate, atmosphere, clouds, rings, tilt, axis, spin direction/period and initial phase. Both System View and Planetary Screen request the same materials and shared sphere/oblate geometry. System LOD changes resolution, not source identity. Close planets receive full materials; small bodies use reduced mesh/maps. Sidebar and Planetary Screen portraits are cached reference-pose projections of the same albedo, cloud and emission maps, including saved axis/rotation, polar flattening, transparent rings and mutual analytic shadows. These compact icons use fixed neutral preview lighting; see [the portrait extension](PLANET_PORTRAITS_REPORT.md). Real campaign time drives surface and independent cloud rotation, and pause freezes them.

Light direction follows the actual star/body chart position; luminosity/exposure affects brightness and effective stellar temperature controls a three-band thermal illuminant. Ocean/ice highlights, cloud shadows, emissive regions, atmospheric rims and separate tilted rings are dynamic. Sol's existing Earth night map remains available for its inhabited globe. Old saves receive appearance records without changing their existing physical environments; subsequent loads preserve the record exactly.

## 13. ENGINE CAPABILITIES ADDED / EXTENDED

Reusable C++ spherical-material preparation; generic normal/packed-property material response; derivative terrain relief; rough ocean/ice highlights; rotating cloud-shadow maps; temperature-derived light color; lit atmosphere-rim shells; bounded immutable GPU resources; and shared sphere/oblate/annulus meshes. Core owns classification, physics, stable selection, persistence, validation and developer generation. Application owns streaming, LOD and UI. No Godot, C#, .NET, Unity or Unreal implementation was added.

The material loader has one background CPU job, a bounded 64-request queue, 80-entry/96 MiB owner cache and 128/256/1024 material LODs. Meshes are shared; all maps use existing GPU admission/cache limits (128 texture entries, 192 MiB). Save data contains small identities/parameters, not pixels. The asset exporter checks every map hash and refuses missing, changed, duplicate or unapproved paths.

## 14–16. Audit deliverables

- [All 1,087 images, CSV](planet-art/full-audit.csv)
- [Full per-image metadata and conversion details](planet-art/full-audit.json)
- [Rejected art and exact reasons](planet-art/rejected-art.md)
- [Classification corrections](planet-art/classification-corrections.md)
- [Every class/subclass, weights, constraints and counts](planet-art/generation-rules.md)

## 17–18. Validation

All 16 selected C++ regression tests and all five export tests passed. Both packaged native replays passed at 720p and 1080p. The shipped `planet-validation.json` records the executable hash and replay summaries.

Core tests exercise 2,400 constrained physical cases, all 65 developer subtype factories, 8,168 generated bodies across four species, deterministic selection, water/ice/retention contradictions, migration and exact appearance persistence. Full Content Coverage and the index resolve all subclasses. The material test proves all 47 folders/1,087 audit rows/665 runtime assets, source flags, same material across LODs, real rotation/pause, light direction and bounded cache eviction. C++ conversion tests cover directional-light removal, rejection, seam handling and zonal reconstruction. GPU tests cover actual normal/relief, cloud shadow, specular/roughness, star color and atmospheric rim output, alongside existing depth and ice optics.

Regression checks include fresh campaigns, legacy visual migration, planet save/load, stellar populations, system workspace/zoom, planet screen and solid small bodies. Five exporter tests reject incomplete, duplicated, unreviewed and tampered materials. Packaged native replays at 1280×720 and 1920×1080 navigate Sol, inspect eight imported/procedural planet examples in both views, open the planet index, verify material identity and save/reload, and check ice/rock solids, orbit/tumbling/pause and debris controls. The deliberately injected diagnostic fault at the end verifies the existing critical-pause handler; it is not a gameplay failure. Original anchor saves are hash-checked unchanged.

## 19. Artwork worth regenerating

No new art is required to run this build. The exact rejection list identifies replacements if desired. Highest priorities are clean shattered-world fragments (all 12 Dying D images were rejected), cloud/ring-free gas-giant discs or proper texture maps, and fictional habitable geography without Earth coastlines. The following supported subtypes currently have no primary source-art assignment and use procedural surfaces: Stripped Core, Blown Apart, Cryogenic Hydrocarbon, Migrated Hot Giant, Greenhouse, Savannah, Terran, Ice World with Local Hotspots. Compatible secondary reuse can still supply some dry rocky variants.

Midjourney prompts for future source material:

**Fictional terrestrial surface:** seamless equirectangular planetary albedo map, complete fictional alien geography, invented continent silhouettes and archipelagos, realistic ocean and land biome colors, flat uniform illumination, no directional light, no shadows, no atmosphere, no clouds, no globe perspective, no Earth geography, no text --ar 2:1

**Ice surface:** seamless equirectangular albedo map of a frozen alien world, fractured blue glaciers and dirty water ice, broad planetary scale structures, flat uniform illumination, no lighting gradient, no specular glare, no stars, no atmosphere, no perspective globe --ar 2:1

**Ringed-giant surface:** seamless equirectangular albedo map of a pale gas giant, physically plausible zonal cloud bands, subtle irregular storms, uniform lighting, no cast shadows, no terminator, no rings, no black background, no text --ar 2:1

**Shattered body reference:** isolated irregular rocky planetary fragment with exposed layered interior, coherent solid geometry, entire object inside frame, neutral diffuse light, no glowing rim, no nebula, no dust cloud obscuring silhouette, plain black background, no text --ar 1:1

Generated maps still need seam, lighting and geography review; a prompt does not guarantee a usable projection.

## ENGINE LIMITATIONS REMAINING

Single-image unseen terrain is synthesized, not recovered. Material masks and terrain heights are artistic estimates, not measured geology. Atmospheric rims/haze and cloud shells are bounded raster approximations, without volumetric multiple scattering or weather simulation. Opaque atmospheric worlds cannot reveal a hidden solid surface. Ring profiles are reconstructed; particle-ring dynamics remain unimplemented. Mutual planet/ring shadows now use analytic ellipsoids and transparent annuli, without finite-star penumbrae or particle scattering; see [the ring shadow report](PLANET_RING_SHADOWS_REPORT.md). The current planet light uses the primary/effective system star and chart-projected direction, not full binary radiative transfer or simulated 3D planet trajectories. Thermal/retention/history rules are game heuristics, not climate, atmospheric escape or formation simulations. Cracked worlds link to genuine fragment fields; this does not simulate fracture propagation or continuously deform the central planet. Night city lights are available only where an authored map exists (currently Earth). Material LOD is discrete, and PNGs are compressed on disk but resident GPU images are RGBA. Initial material loading is asynchronous and can briefly show a loading state.

Earlier ice wording (“ice uses diffuse lighting; advanced reflections and refraction remain unimplemented”) described the previous state and is superseded: ice now supports Fresnel/GGX reflection and environment refraction. Nearby-object ray tracing, caustics and volumetric scattering remain unimplemented. No n-body or asteroid collision simulation is claimed.

## Folder reconciliation

| Supplied folder | Inspected | Accepted | Rejected |
|---|---:|---:|---:|
| Acid A — Sulfuric Cloud Planet | 24 | 20 | 4 |
| Acid B — Acid Rain Greenhouse | 24 | 19 | 5 |
| Acid C — White Corrosive Cloud World | 28 | 25 | 3 |
| Barren B — Iron-Rich Rust World | 26 | 13 | 13 |
| Barren C — Pale Mineral Highland World | 25 | 17 | 8 |
| Carbon A — Graphite World | 23 | 12 | 11 |
| Carbon B — Diamond-Rich World | 26 | 13 | 13 |
| Carbon C — Carbon Volcanic World | 23 | 11 | 12 |
| Cloud A — Cream Cloud World | 28 | 24 | 4 |
| Cloud B — Amber Cloud World | 26 | 24 | 2 |
| Cloud C — Lavender Cloud World | 26 | 24 | 2 |
| Desert A — Golden Dune World | 20 | 12 | 8 |
| Desert B — Red Canyon World | 20 | 14 | 6 |
| Desert C — White Salt Desert World | 22 | 11 | 11 |
| Dying A — Global Fracture World | 25 | 10 | 15 |
| Dying B — Partially Shattered World | 20 | 10 | 10 |
| Dying C — Ancient Collision Scar World | 31 | 18 | 13 |
| Dying D — Blown apart  worlds | 12 | 0 | 12 |
| Frozen A — Global Ice World | 22 | 14 | 8 |
| Frozen B — Dirty Ice World | 19 | 14 | 5 |
| Frozen C — Blue Glacier World | 21 | 18 | 3 |
| Gaia A — Balanced Habitable World | 25 | 13 | 12 |
| Gaia B — Island Paradise | 23 | 13 | 10 |
| Gaia C — Supercontinent World | 26 | 14 | 12 |
| Inferno A — Fractured Lava World | 23 | 18 | 5 |
| Inferno B — Magma Ocean World | 22 | 9 | 13 |
| Inferno C — Sulfur Hell World | 20 | 10 | 10 |
| Jungle A — Rainforest Supercontinent | 22 | 7 | 15 |
| Jungle B — Tropical Archipelago | 24 | 13 | 11 |
| Ocean A — Global Waterworld | 22 | 14 | 8 |
| Ocean B — Archipelago World | 20 | 13 | 7 |
| Ocean C — Storm Ocean World | 25 | 17 | 8 |
| Ringed Giant A — Pale Ring Giant | 19 | 14 | 5 |
| Ringed Giant B — Copper Ring Giant | 21 | 8 | 13 |
| Ringed Giant C — Blue Ring Giant | 24 | 19 | 5 |
| Super-Earth A — Massive Habitable World | 26 | 16 | 10 |
| Super-Earth B — Rocky High-Gravity World | 27 | 13 | 14 |
| Super-Earth C — Cold Oceanic Super-Earth | 23 | 15 | 8 |
| Toxic A — Green Poison Planet | 28 | 18 | 10 |
| Toxic B — Amber Chemical Planet | 25 | 14 | 11 |
| Toxic C — Blue-Green Chemical World | 23 | 15 | 8 |
| Tundra A — Cold Steppe | 22 | 14 | 8 |
| Tundra B — Semi-Frozen World | 23 | 15 | 8 |
| Tundra C — Boreal World | 24 | 15 | 9 |
| Volcanic A — Shield Volcano World | 21 | 8 | 13 |
| Volcanic B — Rift World | 19 | 11 | 8 |
| Volcanic C — Ash-Caldera World | 19 | 6 | 13 |
