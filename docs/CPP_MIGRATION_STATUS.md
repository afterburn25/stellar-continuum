# C++ migration status

Branch of record: `engine/stellar-engine-migration` (head `ac45d958`, engine 0.1.57).
Devin/SWE-2 working branch: `cpp/devin-swe2-native-conversion`.
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
| Diplomacy (presentation) | DiplomacyRelationsPresenter, ObserverDiplomacyCommandService | `native_diplomacy_controller`, `native_diplomacy_workspace` | OK | `native_diplomacy_controller`, `native_diplomacy_workspace` | PARTIAL | Observer-safe projection + RELATIONS workspace wired to top bar; playthrough smoke pending |
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

1. Diplomacy workspace exists but has no real-playthrough/Vulkan smoke evidence yet.
2. Surface scene is a construction workspace, not the reference's rendered colony view.
3. No orbital structure rendering.
4. No audio of any kind in the native client/engine.
5. Frame pacing measured ~17–21 ms mean / ~33 ms p95 under smoke — 60 FPS not established.
6. `graphicalParity=false` retained honestly; `cleanMachineTest` needs a separate machine/VM.

## Current state (engine 0.1.58, working branch `cpp/devin-swe2-native-conversion`)

- 150/150 graphical CTest (incl. `native_diplomacy_controller`, `native_diplomacy_workspace`),
  144/144 headless CTest baseline, all Python export checks.
- Sealed export `StellarContinuum-windows-native-preview-410753da-20260915T042104279069Z`:
  113 files, 39 MB ZIP, every relocated/Vulkan smoke flag true, sourceDirty=false.
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
- No merge to integration/main; PR #325 remains draft coordination point.
