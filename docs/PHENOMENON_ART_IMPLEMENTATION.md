# Supplied phenomenon artwork implementation

The actual C++ Stellar Continuum galaxy and system renderer now uses the supplied
48 PNG files. Procedural generation remains the authority for existence, types,
geometry, intensity, membership and gameplay. New galaxies use configuration v4
and phenomenon schema v2. Existing v3/v1 regions retain their saved simulation.

## ENGINE CAPABILITIES ADDED / EXTENDED

Engine `texture_decal.hpp/.cpp` adds reusable world-to-texture transforms,
aspect-preserving masked decals, luminous/obscuring mask preparation, filtered
LOD selection and adjacent texture batching. `spatial_region_index.hpp` supplies
stable AABB queries for viewport culling and overlap candidates. These modules
have no Core dependency. The existing image preparation queue handles background
work; the existing ordered textured meshes perform the actual drawing.

Core owns the artwork catalog and saved identities, generation configuration,
membership and gameplay. Native presentation consumes those interfaces for map,
local-system, tactical and developer views. The central undiscovered-core veil
also consumes the generic decal preparation and a manifest-selected source.

## 1. Files added

- All 48 original files in `assets/visual/phenomena/`; exact names below.
- `data/stellar/phenomenon-art-v1.json`: catalog, eligibility, masks, dimensions,
  SHA-256 and central-fog visual role.
- `data/stellar/phenomena-v2.json`: new distribution and extent tuning.
- `core/include/stellar/core/phenomenon_art.hpp`,
  `core/src/phenomenon_art.cpp`, `core/src/phenomenon_art_data.hpp.in`.
- `engine/include/stellar/engine/texture_decal.hpp`,
  `engine/src/texture_decal.cpp`,
  `engine/include/stellar/engine/spatial_region_index.hpp`.
- `cmake/NativePhenomenonArtAssets.cmake`.
- `tools/stellar-export/import_phenomenon_assets.py`,
  `tools/stellar-export/test_phenomenon_assets.py`.
- This report.

## 2. Existing files extended for this change

This list describes this artwork task, not unrelated working-tree changes.

- `CMakeLists.txt`, `cmake/StellarNativeClient.cmake`: compile, embed, validate,
  and copy the library into native builds.
- `core/include/stellar/core/galaxy_phenomena.hpp`,
  `core/src/galaxy_phenomena.cpp`, `core/src/galaxy_phenomena_json.hpp`: new families,
  distributions, saved art IDs and backward-compatible region deserialization.
- `core/include/stellar/core/galaxy_configuration.hpp`,
  `core/src/galaxy_configuration.cpp`, `core/src/galaxy_generation_metadata.cpp`,
  `core/src/persistable_fresh_campaign.cpp`: versioned generation and legacy support.
- `app/native_client/native_phenomena.hpp`,
  `app/native_client/native_phenomena.cpp`: real map/local renderer and bounded cache.
- `app/native_client/native_phenomena_debug.hpp`, `app/native_client/main.cpp`:
  inventory, usage, overlays and camera navigation.
- `app/native_client/native_galaxy_backdrop.cpp`: supplied central veil.
- `app/native_client/native_general_settings.cpp`: Space phenomena density label.
- `native-tests/galaxy_phenomena_tests.cpp`,
  `native-tests/native_galaxy_backdrop_tests.cpp`: generation, masks, rendering,
  inheritance, navigation and worker coverage.
- `tools/stellar-export/native_galaxy_art_runtime.py`,
  `tools/stellar-export/test_native_client_runtime.py`: manifest-driven packaging.
- `docs/ENGINE_CAPABILITIES.md`, `docs/PROCEDURAL_PHENOMENA_IMPLEMENTATION.md`:
  current capabilities and historical-renderer clarification.

## 3. Procedural visual output replaced

`NativePhenomena` no longer draws procedural gas as nebula artwork. Source files
provide the visible color and structure. Procedural organic geometry still masks
the artwork and defines authoritative overlap. The procedural density atlas is
restricted to the developer heatmap. The undiscovered central veil uses
`Cleaner Gas Cloud for Star Map Use 2.png`; it is a presentation role, not another
generated region or a new gameplay effect. Central-object discovery rules remain.

## 4. Manifest validation results

All 48 supplied files were inspected and imported without changing their bytes.
Every source is 2944 × 1648. Discovery normalizes spacing, hyphens, underscores,
case and numeric suffixes while preserving original filenames. Validation rejects
unknown names, duplicate mappings, absent categories, path mismatches, missing
files, altered checksums, wrong dimensions and invalid local eligibility.

There are 12 filename categories and nine functional families. Large Cinematic
has five variants. Mixed Nebula Background has variants 1, 2 and 4; no variant 3
was supplied or invented. All other filename categories have four variants.
Runtime logs category counts; build/export validation checks the original bytes.
The native package asset provider adds these 48 files to its existing 16 assets.

| Filename category | Supplied variants |
| --- | --- |
| Blue Reflection Nebula | 4 |
| Cleaner Gas Cloud for Star Map Use | 4 |
| Dark Nebula | 4 |
| Diffuse Space Gas Cloud | 4 |
| Emission Nebula | 4 |
| Faint Nebula | 4 |
| Large Cinematic Nebula | 5 |
| Mixed Nebula Background | 3 |
| Molecular Cloud - Stellar Nursery | 4 |
| Rare Energetic Space Phenomenon | 4 |
| Star-Forming Region - H II Region | 4 |
| Supernova Remnant | 4 |

## 5–6. Exact supplied files and functional mappings

Every listed file lives under `assets/visual/phenomena/`. All are galaxy eligible.
The catalog's normalized functional IDs are shown, with system eligibility.

| Exact filename | Functional family | System view |
| --- | --- | --- |
| Blue Reflection Nebula 1.png | reflection | Yes |
| Blue Reflection Nebula 2.png | reflection | Yes |
| Blue Reflection Nebula 3.png | reflection | Yes |
| Blue Reflection Nebula 4.png | reflection | Yes |
| Cleaner Gas Cloud for Star Map Use 1.png | diffuse_gas | Yes |
| Cleaner Gas Cloud for Star Map Use 2.png | diffuse_gas | Yes |
| Cleaner Gas Cloud for Star Map Use 3.png | diffuse_gas | Yes |
| Cleaner Gas Cloud for Star Map Use 4.png | diffuse_gas | Yes |
| Dark Nebula 1.png | dark_nebula | Yes |
| Dark Nebula 2.png | dark_nebula | Yes |
| Dark Nebula 3.png | dark_nebula | Yes |
| Dark Nebula 4.png | dark_nebula | Yes |
| Diffuse Space Gas Cloud 1.png | diffuse_gas | Yes |
| Diffuse Space Gas Cloud 2.png | diffuse_gas | Yes |
| Diffuse Space Gas Cloud 3.png | diffuse_gas | Yes |
| Diffuse Space Gas Cloud 4.png | diffuse_gas | Yes |
| Emission Nebula 1.png | emission | Yes |
| Emission Nebula 2.png | emission | Yes |
| Emission Nebula 3.png | emission | Yes |
| Emission Nebula 4.png | emission | Yes |
| Faint Nebula 1.png | diffuse_gas | Yes |
| Faint Nebula 2.png | diffuse_gas | Yes |
| Faint Nebula 3.png | diffuse_gas | Yes |
| Faint Nebula 4.png | diffuse_gas | Yes |
| Large Cinematic Nebula 1.png | mixed_nebula | Yes |
| Large Cinematic Nebula 2.png | mixed_nebula | Yes |
| Large Cinematic Nebula 3.png | mixed_nebula | Yes |
| Large Cinematic Nebula 4.png | mixed_nebula | Yes |
| Large Cinematic Nebula 5.png | mixed_nebula | Yes |
| Mixed Nebula Background 1.png | mixed_nebula | Yes |
| Mixed Nebula Background 2.png | mixed_nebula | Yes |
| Mixed Nebula Background 4.png | mixed_nebula | Yes |
| Molecular Cloud - Stellar Nursery 1.png | molecular_cloud | Yes |
| Molecular Cloud - Stellar Nursery 2.png | molecular_cloud | Yes |
| Molecular Cloud - Stellar Nursery 3.png | molecular_cloud | Yes |
| Molecular Cloud - Stellar Nursery 4.png | molecular_cloud | Yes |
| Rare Energetic Space Phenomenon 1.png | rare_energetic | No |
| Rare Energetic Space Phenomenon 2.png | rare_energetic | No |
| Rare Energetic Space Phenomenon 3.png | rare_energetic | No |
| Rare Energetic Space Phenomenon 4.png | rare_energetic | No |
| Star-Forming Region - H II Region 1.png | hii_region | Yes |
| Star-Forming Region - H II Region 2.png | hii_region | Yes |
| Star-Forming Region - H II Region 3.png | hii_region | Yes |
| Star-Forming Region - H II Region 4.png | hii_region | Yes |
| Supernova Remnant 1.png | supernova_remnant | No |
| Supernova Remnant 2.png | supernova_remnant | No |
| Supernova Remnant 3.png | supernova_remnant | No |
| Supernova Remnant 4.png | supernova_remnant | No |

## 7. Spawn-count system

Base mature spiral count bands are configurable in `phenomena-v2.json`:

| Generated systems | Base major phenomena |
| --- | --- |
| Up to 2,000 | 2–4 |
| 2,001–8,000 | 4–7 |
| 8,001–20,000 | 6–12 |
| 20,001–50,000 | 10–18 |
| 50,001–100,000 | 16–26 |
| Above 100,000 | 22–36 |

The mean moves from 35% to 65% through each band's range as size increases.
Seeded triangular variation of 0.8–1.2 multiplies the mean, population multiplier
and morphology multiplier. Seeded stochastic rounding produces the count.
Counts are bounded to 160 and can be zero. Placement can omit a region if it
cannot fit after shrinking and retrying. Natural generation has no family quotas.
Only explicit developer Full Content Coverage requests examples of all nine.

Type-specific base extents vary from 4–10 for rare energetic features through
14–29 for diffuse gas. Extents scale by `(system_count / 500)^0.32`, clamped to
0.7–5, before footprint retries. Art retains its original aspect ratio.

## 8. Type rarity

Mature Spiral starts at Diffuse 26%, Molecular 18%, Dark 16%, Emission 14%,
H II 11%, Reflection 7%, Mixed 5%, Supernova Remnant 2%, Rare Energetic 1%.
These are game weights. After population and morphology modifiers the complete
distribution is normalized. Four legacy enum entries keep their numeric values
for old saves but have zero weight in new natural generation.

## 9. Population State modifiers

Count multipliers are Starburst 1.70, Active 1.30, Mature 1.00, Aging 0.55 and
Quiescent 0.25. Composition uses the requested initial table:

| Family | Starburst | Active | Mature | Aging | Quiescent |
| --- | --- | --- | --- | --- | --- |
| emission | 2.3 | 1.6 | 1 | 0.4 | 0.05 |
| reflection | 1.3 | 1.2 | 1 | 0.6 | 0.1 |
| dark_nebula | 1.3 | 1.1 | 1 | 0.8 | 0.25 |
| molecular_cloud | 1.8 | 1.4 | 1 | 0.5 | 0.1 |
| hii_region | 3 | 1.8 | 1 | 0.3 | 0.02 |
| supernova_remnant | 2.5 | 1.5 | 1 | 0.6 | 0.1 |
| diffuse_gas | 1.2 | 1.1 | 1 | 0.7 | 0.35 |
| mixed_nebula | 1.4 | 1.2 | 1 | 0.7 | 0.2 |
| rare_energetic | 1.5 | 1.2 | 1 | 0.8 | 0.2 |

Visual intensity multipliers are 1.10, 1.05, 1.00, 0.80 and 0.60 respectively.
Elliptical intensity is additionally halved. Density preferences never change
these saved values, the random stream or any gameplay calculations.

## 10. Morphology modifiers and placement

Count multipliers: Spiral 1, Barred Spiral 1.05, Lenticular 0.45, Elliptical 0.25,
Irregular 1.25 and Ring 1.35. Composition multipliers are configurable:

| Family | Spiral | Barred | Lenticular | Elliptical | Irregular | Ring |
| --- | --- | --- | --- | --- | --- | --- |
| emission | 1 | 1.15 | 0.12 | 0.015 | 1.25 | 1.2 |
| reflection | 1 | 1 | 0.3 | 0.1 | 1 | 1 |
| dark_nebula | 1 | 1.1 | 1.6 | 0.15 | 0.9 | 1 |
| molecular_cloud | 1 | 1.2 | 0.12 | 0.01 | 1.4 | 1.5 |
| hii_region | 1 | 1.3 | 0.05 | 0.005 | 1.6 | 1.6 |
| supernova_remnant | 1 | 1 | 1.1 | 1.5 | 1.1 | 1.1 |
| diffuse_gas | 1 | 1 | 2 | 4 | 1 | 1 |
| mixed_nebula | 1 | 1 | 0.8 | 0.25 | 1.1 | 1.1 |
| rare_energetic | 1 | 1 | 1 | 2 | 1 | 1 |

Canonical stellar-region weights favor arms, ring and star-forming regions;
young stars further attract star-forming clouds. Barred star-forming anchors
receive a bar preference; ring anchors receive a ring preference. Lenticular
dark features favor the disk, while diffuse/mixed gas can extend into interarm
regions. Existing regional classification provides irregular clump structure.
Rotated extents and their interiors must fit the galaxy's density footprint and
avoid the central exclusion. Failed placements shrink, then are omitted.

## 11. Galaxy-map rendering

Saved center, extents, rotation and mirror drive a shared inverse world-to-UV
mapping. Pan and zoom preserve the same source coordinates. A uniform fit covers
the organic region without stretching its artwork. Original colors are preserved;
opacity and saved intensity provide restrained variation. The map draws clouds
above its backdrop and below gameplay geometry, labels and UI. Neighbor coverage
caps combined cloud strength at 0.68. Dark/luminous layer order is preserved.

## 12. Full zoom-out rendering

Overview uses the same geometry, location and recognizable color/silhouette.
Fine detail changes with LOD; far opacity is only reduced to 85% of normal.
There is no fade-to-zero at overview. Subpixel culling only removes bounds below
0.35 screen pixels. Automated coverage and actual 720p/1080p/4K captures confirm
major regions remain visible at the fitted complete-galaxy overview.

## 13. System-view inheritance

`phenomenon_context` supplies IDs, dominant region, overlap, density, edge
distance and local intensity from the same authoritative geometry. The shared UV
mapping places the system inside its saved source artwork. A roughly 30–34%
crop varies deterministically with system ID, coordinates and region seed; that
region seed derives from galaxy generation. The strongest three eligible layers
compose into one cached 1536 × 864 background. All overlaps still affect gameplay.

Local alpha is capped at 0.28 before density, close-zoom and combat attenuation.
It draws behind objects, orbits, paths, targeting and UI. Entry uses a 0.65-second
fade. The local crop remains fixed in scale during system camera zoom and is
aspect-preservingly cropped to the viewport. Returning to the map restores the
same world-anchored phenomenon. Clear-space systems retain the normal starfield.

## 14. Types excluded from system view

Supernova Remnant and Rare Energetic are map-only. Tests verify that real overlaps
still exist for gameplay while the resulting local background remains transparent.
Legacy radiation/exotic visuals map to the Rare pool, ionized gas to Diffuse and
dust lanes to Dark; their original simulation effects are preserved.

## 15. Blend and mask implementation

Luminous preparation derives alpha from source brightness and removes unwanted
black matte while preserving hue and faint haze. Obscuring preparation retains
black dust with high alpha; it does not erase dark nebulae. Both use a feathered
elliptical source perimeter and authoritative organic coverage. Outer pixel rows
and columns are transparent. Area filtering works in premultiplied color before
returning straight alpha, avoiding dark LOD fringes. Processing is cached and
performed on the worker; original PNGs remain unchanged.

## 16. LOD implementation

Projected source width selects 256 pixels at ≤360 screen pixels, 768 at ≤1,000,
and the full 2944 near the camera. Low/High visual settings shift these detail
thresholds without changing region positions. A prepared 256-pixel fallback is
shown while larger textures load. At most three visible map regions request full
detail simultaneously; other visible regions retain 768-pixel detail. System
crops sample full source detail. LOD variants share the same world mapping.

## 17. Performance and persistence

Prepared art uses a 96 MiB cache budget with stale-entry eviction and lazy demand.
It does not decode and retain all 48 full images. The existing shared worker
queue has a 32 MiB outstanding reservation limit. One source decode and filtering
scratch are transient worker memory outside the retained image budget. The local
image is about 5.1 MiB; developer heatmaps reserve up to 24 MiB. The central veil
is 1024 × 573, approximately 2.24 MiB, in the backdrop cache. GPU copies are
managed by the existing renderer and are additional memory.

The reusable spatial index culls viewport bounds and limits overlap candidates.
Core's bounded field queries use circle rejection; native per-system results are
cached. Adjacent matching-texture decals batch without reordering. No expensive
alpha extraction happens per frame. A 10,000-query fixture completed below 1 ms
on this host; the mixed-zoom test retained roughly 47.7 MiB of artwork.

New saves persist asset IDs and mirroring alongside existing geometry, effects,
seeds and membership. Deterministic shuffled pools use every matching variant
before repeating. Old v3/v1 saves select a frozen deterministic visual fallback;
they do not regenerate counts, stars, membership or effects. v2 saves remain
without backfilled phenomena. Configuration v4 fingerprints distinguish new rules.

## 18. Developer diagnostics and settings

Hidden Developer/QA Mode, Ctrl+Alt+N, provides Bounds, Types, Density, Overlap,
Region bias and Filename toggles, visual overrides, Previous, Next and Go To.
Navigation wraps through the saved field and centers the actual map camera.
Type labels can identify uncharted clouds in developer mode and draw above art.
Ordinary player discovery filtering remains intact.

`GALAXY PHENOMENA ART USAGE` logs IDs, type, exact filename, position, extents,
rotation, mirror, system eligibility, overlapped system count, base generation
weight, population modifier and morphology modifier, plus counts by type and
unused artwork. The unused list refers to generated regions; the shared central
veil is an independent manifest role. The existing persistent density control is
now labeled **Space phenomena density**, with Low/Medium/High and Medium default.

## 19. Tests and real-renderer evidence

- Complete native C++ build passed.
- All **206/206 CTests passed**, 226.73 seconds, after the main renderer and
  central-veil changes. Subsequent changes only selected the matching v2 region
  tuning table and corrected developer overlay visibility; affected checks were
  rebuilt and rerun separately: **5/5 affected checks passed**, 34.20 seconds.
- Asset import tests: **3/3 passed**. Native export/runtime tests: **68/68 passed**.
  Existing galaxy-asset import tests: **3/3 passed**.
- Manifest validates all 48 original assets. C++ coverage decodes and inspects
  all 48 LOD borders and checks dark-versus-luminous black treatment.
- Deterministic count trends were sampled across six size bands and 2,000
  population seeds. In 54 actual morphology/population test galaxies, Starburst
  totaled 73 regions versus Quiescent 14; Spiral 23 versus Elliptical 6; five
  worlds had no regions. Natural coverage yielded eight families; explicit Full
  Content Coverage yielded all nine.
- Coverage includes rarity normalization, footprint rejection, enum overflow,
  save/load equality, legacy behavior, actual survey effects, local source color,
  edge attenuation, transparent map-only inheritance, overlap caps, aspect ratio,
  world UV continuity, overview visibility, layer order and density invariance.
- Developer tests exercise overlap/filename toggles, previous/next wrap, Go To,
  one-shot navigation and type-label drawing at 720p and 1080p.
- Actual native new-game flow created seed **8057**, 250-system Active Spiral,
  with no injected regions or player knowledge. It naturally contains three
  regions: Emission Nebula 2, Large Cinematic Nebula 3 (Mixed), and Cleaner Gas
  Cloud for Star Map Use 3 (Diffuse), affecting 6, 11 and 7 systems respectively.
  Forty-five source variants are unused in this generated field, as expected.
- Paused native reload/wheel/system-entry captures passed at **1280 × 720**,
  **1920 × 1080** and **3840 × 2160**. All nine captures were visually reviewed.
  They preserve day zero, pause and player knowledge; unknown labels stay hidden.
  A legacy v3 seed-8006 save also passed, showing the new supplied central veil.
- The final 1080p 240-frame steady sample averaged **17.150 ms**, p95 **19.745 ms**;
  scene CPU mean **0.359 ms**, submission mean **0.252 ms**. Cold preparation and
  PNG readback spikes are excluded from that steady measurement. This is a host
  observation, not a universal 60 FPS guarantee. Other resolution smoke runs
  include capture overhead and are visual/function checks, not comparable FPS tests.

Logs: `work/phenomenon-art-full-regression.log`, `work/phenomenon-final-build.log`,
`work/phenomenon-final-checks.log`, `work/phenomenon-final-manifest-validation.log`,
`work/phenomenon-export-runtime-test.log`, `work/phenomenon-export-galaxy-test.log`,
`work/phenomenon-v4-{new-game,720,1080,4k}.log`,
`work/phenomenon-legacy-final.log`.

Actual native screenshots, not mockups:

- [1080p overview](../work/phenomena-captures/supplied-v4-1080.png)
- [1080p regional view](../work/phenomena-captures/supplied-v4-1080-regional.png)
- [1080p inherited system environment](../work/phenomena-captures/supplied-v4-1080-system.png)
- [4K overview](../work/phenomena-captures/supplied-v4-4k.png)
- [4K inherited system environment](../work/phenomena-captures/supplied-v4-4k-system.png)
- [Legacy save and supplied core veil](../work/phenomena-captures/supplied-legacy-final-regional.png)

## 20. ENGINE LIMITATIONS REMAINING

- These are layered 2D source-image decals. Baked background stars remain part
  of source artwork; there is no volumetric gas simulation or semantic star removal.
- Footprint containment is sampled; edge distance is radial, not exact shortest
  distance to arbitrary contours. Existing simulation geometry remains authoritative.
- Local visuals composite the strongest three eligible overlaps. All overlaps
  retain their simulation effects. Full-resolution map requests are capped to
  three visible regions; asynchronous first-use preparation can take seconds.
- This artwork pass retained 250/500/1,000/2,500 systems. The subsequent
  [larger-campaign extension](LARGE_GALAXY_ENGINE_REPORT.md) adds 5,000/10,000.
  Distribution formulas cover the requested 100,000+ bands without certifying
  generation, loading or rendering a 100,000-system campaign.
- Exploration sensor/survey effects and anomaly/resource biases remain active.
  Movement, attrition/radiation, combat visibility, concealment, colonization and
  research-interest values remain available as existing data hooks where their
  consuming mechanics are not yet implemented.
- Legacy saves intentionally retain their old counts and simulation. New tuning
  applies to newly generated v4 galaxies. The source manifest is embedded at
  build time; changing configured files requires rebuilding.
- Native runtime and packaging-provider checks passed; a complete release export
  and hardware-wide performance certification were not performed by this task.
