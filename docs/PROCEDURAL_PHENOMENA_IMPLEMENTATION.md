# Procedural galaxy and system phenomena

This document records the original simulation and procedural-renderer pass.
The visible gas renderer, generation tuning, version numbers and texture budgets
below have been superseded by the [supplied artwork implementation](PHENOMENON_ART_IMPLEMENTATION.md).
That report is the current authority for the 48-image library, world-anchored
map artwork, system eligibility, LOD, configuration v4 and verification results.
The original organic geometry, membership and gameplay remain in use.

This extends the native C++ generation and save authority. It does not replace
the stellar catalogue, population profiles, artwork mappings or fog-of-war.

## ENGINE CAPABILITIES ADDED / EXTENDED

`engine/include/stellar/engine/organic_region.hpp` provides a reusable, seeded,
bounded organic region and noise sampler. Cloud, filament, cluster, shell,
crescent and band silhouettes share the same spatial sampling in simulation and
presentation. Signed edge distance is **radial**, measured along the ray from
the centre, rather than a shortest Euclidean distance to an irregular contour.
The broad phase rejects points outside the bounding circle. A separate nearest
region query evaluates signed distances even outside that circle. Shells include
their inner boundary; hollow centers correctly report as outside.

`core/include/stellar/core/galaxy_phenomena.hpp` and
`core/src/galaxy_phenomena.cpp` own definitions, seeded generation, validation,
membership, overlaps, dominant environments and combined effects. Settings live
in `data/stellar/phenomena-v1.json`, embedded at build time. Ten types cover
emission/reflection/dark nebulae, molecular clouds, H II regions, supernova
remnants, radiation, ionized gas, dust lanes and exotic phenomena. They reuse
`StellarDiscoveryHooks` for future discovery/research/resource interactions.

Generation uses the canonical configuration stream with a separate phenomenon
salt. Counts scale with system count, morphology and population state. Regional
weights favour arms, rings, star-forming regions, irregular clumps and bars;
young stars attract star-forming phenomena and remnants attract energetic
regions. Centers are offset from their stellar anchors. The full rotated ellipse
is sampled against the existing artwork density mask, including its interior,
and avoids the fitted central exclusion zone. Late placement attempts shrink
regions, and failed placements are omitted instead of filling black margins.
Lenticular regions align with the disk; elliptical regions are sparse and faint.
There are at most 160 regions. No fixed quota forces an exotic object to exist.

The region field is persisted in `GalaxyGenerationMetadata.Phenomena`, including
geometry, seeds, type, color, intensity, effects, membership and discovery hooks.
`galaxy_payload_json.cpp` uses `galaxy_phenomena_json.hpp` for this versioned
extension. Native player/developer save envelopes already share that authority.
New canonical galaxies use `galaxy-configuration-v3`. Version 2 fingerprints,
population resolution and saved galaxies remain supported. Older saves are not
backfilled, rerolled or assigned invisible new hazards. Future changes to the
generation rules must bump the generation version; saved geometry remains the
loading authority.

Actual initial gameplay effects:

- Local reconnaissance/science-survey progress is slowed by the saved scanning
  modifier. Survey estimates include it.
- Arrival sensor discovery uses the local effective sensor range.
- Seeded anomaly and rare-resource opportunities are added to eligible systems
  and one real existing body; existing discoveries and Sol are preserved.
- Effect combinations are bounded: sensor multiplier >= 0.45, scanning effort
  <= 1.8, anomaly bias <= 0.4, resource bias <= 0.25.

`app/native_client/native_phenomena.{hpp,cpp}` binds an immutable presentation
copy of the canonical field. Engine `procedural_gas.hpp` samples warped gas,
fine illuminated filaments and absorbing dust pockets. The map uses one shared
1536 x 864 gas material, tinted by each saved region. Engine
`texture_coverage_mesh.hpp` combines authoritative world coverage with fixed
screen-space texture coordinates. Zoom changes the visible region boundaries
without enlarging the gas wisps. The existing textured `TriangleMesh` is now
available in ordered world commands as well as overlays; no second renderer is
introduced. This helper can also serve other world masks with steady detail.

Local 1536 x 864 textures derive from phenomenon seeds, system ID and coordinates;
they retain the containing type/color and local overlap strength. Equal-type
overlaps combine into at most ten material families, bounding preparation work.
The image covers the viewport at every system zoom. The existing Engine
`ImagePreparationQueue` prepares map and local textures off the UI thread with
backpressure/cancellation. Normal map plus local images occupy 10.125 MiB of CPU
pixels. An optional developer atlas uses adaptive 128/192/256/512-pixel tiles,
bounded to 24 MiB including its density heatmap. There is no texture per region
or per-frame texture generation. Local overlap queries are cached per system.
Central undiscovered-core fog uses a separate cached 1024-square gas material
(4 MiB), translucent dust structure and the same fixed-scale coverage helper.
It also prepares on the shared worker, with capacity retry and cancellation;
the render thread never generates the high-detail fog. The central black hole's
discovery rules remain unchanged.

Map clouds are between the macro backdrop and gameplay geometry. New galaxies
use the generated clouds in place of the generic regional nebula image. System
clouds follow a deterministic starfield and precede all objects, orbit paths and
labels. Dark masks attenuate background stars, not gameplay objects. Combined
local texture alpha is capped at 0.28 before visual-density and zoom attenuation.
Close views reduce clutter; combat further attenuates clouds and places them
after the opaque tactical base, before all tactical indicators. There is no
foreground cloud layer. Entry/exit alpha transitions take 0.65 seconds.

General Settings has a persistent **Nebula density** dropdown (Low, Medium,
High; Medium default). It changes rendering only. It is separate from galaxy
generation and save state. Hovering or clicking a cloud inspects it; detailed
information requires partial survey of an intersecting system. The system header
shows the actual local environment and active sensor/survey modifiers.

Developer campaigns use **Ctrl+Alt+N** for phenomenon diagnostics. Controls show
bounds, type labels, mask density heatmap, membership counts and regional
affinities, plus a 0/25/50/75/100% visual override. The scrollable data includes
configuration fingerprint, shape seeds, system IDs, local seed, dominant and
secondary overlaps, density, signed edge distances and gameplay modifiers.

## Validation

`native-tests/galaxy_phenomena_tests.cpp` covers all morphologies and representative
population states, reproducibility, zero-region cases, all ten types across a
seed sample, save/load equality, legacy v2 preservation, invalid geometry and
membership rejection, actual survey advancement, overlap caps, nearest-region
queries, local texture reproducibility, changed seeds, shared color, weaker edge
clouds, clear-space textures, transparent atlas borders, render ordering,
combat/zoom attenuation, visual-only preferences and spatial query cost. Actual
map and system draw commands verify that texture coordinates and image bounds
retain their scale across zoom; backdrop tests protect central gas transparency.
Saved enum values are range-checked before narrowing, including overflow inputs.
`galaxy_configuration_tests.cpp` additionally covers all supported size/state/type
combinations. General Settings tests exercise dropdown selection, save, cancel,
defaults, persistence and coexistence with screenshot/navigator preferences.

The native visual replay uses a naturally generated cloud-embedded homeworld:
seed 8006, Spiral, Random resolved to Active, 250 systems, Pelagic High-Pressure,
six pre-warp civilizations and one ancient civilization. No cloud or knowledge
was injected for the capture. Evidence is under `work/phenomena-captures` and
`work/phenomena-*.log`. Final verification results are recorded in the capability
registry after the regression run.

Final September 17 verification:

- Full native build passed (`work/phenomena-final-build.log`).
- 206/206 CTests passed in 208.82 seconds
  (`work/phenomena-complete-regression.log`). After the final central-fog worker
  change, all seven directly affected rendering/settings/worker checks passed
  in 13.83 seconds (`work/phenomena-final-targeted.log`). They include queue
  saturation, cancellation/retry and synchronous/background pixel equality.
- Actual paused native reloads at 1280 x 720, 1920 x 1080 and 3840 x 2160 passed
  overview, wheel zoom, camera pan and observed-system double-click entry. All
  captures retained the campaign day, fog-safe labels and proper view gating.
  Final screenshots are `work/phenomena-captures/final-gas-{720,1080,4k}.png`
  with `-regional` and `-system` companions. These were visually reviewed.
- The final 1080p 240-frame steady sample measured 16.692 ms mean interval,
  19.230 ms p95; scene preparation averaged 0.710 ms and renderer submission
  0.351 ms. The one-time cold scene maximum fell from 336.944 ms to 26.633 ms
  after moving central-fog generation to the worker. PNG readback/compression
  time is excluded from steady profiling. This is approximately 60 FPS average
  on this host, not a guarantee that every frame meets 16.67 ms. The 4K run was
  a visual/navigation check, not a 60 FPS certification.

## File inventory for this extension

Added:

- Engine: `engine/include/stellar/engine/organic_region.hpp`,
  `procedural_gas.hpp` and `texture_coverage_mesh.hpp` in the same directory.
- Core: `core/include/stellar/core/galaxy_phenomena.hpp`,
  `core/src/galaxy_phenomena.cpp`, `core/src/galaxy_phenomena_json.hpp`,
  `core/src/galaxy_phenomena_data.hpp.in`.
- Definitions: `data/stellar/phenomena-v1.json`.
- Native: `app/native_client/native_phenomena.hpp`, `native_phenomena.cpp`,
  `native_phenomena_debug.hpp` in the same directory.
- Tests/report: `native-tests/galaxy_phenomena_tests.cpp` and this document.

Extended existing work:

- `CMakeLists.txt`, `cmake/StellarNativeClient.cmake`,
  `cmake/NativeClientLogicTests.cmake` register sources, embedded definitions and tests.
- `core/include/stellar/core/galaxy_configuration.hpp` and
  `core/src/galaxy_configuration.cpp` support versioned v3/v2 configuration identity.
- `core/include/stellar/core/galaxy_generation_metadata.hpp`,
  `core/src/galaxy_generation_metadata.cpp`, `galaxy_payload_json.cpp` and
  `persistable_fresh_campaign.cpp` integrate canonical generation and persistence.
- `core/include/stellar/core/exploration_advance.hpp`,
  `core/src/exploration_advance.cpp` and `campaign_coordinator.cpp` connect real
  exploration to the region field.
- `app/native_client/main.cpp`, `native_galaxy_backdrop.{hpp,cpp}`,
  `native_system_workspace.{hpp,cpp}` and `native_general_settings.{hpp,cpp}`
  integrate views, settings and developer diagnostics.
- `native-tests/native_general_settings_tests.cpp` and
  `docs/ENGINE_CAPABILITIES.md` extend regression coverage and the shared registry.
- `engine/include/stellar/engine/native_map_platform.hpp` and
  `engine/src/native_map_platform.cpp` allow existing textured meshes in the world
  layer; `native-tests/native_galaxy_backdrop_tests.cpp` checks the fog consumer.

## ENGINE LIMITATIONS REMAINING

Radiation/attrition, shield stress, movement, colonization, research interest,
combat concealment and special events are stored hooks; this change does not
claim those additional simulation mechanics fire. Existing same-system combat
scanner contacts are unchanged. Active penalties are sensor discovery and survey
effort, with generated discovery biases as described above.

The renderer uses cached 2D masks, not physical volumetric scattering. Footprint
validation samples the enclosing ellipse rather than proving containment at
every continuous point. Diagnostics expose actual masks and regional affinities,
not a separate probabilistic spawn-heatmap simulation. Tuning should continue
against play sessions. Measured frame timings are specific to this host; this
work does not certify a universal 60 FPS or the remaining unrelated release gates.
