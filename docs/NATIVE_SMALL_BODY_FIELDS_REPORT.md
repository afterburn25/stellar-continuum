<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Native small-body fields — implementation report

Current rendering and zoom are updated in [the 3D follow-up report](NATIVE_SMALL_BODY_3D_REPORT.md).
Its solid-body rendering replaces the billboard limitations described below.

Test build: Stellar Continuum 0.1.13-alpha, small-body-fields-test-20260918.
This extends the native C++ Stellar Engine application. No new game engine or managed runtime is required.

## Player and developer entry points

Open **Developer Game.cmd**, enter Sol, then choose **BELTS & DEBRIS**. Browse fields or individual bodies and choose **Focus body**. Click a visible close body to inspect its composition and resources. The simulation clock drives motion; pause freezes both orbit and spin. Fit System restores the overview.

Developer campaigns additionally offer four working commands: asteroid belt, ice belt, debris disk and cracked-world debris. The band toggle exposes the region boundaries; the survey panel reports density, counts, variant, spin, orbit exponent and draw batches. These controls operate on the isolated developer campaign. Ordinary play keeps survey restrictions and cannot issue the developer commands.

## 1. Files added

Paths below are relative to the repository root.

- Engine: `engine/include/stellar/engine/analytic_orbit.hpp`, `engine/include/stellar/engine/billboard_batch.hpp`.
- Core public interfaces: `core/include/stellar/core/small_body_fields.hpp`, `core/include/stellar/core/small_body_commands.hpp`.
- Core implementation: `core/src/small_body_configuration.cpp`, `small_body_configuration_data.hpp.in`, `small_body_fields.cpp`, `small_body_motion.cpp`, `small_body_commands.cpp`, `small_body_json.hpp`.
- Application: `app/native_client/native_small_body_assets.hpp`, `native_small_body_renderer.hpp`, `native_small_body_renderer.cpp`, `native_small_body_panel.cpp`.
- Content: `data/stellar/small-body-fields-v1.json`, `export/native-small-body-assets.json`, and all 36 original named PNGs under `assets/visual/small-bodies/`.
- Build/export: `cmake/NativeSmallBodyAssets.cmake`, `tools/stellar-export/native_small_body_runtime.py`.
- Tests: `native-tests/small_body_fields_tests.cpp`, `native-tests/native_small_body_renderer_tests.cpp`, `tools/stellar-export/test_native_small_body_runtime.py`.
- Documentation: this report. Reproducible import, build, staging, replay and archive helpers are under `work/small-bodies/` and are development tools, not runtime dependencies.

## 2. Existing files extended

Some files were already uncommitted from earlier native work. This list describes changes in this task, not the entire working-tree diff.

- `CMakeLists.txt`, `cmake/StellarNativeClient.cmake`, `cmake/NativeClientLogicTests.cmake`: compile Core/application code, embed configuration, stage assets and register tests.
- `engine/include/stellar/engine/texture_decal.hpp`, `engine/src/texture_decal.cpp`: opaque cutout preparation.
- `core/include/stellar/core/galaxy_catalog.hpp`, `campaign_foundation_persistence.hpp`, `planetary_catalog.hpp`, `planetary_body_persistence.hpp`, `planetary_classification.hpp`: field records, persistence DTOs and cracked-world classification.
- `core/src/campaign_foundation_persistence.cpp`, `planetary_body_persistence.cpp`, `galaxy_payload_json.cpp`, `galaxy_payload_persistence.cpp`, `fresh_campaign.cpp`: capture/restore, validation, optional JSON extensions and generation.
- `app/native_client/native_campaign_session.cpp`, `native_system_view.hpp/.cpp`, `native_system_workspace.hpp/.cpp`, `main.cpp`: legacy-session initialization, observer snapshots, shared layout, rendering, input, developer commands and graphical replay.
- `native-tests/native_system_view_tests.cpp`, `native_system_workspace_tests.cpp`, `large_galaxy_tests.cpp`: enlarged-layout expectations, outer-system zoom/focus regressions and full field persistence at galaxy scale.
- `tools/stellar-export/native_client_runtime.py`, `test_native_client_runtime.py`: runtime allowlist integration and export fixtures.
- `docs/ENGINE_CAPABILITIES.md`, `docs/engine/ARCHITECTURE.md`: ownership, interfaces, tests, save/performance implications and limits.

## 3. ENGINE CAPABILITIES ADDED / EXTENDED

Engine now supplies unit-independent Kepler orbit evaluation, deterministic quaternion spin/precession, monotonic orbital display scaling, indexed billboard batching and edge-connected background removal that preserves enclosed shadows. Engine knows nothing about minerals, planet classes or campaign rules.

Core owns the nine field types, seeds, profiles, resource ledgers, generation context and validated developer commands. Application consumes observer-filtered snapshots and the canonical campaign clock. It owns art loading, draw batches, picking and display coordinates. No second simulation is maintained by the UI.

Public reusable interfaces include `analytic_orbit_position`, `analytic_spin_rotation`, `stretched_orbit_radius`, `append_billboard`, `prepare_decal_texture`, `generate_small_body_fields`, `small_body_instance`, `small_body_environment`, `extract_small_body_resource`, `harvest_small_body_supply` and `force_developer_small_body_field`.

## 4. Small-body field architecture

Nine distinct content types share one implementation: rocky, metallic-rich, carbonaceous and mixed asteroid belts; ice belts; debris disks; shattered belts; cracked-world clusters; planetary debris halos.

A version-1 `SmallBodyField` records local identity, 64-bit seed, AU bounds and thickness, density, logical/visible counts, eight composition weights, tilt/eccentricity, clustering, arc extent, phase/epoch, central mass, spin distribution, asset pools/variants, optional parent identities, context profile and sparse extraction records.

`SmallBodyInstance` is reconstructed from field seed and body index. That identity determines material, artwork variant, orbit, spin, scale, brightness and cluster. LOD changes select a stable prefix; they never reroll bodies. Save files store the compact field and modifications, not tens of thousands of untouched instances.

## 5. Sol baseline

Sol always starts with a mixed rocky/metallic/carbonaceous belt at **2.1–3.3 AU**, between Mars and Jupiter, plus an ice belt at **32–50 AU**, beyond Neptune. Sol's existing planetary order is retained. Its ice composition includes water, methane, ammonia, mixed rock/ice and frozen volatiles.

Procedural rocky belts normally use the gap between outer terrestrial worlds and the first giant; systems without that gap use a middle-to-outer fallback region. Ice fields start beyond the outermost generated non-moon body. Stellar destruction radii constrain generated regions.

## 6. Rarity and configuration

The base category weights total 100:

| Category | Weight |
| --- | ---: |
| No primary belt | 25 |
| One asteroid belt | 34 |
| One ice belt | 14 |
| Asteroid and ice belts | 20 |
| Multiple belts | 7 |

Independent disk and shattered overlays start at 7% and 4%. Therefore “no primary belt” does not prohibit a separate debris overlay. Young/protostar systems multiply disk probability by 3; old systems multiply shattered probability by 1.5; disturbed history doubles shattered probability. Giants increase nonempty belt category weights by 1.12 before normalization. Resource-rich archetypes increase metallic subtype weight by 1.8.

The JSON table also owns all nine density ranges, compositions, logical/visible counts, clustering, thickness, eccentricity, arc size, pools, cracked modifiers, spin and visual scaling. The build validates and embeds it in Core. Editing the shipped copy alone does **not** hot-reload rules; rebuild after authoring changes.

## 7. Cracked-world influence

`PlanetaryBody::cracked_world` is authoritative, saved and exposed as the Cracked class. Fresh procedural generation has a 2.5% chance per eligible uninhabitable solid planet, doubled in disturbed systems; Sol and candidate home/colonization worlds are excluded.

Each cracked world adds an 80% local cluster chance (40% baseline ×2), a 32% expanded shattered-region chance and a 45% planetary halo chance. Local clusters initially align with the parent's displayed orbital phase and span 0.93–1.08 times its orbital radius; expanded shattered regions use 0.8–1.2. Planet-centered halos use 3–10 physical planet radii and the planet's mass.

The developer command selects an eligible uninhabited solid parent, rejects colonies and pre-warp populations, stages the field before committing, sets the cracked flag and marks developer tools used. Partial rubble arcs, clustered chunks and the supplied shattered art distinguish this content.

## 8. Orbital motion

Kepler elements include radius, eccentricity, inclination, node, periapsis, phase and signed angular speed. Speed derives from central mass and AU radius; 3% of samples are retrograde. Eccentric excursions are bounded by the field. Seeded gaps and eleven cluster groups break uniformity.

Positions are evaluated from canonical simulation days minus the saved field epoch, so pause and reload are stable. Coarse haze rotates at the region's mean analytic rate. Individual bodies retain their own rates. No per-frame authoritative integration or frame-rate dependence is introduced.

## 9. Spin and tumbling

Every sample gets an axis, signed rate and initial phase. The default rate range is 0.1–1 radian/day. Eighteen percent also receive secondary precession/wobble. Quaternion evaluation is deterministic. Billboards express this as roll and foreshortening; this is not a newly rendered 3D surface with changing self-shadow geometry.

## 10. Orbit stretching

The baseline transform is **2600 × AU^0.32**, followed by planet-family clearance constraints. A shared monotonic interpolation through these adjusted orbit anchors maps both planets and field radii into the same presentation space. This preserves Mars/belt/Jupiter ordering while opening inner orbits and keeping outer orbits reachable.

Physical AU values, stellar exposure, resources and simulation travel rules are unchanged. Camera pan limits now grow with system size and zoom, fixing outer-planet focus followed by wheel zoom. Small-body focus adapts to body size, with a dedicated close-view zoom ceiling.

## 11. Planet visual scale

System planets receive a **1.22×** display factor and moons **1.18×**. Existing class/radius distinctions and ordering remain. Orbit clearances account for the enlarged planet families. Physical radii, masses and the planetary globe assets are unchanged.

## 12. Named artwork and LOD

All nine supplied names are authoritative asset pool identifiers, with variants 1–4 retained verbatim. The manifest records original dimensions and SHA-256 values. All 36 files are copied unchanged. There are 35 distinct hashes because `Asteroid belt far-view layer 1.png` and `4.png` are identical source files; both identifiers remain available. The supplied “Ice cluster layer” name is honored even though those images look rocky.

Eight isolated-body textures use edge-connected black-background removal, retaining dark enclosed shadows. Worker jobs prepare 768-pixel isolated textures and 512-pixel-wide luminous field layers. Dust/far-view images wrap onto orbital sectors with gaps and varying opacity. They are not pasted across the whole system as rectangles.

Far views use sparse point batches, faint haze and cluster overlays. Medium/close views use textured batches with stable sample identities; close views omit the large cluster overlay cards. Water/methane/ammonia/mixed-ice materials use icy body art, while rock/metal/carbon receive appropriate tones.

The renderer caps visible reconstruction at 2,048 samples per field and 32 fields per system. Normal profiles use 440–900 samples from 8,000–40,000 logical bodies. Far LOD draws up to 160 or 420 samples; near LOD uses the configured visible count. Body visibility is culled to the viewport. Texture batches split at Engine mesh limits. Cached prepared images are bounded by the fixed 36-entry catalog, approximately 34 MiB if all are loaded; decoding uses the existing bounded worker queue.

## 13. Save/load and gameplay hooks

Galaxy JSON gains optional `SmallBodyFields` and true-only `CrackedWorld` properties. Field version, integer identities/counts, exact array lengths, finite ranges, composition, variants, depletion, parent ownership and acyclic references are validated. Legacy absent catalogs remain readable; native campaign activation deterministically initializes them once. Explicit empty catalogs remain empty. Standalone canonical legacy restore does not silently rewrite old payloads.

Saved seeds, profiles, epoch and campaign clock reconstruct body variants, motion and spin without reroll. Sparse extraction records persist depletion. Field generation uses an independent random stream and leaves preexisting galaxy generation parity intact.

Core exposes resource yields for minerals, metals, organics, water, hydrogen, deuterium, volatiles, salvage and exotic minerals. `extract_small_body_resource` validates and depletes one resource; `harvest_small_body_supply` apportions requested yields within capacity and returns quantities for a caller-owned cargo/supply transaction. Environment queries expose navigation cost, concealment, hazards and scan difficulty. The survey panel consumes these values.

These are usable gameplay hooks, not a new fleet mining order system. Automatic cargo crediting, colony supply scheduling, fuel processing and salvage missions must call these APIs within their own access, effort and ownership policies.

## 14. Verification

- Native checks cover all nine types, exact Sol placement, 12,000-seed base rarity, 2,000-seed cracked clustering, stable LOD identities, four variants, spin distribution, independent orbit reference, authoritative depletion, supply capacity, invalid saves and JSON round-trip.
- Rendering checks cover opaque shadow preservation, physical/display ordering, planet size coherence, batch validity, LOD budgets, picking, culling, pause stability and survey gating. Workspace regressions cover outer-planet zoom and small-body focus.
- Focused regression suite: **17 passed**, including fresh/legacy persistence, settlement/system consumers and 2,500/5,000/10,000-system generation and save round-trips.
- Export suite: **534 run, 517 passed, 17 skipped**, with no failures. New tests validate all 36 asset identities/hashes and reject malformed asset manifests.
- Graphical replay and final archive validation are recorded in the packaged `SmallBodyFieldsValidation.txt`.

The 10,000-system test generated 13,818 fields and a 107,750,799-byte full galaxy payload; full generation took about 450 ms and encode/decode/restore validation about 7.17 s in this run. The 2,500/5,000 tests produced 3,522/6,908 fields. These measurements include existing world content and ran alongside other checks. They are not isolated renderer or late-game performance benchmarks.

## 15. ENGINE LIMITATIONS REMAINING

- This implementation uses CPU-generated indexed billboard batches, not hardware GPU instancing, body meshes or n-body gravity. Quaternion foreshortening is an artistic approximation of tumbling.
- Planets retain the existing schematic fixed-phase system layout. Small bodies orbit; planet-centered halos remain anchored to their displayed parent. Cracked stellar debris begins near the parent's phase and shears around its orbital region over time.
- Navigation, concealment, hazards and scans use a coarse region query. Existing fleet pathfinding does not yet consume those costs or collide with individual rocks. Mining/supply APIs do not add automatic fleet orders, cargo transfers or colony income.
- Composition and violent history use existing archetypes, stellar age/type, giants and engulfment history. No new metallicity catalog, collision-history simulation, ruin-site generator, Oort cloud or destruction animation is included.
- Close-body lighting is baked into the supplied artwork. The field textures and density model are artistic; sampled objects are deliberately sparse. Further tuning can improve sector seams, fade transitions, shape diversity and resource balance.
- Texture preparation is asynchronous and lazy; first visits can briefly show fallback dots. Body draws are culled, but coarse sector geometry is still built for every field in the current system. Stress profiling at the 32-field developer cap and larger late-game populations remains useful future work.
- Configuration is validated at build/runtime initialization and compiled into the executable; hot reload and content-version migrations beyond field version 1 remain future extensions.

The reusable Engine primitives can support comets, satellites, fragment effects and other seeded objects without duplicating campaign rules in render code.
