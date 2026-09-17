# Stellar generation integration — 0.1.11 Alpha

This extends the native C++ implementation on `cpp/codex-native-architecture-integration`, based on `b812995b`. The profile addendum is integrated into the same generator, physical definitions, artwork renderer, knowledge state and Player17 save path. No second generator, stellar type list, research framework or save format was introduced.

## Authoritative data and generation

`data/stellar/population-v1.json` owns the 23 ordinary stellar types, the exact 1,000,000-millionth baseline, physical ranges, visual attributes, discovery hooks and hazard tuning. `data/stellar/population-profiles-v1.json` owns only morphology, activity, regional and bounded variation factors. Both are embedded at build time and included in the package for auditability. Tuning changes require rebuilding and assigning a new generation/profile version if old seed reproduction is to remain supported.

The six presets are Mature Spiral, Mature Barred Spiral, Aging Lenticular, Quiescent Elliptical, Active Irregular and Mature Ring. All five activity states are selectable independently: Starburst, Active, Mature, Aging and Quiescent. All twelve region profiles are integrated: disk, spiral arm, bulge, star-forming region, ring, older interior, inner disk, inter-arm, outer disk, halo, irregular clump and bar. Exact per-type factors, normalized presets and implementation details are in [the profile report](stellar-population-profiles.md) and the configuration.

Positions and region labels are generated first. A deterministic calibration over actual regional counts retains the whole-galaxy target in expectation while concentrating young types in arms, ring sites or irregular clumps. Ellipticals use a spheroidal distribution; lenticulars use a disk/bulge without spiral arms. Spiral and barred presets share their global baseline. Rarity remains probabilistic: empty rare categories are valid and are never backfilled.

Known nearby catalog positions/classes are retained as 96 anchors. Their generated physical parameters are not presented as measurements; Sol uses its reference values. Anchors are reported separately from procedural population statistics. Map dimensions follow the existing count-dependent galaxy bounds, with the ordinary stellar population outside the central exclusion zone.

Independent fixed integer random streams separate spatial placement, type selection, physical properties and the central object. The same seed, settings and generation/profile version reproduce the same galaxy, including rare objects. Physical values, planet exposure, regional assignments, central state and active hazard-avoidance routes are saved. Loading never rerolls them. Legacy saves without these fields retain their earlier galaxy and visuals.

## Artwork and observation

All 33 supplied PNGs are packaged with unchanged source bytes and SHA-256 checks. The manifest covers 25 close representations, including the quiet M-dwarf and separate central object, and automatically pairs all eight ` Distance` variants. [The asset report](stellar-asset-validation.md) lists every mapping and fallback.

Runtime preparation removes the black matte, fades outside edges and preserves enclosed dark event horizons. Shared 128-pixel distant representations crossfade to 1,024-pixel close artwork over a screen-radius interval of 18–46 pixels. Newly prepared close artwork also fades in over 0.45 seconds, avoiding a decode-completion pop during quick zooms. Rendering never requires thousands of full-resolution textures: four close types, 25 distant types and two pending preparation jobs are bounded; crowded views retain distant LOD for excess close types without repeated decode/eviction churn. Camera movement does not reroll artwork or regenerate stellar physics. Existing intermittent prominence animation is shared by legacy and supplied normal-star surfaces.

Owned settlements and explored systems reveal spectral color for their three nearest neighbors, while farther unexplored systems remain white. This exposes color only. Detailed class artwork, hazards, physical measurements and discovery hooks require a completed survey. Existing binary/triple markers remain, with enlarged primary stars included in companion separation and label avoidance.

The central supermassive black hole is a single separately generated object at the configured center, outside the ordinary population table. Configured initial state probabilities are 90% quiescent, 9% accreting and 1% relativistic jets. Its artwork is gated by the existing access/unlock plus exploration knowledge path; broad core fog is not an object discovery. No new expedition mission or rare-object reward gameplay is invented here.

## Physical and gameplay integration

Each generated object has mass, radius, luminosity, effective temperature, age/lifetime, radiation/wind/habitability factors, safety distances and optional jet orientation. Ordinary luminous objects use consistent radius/temperature/luminosity combinations; black holes are not treated as luminous stellar photospheres. Class ranges are gameplay approximations, not a full stellar-evolution simulation.

The initial conservative temperate zone is `0.95–1.67 × sqrt(luminosity / Sol)` AU. Generation removes engulfed planetary orbits and their moons, records how many were removed, evaluates flux at remaining orbits and flags baked worlds. Evolved stars can have scorched inner systems and warmer outer orbits. Hot massive-star habitability also accounts for radiation and short lifetime; a temperate orbit alone does not create a viable Earth-like world.

Baked worlds cannot be colonized or used by normal surface placement, upgrades, hub repair or close-approach mining/outpost planning. Their inspection explains extreme irradiation. Fleet local routes avoid the stellar danger region and directional jet cones, use safe staging positions, and measure travel time over the full detour. Mid-route saves preserve exact route progress. System maps draw the safety boundary and directional hazards from that same navigation scale. The existing orbital map remains a gameplay schematic, rather than claiming true physical scale for every displayed planetary spacing.

Discovery event IDs, feature/research/resource/interaction IDs, rarity tier, sensor signature, hazard profile and text keys are shared data hooks. Future opportunities can use them without guaranteeing rare spawns or revealing unseen objects in normal UI.

## Verification evidence

Focused native tests passed for:

- One million deterministic baseline rolls, exact supplied presets after rounding normalization, and all 360 morphology/activity/region combinations.
- Whole-galaxy target calibration, bounded variation, zero default Quiescent Elliptical O/WR/hypergiant eligibility, explicit rejuvenating activity and no forced rarities.
- Regional contrasts across 100,000 generated system positions, including young-star concentrations in arms, rings and clumps and no spiral-arm placement in elliptical/lenticular maps.
- Exact seed/settings reproduction and semantic save/load identity at 250, 500, 1,000 and 2,500 systems; legacy save preservation; 20 species/morphology/activity homeworld cases.
- Luminosity-scaled habitable zones, baked/engulfed worlds, directional hazards, safe fleet movement and persisted mid-route movement.
- Inspection redaction before full survey, central unlock versus exploration separation, and no unknown identity disclosures in graphical smoke captures.
- All 33 artwork mappings, eight distance pairs, alpha boundaries, zoom and readiness fades, bounded asynchronous jobs, stable crowded-view caches and retained intermittent solar activity.

The complete CTest suite passed all 192 tests. The native package dependency suite passed all 68 tests, including the newly added missing/tampered stellar image, incomplete canonical mapping and out-of-scope path checks. Its temporary asset fixture now includes the new stellar declarations and documentation.

Actual Vulkan captures were inspected at 720p and 1080p, including galaxy creation, the galaxy/regional/system transitions, close/distant stellar artwork and 2,500-system ring and elliptical maps. The measured 2,500-system ring pass at 1920×1080 had a steady mean interval of 16.714 ms (about 59.8 FPS with presentation pacing), p95 16.859 ms and p99 17.025 ms over 180 measured frames. Scene construction averaged 0.352 ms. This is a measured machine/workload result, not a universal 60 FPS guarantee; cold uploads, saves and capture readbacks are recorded separately.

Raw development evidence lives under `work/stellar-*.log`, `work/stellar-*-diagnostics.txt`, `work/stellar-*.bmp` and `work/stellar-morphology-captures/`. Developer reports contain hidden generated objects and are never fed into normal player screens. The standard export additionally gates the release on the complete CTest suite, Python packaging tests and relocated runtime save/load/render checks; the export log is the authoritative release-gate record.

## Integration decisions and limits

The profile workstream's canonical enum IDs and baseline were reused without conflicts. Activity factors are applied relative to each preset's reference activity, preventing double-application of Active Irregular or Quiescent Elliptical. Region calibration prevents a denser ring from silently altering the intended whole-galaxy weights. The old coarse three-group modifier prototype was removed in favor of the single profile configuration. No unrelated menu, research, display-mode, sound or planetary-management redesign was included.

New generation rules apply when creating a galaxy. Existing saves are deliberately not reconstructed or populated with new rare objects. New authored quests, rewards, core-expedition gameplay and a complete stellar-evolution/orbital simulation remain future work, as requested by the hook-based scope.

## Scientific context

The requested population percentages and central state odds are game-tuning values. Stellar temperature/radius/luminosity relationships, evolutionary distinctions and the limitations of temperate-zone habitability are informed by [NASA stellar types](https://science.nasa.gov/universe/stars/types/), [NASA exoplanet stars](https://science.nasa.gov/exoplanets/stars/), [NASA Goldilocks stars](https://science.nasa.gov/missions/hubble/goldilocks-stars-are-best-places-to-look-for-life/), and [NASA Exoplanet Archive calculations](https://exoplanetarchive.ipac.caltech.edu/docs/poet_calculations.html).

## Files added

- `app/native_client/native_stellar_art.cpp`
- `app/native_client/native_stellar_art.hpp`
- `core/include/stellar/core/stellar_object.hpp`
- `core/include/stellar/core/stellar_population_profiles.hpp`
- `core/src/stellar_object.cpp`
- `core/src/stellar_object_json.hpp`
- `core/src/stellar_population_config.hpp.in`
- `core/src/stellar_population_profiles.cpp`
- `core/src/stellar_profiles_config.hpp.in`
- `data/stellar/population-profiles-v1.json`
- `data/stellar/population-v1.json`
- `docs/stellar-asset-validation.md`
- `docs/stellar-generation-validation.md`
- `docs/stellar-population-profiles.md`
- `native-tests/native_stellar_art_tests.cpp`
- `native-tests/stellar_object_tests.cpp`
- `native-tests/stellar_population_profiles_tests.cpp`
- `tools/generate_stellar_asset_manifest.py`
- `assets/visual/stellar/`: 33 supplied PNGs and `manifest.json`; every image is enumerated in the asset report.

## Files modified

- `CMakeLists.txt`
- `app/native_client/main.cpp`
- `app/native_client/native_body_inspection.cpp`
- `app/native_client/native_celestial_appearance.cpp`
- `app/native_client/native_celestial_appearance.hpp`
- `app/native_client/native_galaxy_backdrop.cpp`
- `app/native_client/native_galaxy_backdrop.hpp`
- `app/native_client/native_galaxy_star_markers.cpp`
- `app/native_client/native_galaxy_star_markers.hpp`
- `app/native_client/native_inspection.cpp`
- `app/native_client/native_new_campaign_setup.cpp`
- `app/native_client/native_new_campaign_setup.hpp`
- `app/native_client/native_new_game_workspace.cpp`
- `app/native_client/native_new_game_workspace.hpp`
- `app/native_client/native_startup_entry.cpp`
- `app/native_client/native_startup_workspace.cpp`
- `app/native_client/native_startup_workspace.hpp`
- `app/native_client/native_system_view.cpp`
- `app/native_client/native_system_view.hpp`
- `app/native_client/native_system_workspace.cpp`
- `app/native_client/native_system_workspace.hpp`
- `cmake/StellarNativeClient.cmake`
- `core/include/stellar/core/campaign_foundation_persistence.hpp`
- `core/include/stellar/core/fleet_persistence.hpp`
- `core/include/stellar/core/fleet_state.hpp`
- `core/include/stellar/core/fleet_transit.hpp`
- `core/include/stellar/core/fresh_campaign.hpp`
- `core/include/stellar/core/galaxy_catalog.hpp`
- `core/include/stellar/core/galaxy_generation_metadata.hpp`
- `core/include/stellar/core/persistable_fresh_campaign.hpp`
- `core/include/stellar/core/planetary_body_persistence.hpp`
- `core/include/stellar/core/planetary_catalog.hpp`
- `core/src/campaign_foundation_persistence.cpp`
- `core/src/colonization_runtime.cpp`
- `core/src/construction_projects.cpp`
- `core/src/exploration_advance.cpp`
- `core/src/fleet_persistence.cpp`
- `core/src/fleet_reach.cpp`
- `core/src/fleet_transit.cpp`
- `core/src/founding_catalog.cpp`
- `core/src/fresh_campaign.cpp`
- `core/src/galaxy_generation_metadata.cpp`
- `core/src/galaxy_payload_json.cpp`
- `core/src/nearby_habitable.cpp`
- `core/src/persistable_fresh_campaign.cpp`
- `core/src/planetary_body_persistence.cpp`
- `core/src/planetary_catalog.cpp`
- `core/src/planetary_generation.cpp`
- `core/src/settlement_planning.cpp`
- `core/src/shipbuilding.cpp`
- `core/src/species_environment.cpp`
- `core/src/surface_construction.cpp`
- `export/runtime-config.json`
- `native-tests/native_celestial_appearance_tests.cpp`
- `native-tests/native_inspection_tests.cpp`
- `tools/stellar-export/native_client_runtime.py`
- `tools/stellar-export/test_native_client_runtime.py`
