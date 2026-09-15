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
| Diplomacy (presentation) | DiplomacyWorkspace*, Main.Diplomacy | remote Devin candidate (`native_diplomacy_controller`, `native_diplomacy_workspace`) | pending integration | pending review | NOT IN CURRENT CHECKOUT | Available on `cpp/devin-swe2-native-conversion` at `06b2b802`; review/integrate rather than re-port |
| Territory / exploration | Exploration* | `core/exploration_*`, `survey_operations`, `knowledge` | OK | `exploration_*_parity`, `knowledge_parity` | PARITY VERIFIED | Survey secrecy preserved |
| Strategic AI | CivilizationStrategic* | `core/strategic_*` (6 modules) | OK | `strategic_*_parity` | PARITY VERIFIED | Scheduled reviews, bounded work |
| Fleets | Fleet*, FleetTransit | `core/fleet_*`, `fleet_transit`, `fleet_reach` | OK | `fleet_*_parity` | PARITY VERIFIED | `design_id` now in native presentation |
| Combat | Combat*, MassiveCombat* | `core/combat_*`, `massive_combat_*`, `campaign_massive_combat` | OK | `combat_*_parity`, `massive_combat_*` | PARITY VERIFIED | Engine resolution; native battle view not started |
| Events | none in C# | none | — | — | N/A | No event subsystem exists in reference |
| Save/Load | Game/Persistence | `core/player_campaign_*`, `galaxy_payload_*`, `*_persistence` | OK | `player_campaign_*` parity + reload validators | PARITY VERIFIED | Player17 format; paused reload equality |
| Time simulation | SimulationClock, GalaxySimulationStepCoordinator | `core/campaign_frame`, `strategic_clock`, `campaign_coordinator` | OK | `campaign_frame_parity`, `strategic_clock_parity` | PARITY VERIFIED | Deterministic stepping |
| UI (native) | Main.*, panels | `app/native_client/*_workspace` (15+ modules) | OK | workspace + input tests + smoke validators | PARTIAL | Fleet/shipyard/research/colony/surface/settlement/system/startup workspaces done; diplomacy panel missing |
| Rendering (native) | Main.VisualMap, renderers | `engine/native_map_platform`, `app/native_client` scene | OK | Vulkan smoke + capture validators | PARTIAL | Galaxy art, star markers, ship art, route effects done; no surface/orbital scene art |
| Audio | AudioDirector, voice | none | — | — | NOT STARTED | Engine has no audio module |
| Input | Main.PlayerCommands, input actions | `native_client_input`, `map_interaction` | OK | input tests | PARTIAL | Map/fleet/confirm flows done |
| Assets | asset library | `assets/` + exact-hash declarations | OK | packaging rejection tests | PARITY VERIFIED | Explicit reviewed manifests only |
| Voice | Main.Voice*, CharacterVoiceResolver | `work/voice-engine-tts` (merged) | OK | worker regressions | PARTIAL | Engine-side TTS landed upstream; game hooks not wired |
| Packaging | export tooling | `tools/stellar-export`, `export/*.json`, `cmake/Native*` | OK | sealed export validators | PARITY VERIFIED | No Godot/.NET/compiler at runtime |

## What blocks "fully playable native"

1. Review and integrate the existing Devin diplomacy slice; do not duplicate the port. Its current evidence has no real-campaign or graphical diplomacy smoke because Player17 has no contacts, and it still lacks claims/border warnings, a demand/trade composer, and grievance UI.
2. Surface scene is a construction workspace, not the reference's rendered colony view.
3. No orbital structure rendering.
4. No audio of any kind in the native client/engine.
5. Smoke timing is ~20.6–21.2 ms mean / ~33.4–33.7 ms p95 for system/galaxy; render-present includes VSync wait, so 60 FPS is not established.
6. `graphicalParity=false` retained honestly; `cleanMachineTest` needs a separate machine/VM.

## Current state (engine 0.1.57, commit `ac45d958`)

- 148/148 graphical CTest, 144/144 headless CTest, 61 Python export checks.
- Sealed export `windows-native-preview`: relocated launch, restricted PATH, Player17
  save/reload, all subsystem validators.
- Ship artwork: six approved images, bounded 6-entry/4 MiB cache, 224px thumbnails,
  design_id→role resolution, fleet+shipyard rows/details, Vulkan-validated.
- Fleet route effects: dashes, chevrons, trails for active owned fleets matching the
  Godot map; unsurveyed-leg drawing is player-authorized own-fleet data.
- No merge to integration/main; PR #332 is the candidate under review.

## Codex candidate checkpoint (PR #332)

- Fresh-checkout byte stability is covered for reviewed native assets; soft-circle submission now preserves the legacy 20-segment pixels and ordered blending without per-circle heap allocation or trigonometry.
- Native system framing now uses measured labels, body/stellar/orbital envelopes and a 12px presentation inset. Selected labels take priority; lower-priority labels hide rather than overlap. Initial travel activation fits visible exits once, while later refreshes preserve pan/zoom.
- Final evidence: five focused CTests (`native_system_travel`, `native_system_workspace`, `native_system_view`, `native_system_colony_entry`, `native_settlement_workspace`) and seven actual Vulkan galaxy/system/travel launches passed established validators, including exact paused Player17 reload and observer secrecy. See `native-system-layout-tests.log`, `native-system-checkpoint.log`, `work/layout-{galaxy,system,travel}.json`, and `build-native/preview-*.bmp`.
- Smoke-only timing now records bounded update/scene/render-present mean and p95 before JSON diagnostics. System/galaxy means were about 20.6–21.2 ms and p95 about 33.4–33.7 ms; render-present mean about 16.4–16.6 ms includes VSync wait. This is not a GPU-only measurement or a 60 FPS claim; cold-entry versus steady scene/update spikes remain to investigate.
- CI trigger coverage was proven green by native workflow `34927971070` for `21ea21d8` (`21ea21d8338b75d5ec09731c5d71ad341857e57d`). The final layout/timing candidate still needs CI at its exact head; this run does not cover uncommitted changes. No release bump, shared merge, full-suite claim, or clean-machine claim is made.
