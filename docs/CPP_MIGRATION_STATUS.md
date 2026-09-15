# C++ migration status

Branch of record: `engine/stellar-engine-migration` (head `ac45d958`, engine 0.1.57).
Devin/SWE-2 working branch: `cpp/devin-swe2-native-conversion`.
Codex working branch: `cpp/codex-native-architecture-integration`, based on `ac45d958`.
Reference: Godot 4.7.2 / C# / .NET 8 under `src/`, retained as behavioral and visual truth.

Evidence basis: states below cite real artifacts — `core/` parity CTests (`*_parity`,
145+ cases), `app/native_client/` presentation modules, `tools/stellar-export/` sealed
validators, and `docs/engine/*_VALIDATION.md` contracts. "PARITY VERIFIED" means the
subsystem has a maintained parity/validation gate that runs in the sealed export.

## Subsystem matrix

| Subsystem | C# source | C++ target | Build | Tests | Parity | Notes |
|---|---|---|---|---|---|---|
| Core primitives / deterministic utilities | Simulation/* | `core/legacy_random`, `interstellar_distance`, `atomic_file_write` | OK | parity tests | PARITY VERIFIED | Seeded RNG preserved |
| Galaxy generation | GalaxyGenerator | `core/galaxy_catalog`, `galaxy_generation_metadata` | OK | `galaxy_catalog_parity` | PARITY VERIFIED | Up to 5000-system benchmarks in export |
| Stars / star systems | StarSystemState, NearbyStarCatalog | `core/galaxy_catalog` | OK | `sol_catalog`, `galaxy_catalog_parity` | PARITY VERIFIED | Hidden core preserved |
| Planetary bodies | PlanetaryBodyGenerator/State | `core/planetary_catalog`, `planetary_body_persistence` | OK | `planetary_body_persistence_parity` | PARITY VERIFIED | Physical model, not enum-only |
| Species | Species* (~15 files) | `core/species_environment` + catalog seeding | OK | `species_parity`, `colony_biology_parity` | PARITY VERIFIED | Species-relative habitability preserved |
| Civilizations | Civilization* | `core/civilization_catalog`, `fresh_campaign` | OK | `civilization_parity`, `fresh_campaign_parity` | PARITY VERIFIED | |
| Leadership | CivilizationLeadershipState | `core/campaign_foundation_persistence` | OK | persistence parity | PARITY VERIFIED | |
| Population / colonies | Colony*, Population* | `core/colony_*`, `colony_economy`, `colony_operations` | OK | `colony_economy_parity` etc. | PARITY VERIFIED | |
| Economy / resources | EconomySimulation, treasury | `core/campaign_economy`, `sovereign_currency`, `colony_economy` | OK | `campaign_economy_parity` | PARITY VERIFIED | |
| Logistics / freight | LogisticsFlowAllocator, FreightSimulation | `core/logistics_*`, `freight` | OK | `freight_parity`, `logistics_*_parity` | PARITY VERIFIED | |
| Construction | Construction* | `core/construction_*`, `surface_construction` | OK | `construction_*_parity` | PARITY VERIFIED | |
| Industry | IndustryAllocation | `core/industry_allocation` | OK | `industry_allocation_parity` | PARITY VERIFIED | |
| Ship design | ShipDesignRegistry | `core/ship_designs` | OK | `ship_designs_parity` | PARITY VERIFIED | |
| Shipbuilding | Shipbuilding*, Shipyard* | `core/shipbuilding`, `shipyard_*` | OK | `shipbuilding_parity`, `shipyard_*` | PARITY VERIFIED | |
| Legacy research | Technology* | `core/legacy_research`, `legacy_technology` | OK | `legacy_*_parity` | PARITY VERIFIED | Superseded path, kept for saves |
| Adaptive Research | AdaptiveResearch* (~40 files) | `core/adaptive_research_*` (~25 modules) | OK | 10+ adaptive parity tests | PARITY VERIFIED | Authoritative research; integrated host |
| Diplomacy (simulation) | Diplomacy*, Diplomatic* | `core/diplomacy_*` | OK | diplomacy parity/persistence tests | PARITY VERIFIED | Observer-safe commands preserved |
| Diplomacy (presentation) | DiplomacyRelationsPresenter, ObserverDiplomacyCommandService | `native_diplomacy_controller`, `native_diplomacy_workspace` | OK | focused C++/Python checks + Vulkan smoke | PARTIAL | Observer-safe RELATIONS workspace; claims/composer/grievance remain partial |
| Territory / exploration | Exploration* | `core/exploration_*`, `survey_operations`, `knowledge` | OK | `exploration_*_parity`, `knowledge_parity` | PARITY VERIFIED | Survey secrecy preserved |
| Strategic AI | CivilizationStrategic* | `core/strategic_*` (6 modules) | OK | `strategic_*_parity` | PARITY VERIFIED | Scheduled reviews, bounded work |
| Fleets | Fleet*, FleetTransit | `core/fleet_*`, `fleet_transit`, `fleet_reach` | OK | `fleet_*_parity` | PARITY VERIFIED | `design_id` now in native presentation |
| Combat | Combat*, MassiveCombat* | `core/combat_*`, `massive_combat_*`, `campaign_massive_combat` | OK | `combat_*_parity`, `massive_combat_*` | PARITY VERIFIED | Engine resolution; native battle view not started |
| Events | none in C# | none | — | — | N/A | No event subsystem exists in reference |
| Save/Load | Game/Persistence | `core/player_campaign_*`, `galaxy_payload_*`, `*_persistence` | OK | `player_campaign_*` parity + reload validators | PARITY VERIFIED | Player17 format; paused reload equality |
| Time simulation | SimulationClock, GalaxySimulationStepCoordinator | `core/campaign_frame`, `strategic_clock`, `campaign_coordinator` | OK | `campaign_frame_parity`, `strategic_clock_parity` | PARITY VERIFIED | Deterministic stepping |
| UI (native) | Main.*, panels | `app/native_client/*_workspace` (16+ modules) | OK | workspace + input tests + smoke validators | PARTIAL | Fleet/shipyard/research/construction/colony/surface/settlement/system/startup/diplomacy workspaces done |
| Rendering (native) | Main.VisualMap, renderers | `engine/native_map_platform`, `app/native_client` scene | OK | Vulkan smoke + capture validators | PARTIAL | Galaxy art, star markers, ship art, route effects done; no surface/orbital scene art |
| Audio | AudioDirector, voice | none | — | — | NOT STARTED | Engine has no audio module |
| Input | Main.PlayerCommands, input actions | `native_client_input`, `map_interaction` | OK | input tests | PARTIAL | Map/fleet/confirm flows done |
| Assets | asset library | `assets/` + exact-hash declarations | OK | packaging rejection tests | PARITY VERIFIED | Explicit reviewed manifests only |
| Voice | Main.Voice*, CharacterVoiceResolver | `work/voice-engine-tts` (merged) | OK | worker regressions | PARTIAL | Engine-side TTS landed upstream; game hooks not wired |
| Packaging | export tooling | `tools/stellar-export`, `export/*.json`, `cmake/Native*` | OK | sealed export validators | PARITY VERIFIED | No Godot/.NET/compiler at runtime |

## What blocks "fully playable native"

1. Diplomacy presentation remains partial: no claims/border warnings, demand/trade composer, or grievance display.
2. Surface scene is a construction workspace, not the reference's rendered colony view.
3. No orbital structure rendering.
4. No audio of any kind in the native client/engine.
5. Background system artwork now reduces measured first-scene CPU work from ~211 ms to 2.8–2.9 ms. Initial galaxy scenery still costs 45–46 ms and regional scenery ~20 ms; upload/presentation tails and busy-campaign performance remain to investigate. Paused warm maps average ~16.7 ms on this host; broad-hardware 60 FPS is unproven.
6. `graphicalParity=false` retained honestly; `cleanMachineTest` needs a separate machine/VM.

## Upstream evidence (engine 0.1.58, Devin branch `cpp/devin-swe2-native-conversion`)

Latest Codex checkpoint prepares system stars, rings and planet discs on one
existing Engine JobSystem worker, using copied observer-safe appearances.
Admission is bounded to 16 outstanding jobs / 32 MiB reserved output; cache
budgets remain unchanged. Generation changes cancel obsolete requests without
waiting, completed results drain on the owner thread, and preparation errors
retain their cause/path. No simulation, GPU or window access moves to workers.
Six focused CTests (including exact synchronous/background pixel comparisons),
a subsequent workspace readiness/input regression, 42 Python export checks and
seven actual Vulkan runs passed. Four 600-frame profiles at 720p/1080p measured
Sol first-scene CPU time 2.936/2.807 ms versus 211.452/211.567 ms; final images
arrive asynchronously in ~213–258 ms. Screenshot gates wait for real artwork.
Paused-map interval means 16.717–16.722 ms and p95 16.913–17.026 ms preserve warm
pacing; see the handoff/validation document for remaining stalls and evidence.
Previous profiling head `b491783c` passed native CI `34942125331`; this new
checkpoint needs its own CI. Engine remains 0.1.58 candidate.

- 150/150 graphical CTest (incl. `native_diplomacy_controller`, `native_diplomacy_workspace`),
  144/144 headless CTest baseline, 61 Python export checks.
- Diplomacy presentation: `native_diplomacy_controller` ports `DiplomacyRelationsPresenter`
  over `DiplomaticStateView` only — unidentified contacts carry no civ id/name/species/
  metrics. Revision+signature stale-command guard covers relationship drift and
  identification changes. `native_diplomacy_workspace` provides the RELATIONS workspace:
  filterable contact directory, species transmission portrait or signal waveform, five
  relationship meters, negotiation/war confirmation modals, agreements/proposals/history/
  intelligence/overview tabs, proposal accept/reject/withdraw buttons, FocusSystem jump.
- Ship artwork: six approved images, bounded 6-entry/4 MiB cache, 224px thumbnails,
  design_id→role resolution, fleet+shipyard rows/details, Vulkan-validated.
- Fleet route effects: dashes, chevrons, trails for active owned fleets matching the
  Godot map; unsurveyed-leg drawing is player-authorized own-fleet data.
- No merge to integration/main; PR #332 is the candidate under review.

## Codex candidate checkpoint (PR #332)

- Cold celestial generation now skips surface calculations that cannot contribute
  to halo pixels, covered corona work and the transparent outer boundary. Exact
  before/after comparison preserves every byte of eight RGBA resources (19.4 MB).
  Star generation improved from ~210 to ~80 ms; full first system scene from
  ~340–346 to ~210–213 ms. Five focused CTests and four final Vulkan system/galaxy
  launches passed. Resource resolution, artwork, colors, flare behavior and observer
  rules remain unchanged. Entry stalls and sustained 60 FPS remain unresolved;
  see the cold celestial checkpoint in the handoff for evidence and next steps.
- Integrated Devin's diplomacy code `410753da` as `c9338699` and reviewed the real campaign path. Contact selection now refreshes immediately, unknown contacts retain stable selection, confirmation commands use their original generation/revision, and proposal terms/precise relationship changes invalidate stale commands. Scroll clipping, intelligence layout, and 720p filter labels were repaired.
- Communications-v2 PNGs now load through canonical species IDs and are explicitly packaged. Four lazy entries are capped at 32 MiB; the actual decoded artwork occupies 24 MiB. Missing declared artwork fails with its path/cause; unknown species retain the signal fallback.
- Final combined validation passed seven focused CTests, 18 diplomacy-validator Python checks, 45 package/checkout Python checks, and six actual Vulkan diplomacy/system/galaxy launches at 720p/1080p. Diplomacy acceptance, the visible new agreement, four known/unknown captures, unrelated-state preservation, and exact paused Player17 reload are covered by the maintained exporter. See `native-diplomacy-final.log` and `work/native-diplomacy-final-{diplomacy,system,galaxy}.json`.
- Final diplomacy frame means were 17.790–17.791 ms and p95 28.367–32.663 ms; system/galaxy means 20.748–21.215 ms and p95 33.458–33.937 ms. This is not a sustained 60 FPS result. Full graphical parity, audio, orbital structures and final surface scenery remain outstanding.
- Fresh-checkout byte stability is covered for reviewed native assets; soft-circle submission now preserves the legacy 20-segment pixels and ordered blending without per-circle heap allocation or trigonometry.
- Native system framing now uses measured labels, body/stellar/orbital envelopes and a 12px presentation inset. Selected labels take priority; lower-priority labels hide rather than overlap. Initial travel activation fits visible exits once, while later refreshes preserve pan/zoom.
- Final evidence: five focused CTests (`native_system_travel`, `native_system_workspace`, `native_system_view`, `native_system_colony_entry`, `native_settlement_workspace`) and seven actual Vulkan galaxy/system/travel launches passed established validators, including exact paused Player17 reload and observer secrecy. See `native-system-layout-tests.log`, `native-system-checkpoint.log`, `work/layout-{galaxy,system,travel}.json`, and `build-native/preview-*.bmp`.
- Smoke-only timing records bounded update/scene/render-present means, p95 and maxima with frame indices. Separate cold-entry, save-service, capture/transition and steady buckets expose loading/save spikes. Render-present includes VSync wait; this is not a GPU-only measurement or a 60 FPS claim. Cold artwork preparation and presentation pacing remain to investigate.
- Async manual-save evidence now covers cold, 61-save-service, capture-transition and steady buckets. System manual-save update fell from 130.901/130.302 ms to 7.272/2.610 ms; galaxy from 130.953/129.773 ms to 5.080/2.955 ms; travel from 32.392–35.940 ms to 1.008–1.157 ms. Cold system scene remains ~340–346 ms; VSync p95 remains ~33 ms. See `work/save-baseline-{system,galaxy,travel}.json`, `work/layout-{system,galaxy,travel}.json`, and `native-save-background-runtime.log`.
- Native manual saving now uses Core's `begin_manual` admission and immutable single-writer persistence. Queued clicks coalesce; failure stays visible before an explicit retry; load/exit drains remain ordered. Nine actual Vulkan map/transit/diplomacy launches passed durable-save, observer and paused-reload validation. See the async-save handoff for interfaces and focused native checks.
- The preceding layout/timing head `66c56b897ad04a99d2e662a2fb089cdcc160186b` passed native workflow `34929897092`; diplomacy head `0e0835ae` passed `34934085259`. The subsequent async-save candidate needs its own CI. Engine 0.1.58 is inherited from Devin's candidate; no new sealed release, shared merge, full-suite or clean-machine claim is made.
