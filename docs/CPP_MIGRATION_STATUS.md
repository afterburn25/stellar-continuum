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
| Diplomacy (presentation) | DiplomacyRelationsPresenter, ObserverDiplomacyCommandService | `native_diplomacy_controller`, `native_diplomacy_workspace` | OK | `native_diplomacy_controller`, `native_diplomacy_workspace`, `--diplomacy-smoke` validator | PARITY VERIFIED | Observer-safe projection + RELATIONS workspace; sealed export drives a real command + redaction check |
| Territory / exploration | Exploration* | `core/exploration_*`, `survey_operations`, `knowledge` | OK | `exploration_*_parity`, `knowledge_parity` | PARITY VERIFIED | Survey secrecy preserved |
| Strategic AI | CivilizationStrategic* | `core/strategic_*` (6 modules) | OK | `strategic_*_parity` | PARITY VERIFIED | Scheduled reviews, bounded work |
| Fleets | Fleet*, FleetTransit | `core/fleet_*`, `fleet_transit`, `fleet_reach` | OK | `fleet_*_parity` | PARITY VERIFIED | `design_id` now in native presentation |
| Combat | Combat*, MassiveCombat* | `core/combat_*`, `massive_combat_*`, `campaign_massive_combat`, `native_battle_workspace` | OK | `combat_*_parity`, `massive_combat_*`, `native_battle_workspace`, `--battle-smoke` validator | PARITY VERIFIED | Engine resolution + observer-filtered tactical presentation, orders, secrecy |
| Events | none in C# | none | — | — | N/A | No event subsystem exists in reference |
| Save/Load | Game/Persistence | `core/player_campaign_*`, `galaxy_payload_*`, `*_persistence` | OK | `player_campaign_*` parity + reload validators | PARITY VERIFIED | Player17 format; paused reload equality |
| Time simulation | SimulationClock, GalaxySimulationStepCoordinator | `core/campaign_frame`, `strategic_clock`, `campaign_coordinator` | OK | `campaign_frame_parity`, `strategic_clock_parity` | PARITY VERIFIED | Deterministic stepping |
| UI (native) | Main.*, panels | `app/native_client/*_workspace` (18+ modules) | OK | workspace + input tests + smoke validators | PARTIAL | Fleet/shipyard/research/construction/colony/surface/settlement/system/startup/diplomacy/battle workspaces + recent-events notification feed + selected-system inspection card (survey status/progress, guidance, intel facts, observer-safe colony intel; reference `Main.Inspection`/`SystemInspectionPanel`) + toggleable SUPPLY NETWORK panel (home-system logistics metrics, guidance, node rows; reference `LogisticsNetworkPanel`/`Main.Logistics`) + civilian fleet recovery controls (hold/resume/return-to-base with paid-commitment confirmation; reference `UiToggleSelectedCivilianFleetHold`/`UiRequestSelectedCivilianReturnToBase`) + empire overview in the fleet panel detail area (knowledge-gated selected system, home distance, colony quick-open into the orbital view, combined power; reference `EmpireOverviewPanel`) + toggleable MISSIONS & SETTLEMENT board (top-rail MISSIONS button; owned scout/science/colony cards with phase, destination, ETA and the ported `ExplorationMissionStatus` summary text; reference `ExplorationMissionPanel` missions tab) + its Colony Sites tab (bounded site browser over the settlement-mission controller, ship/site navigation, detail text, select-ship-on-map, owned-colony View actions) done |
| Rendering (native) | Main.VisualMap, renderers | `engine/native_map_platform`, `app/native_client` scene | OK | Vulkan smoke + capture validators | PARTIAL | Galaxy art, star markers, ship art, route effects, strategic territory overlay (fills, contours, labels, fog, claim arcs, unexplored dimming), orbital construction markers + software-rasterized staged structures, surface colony scene (hub, per-type building sprites, construction phases, roads, ghost previews) done |
| Audio | AudioDirector, voice | `native_audio*` mixer + SDL3 stream device | OK | `native_audio` + `native_audio_settings` CTests + `--audio-smoke` validator | PARTIAL | Music loop + 6 SFX + hover/confirm + event routing + persistent volumes + duck ramp + settings UI (pause-menu AUDIO button, sliders, defaults, persisted) + dedicated dialogue voice with live ducking done |
| Input | Main.PlayerCommands, input actions | `native_client_input`, `map_interaction`, `native_support` | OK | input tests | PARTIAL | Map/fleet/confirm flows + keyboard shortcuts (Space, 1-4/1-5, F fit, F6 save, T/R/C/B candidates, N mid-session new campaign, F8 support bundle) + hover fleet route preview (reference `UiFleetDestinationPreview`) done; menu NEW GAME row wired to the startup sandbox |
| Assets | asset library | `assets/` + exact-hash declarations | OK | packaging rejection tests | PARITY VERIFIED | Explicit reviewed manifests only |
| Voice | Main.Voice*, CharacterVoiceResolver | `native_voice*` + Windows SAPI | OK | `native_voice` + `native_voice_settings` CTests + `--audio-smoke` voice fields | PARTIAL | Catalogue/profiles/roles, observer-safe router, queue/dedupe/cooldown/interrupt playback, roster character resolution (own leadership offices only — `ResolveCurrentVoiceCharacter` parity), SAPI 5 synthesis, WAV cache, captions, gameplay bridge, settings UI (pause-menu VOICE button: toggles, sliders, size/frequency cycles, Replay/Stop, persisted) done; offline-neural backend remains |
| Packaging | export tooling | `tools/stellar-export`, `export/*.json`, `cmake/Native*` | OK | sealed export validators | PARITY VERIFIED | No Godot/.NET/compiler at runtime |

## What blocks "fully playable native"

1. Surface scene renders hub/buildings/roads/ghosts as rasterized sprites, but
   the reference's free camera orbit, terrain relief and settlement overlays
   remain (workspace is a fixed top-down construction view).
2. Voice playback is wired end to end (catalogue → router → playback → mixer
   dialogue voice → captions) over the Windows SAPI 5 backend, with the voice
   & subtitles settings view live under the pause menu's VOICE button; the
   reference's offline-neural backend remains.
3. Frame pacing is present-bound on the measurement host: smoke now reports
   `cpu_mean/p95` (update+scene build ≈ 2.3 ms mean / 0.14 ms p95) separately
   from `draw_mean/p95` (≈ 18.9 ms — the vsync interval of the ~53 Hz Meta
   Virtual Monitor the host displays through). CPU headroom for 60 FPS is
   established; a native-refresh measurement needs a physical 60 Hz display.
4. `graphicalParity=false` retained honestly; `cleanMachineTest` needs a separate machine/VM.

## Current state (engine 0.1.58, working branch `cpp/devin-swe2-native-conversion`)

- 159/159 graphical CTest (incl. `native_diplomacy_*`, `native_territory_projection`,
  `native_orbital_structure`, `native_surface_scene`, `native_audio`,
  `native_audio_settings`, `native_battle_workspace`, `native_notifications`,
  `native_support`, `native_voice`,
  extended `native_system_view`/`native_system_workspace`/`native_surface_workspace`/
  `native_ui_layout`),
  144/144 headless CTest baseline, all Python export checks.
- Notification feed (`native_notifications` + session harvest): ports the
  reference `PlayerNotificationFeed`/`NotificationCenter`/`PlayerControls`
  surface — bounded 32-item session history, newest-first 16-card
  "RECENT EVENTS" panel, category palette, unread badge (gold/"99+") on a new
  top-bar button, X/Escape dismissal, and the card-level OPEN RELATIONS
  shortcut that focuses the counterpart in the relations workspace
  (`select_contact_civilization` ⇔ `UiOpenDiplomaticContact`). The session
  publishes step events the reference publishes (research, construction,
  ships, exploration, colony, player-involved combat, the funding transition)
  plus accepted command outcomes, and additionally harvests observer-filtered
  diplomatic bulletins (`proposal_*`, `agreement_*`, `war_declared`,
  `contact_established`, `communication_available`) — completing the
  reference's dormant `DiplomaticContactId`/`OPEN RELATIONS` path. Diplomatic
  bulletins carry a contact id only when the counterpart is an identified
  contact; the audience-gated `recent_events` view keeps unidentified
  identities out of the feed. Retained history is seeded as seen on
  load/activation so only live events publish. `--notification-smoke` +
  `native_notification_runtime.py` (13 mock tests) assert panel, unread
  badge, contact focus and no history flood across a reload.
- Keyboard candidate shortcuts: T/R and C/B now port the reference
  `UiCycleResearchCandidate`/`UiStartResearchCandidate` and construction
  equivalents — each press cycles or starts a currently-startable project
  reported by the research/construction controllers (view ordering rather
  than the reference's plan-ranked ordering) and reports through a new
  `SessionNoticeKind::Status` line (`publish_status` ⇔ `SetStatus`). The
  active-project guards match the reference ("Complete the current
  construction project…"). `--research-smoke`/`--construction-smoke` emit
  `shortcut=1` evidence; both export validators require it.
- Mid-session New Game: `N` and the pause-menu NEW GAME row port the reference
  `UiNewCampaign` — the live campaign is saved first (the outer loop waits for
  the Saved notice), then the full startup sandbox entry runs again
  (species/size/seed → Create → generation). Cancelling setup hands the saved
  session back and resumes the campaign. `--new-game-restart-smoke` drives N,
  the mid-session save, the automated second startup, and a second-campaign
  save; `native_new_game_runtime` validates the `new_game_restart` diagnostic,
  the re-saved prior campaign, the isolated `…-native-N` slot (seed 143251),
  and all three restart captures.
- Support bundle (`native_support`): ports `SupportLogger` — a per-session
  `game-<id>.log`/`system-<id>.txt` under `<save>/logs`, and a SUPPORT BUNDLE
  menu button plus F8 that export `support/support-<id>-<ts>.zip` as a
  store-format ZIP (session log + system info + campaign save), reporting
  the path through the status line. `--audio-smoke` exercises both entry
  points (`support=1`); the audio validator requires the flag and opens the
  bundle with a real ZIP reader. `native_support` tests verify CRC32s,
  central-directory structure and entry contents. The smoke now lands the
  save before exporting so every bundle is a three-entry ZIP.
- Voice (`native_voice*`): ports the reference voice presentation layer —
  `NativeVoiceProfileRegistry`/`NativeCharacterVoiceResolver` load the
  reviewed `Data/voice_profiles/{events,human,roles}.json` catalogue (SHA-256
  gated by `export/native-voice-assets.json` + `cmake/NativeVoiceAssets.cmake`),
  `NativeVoiceRouter` ports `VoiceEventRouter` (authorization, dedupe, once,
  cooldown, frequency gate, deterministic variant selection, template render),
  `NativeVoicePlayback` ports `VoicePlaybackController` (8-deep queue,
  2-per-category, priority interrupts, 15 s dedupe, 40 s expiry, subtitle
  fallback), and `NativeGameplayVoiceBridge` ports `GameplayVoiceEventBridge`
  observing only player-authorized frame results. Windows SAPI 5 synthesis
  runs on a dedicated STA worker at 22.05 kHz 16-bit mono into a hashed WAV
  cache; decoded lines play through the mixer's dedicated dialogue voice with
  live ducking, and captions render bottom-center (hidden while the menu or
  relations workspace is open). `--audio-smoke` reports `voice_pipeline`,
  `voice_backend` and `voice_lines` evidence. `native_voice` tests cover the
  router, resolver, queue semantics, cache validation and subtitle fallback
  with a fake backend.
- Tactical battle presentation (`357872e8`): `native_battle_workspace` ports the
  reference `MassiveCombatView` — full-screen observer-filtered formation tokens
  (bounded 4096-token pool, zoom-dependent sampling), selection + box-select +
  hover, right-drag pan / wheel zoom / FIT, tactical pause/speed chrome, the
  nine-order bar (Hold/Defend/Advance/FocusFire/FlankL/FlankR/Intercept/
  BreakContact/Retreat) with a targeting pick state, context engage/advance on
  right-click, combat-event beam/volley/salvo effects and an event feed.
  `CampaignFrame` exposes `begin_tactical`, `issue_tactical_order` and
  `tactical_snapshot`; the frame loop detects `active_combat_encounter`, opens
  the workspace and routes its commands through `execute_battle`. The fleet
  workspace gains an ENGAGE affordance on armed fleets. Secrecy is preserved:
  the workspace renders only the observer snapshot — foreign formations carry
  inexact ship-count ranges, hidden cohort/vessel detail and
  "Unidentified formation" labels when confidence is low; unengaged hostile
  formations stay out of the snapshot entirely. Manual saves now work after
  tactical frames (matching the reference's direct runtime capture), so
  mid-battle state persists and reload re-enters the encounter. Chrome
  clicks no longer clear selection on release, and a release over an armed
  targeted-order button keeps the pick state. `--battle-smoke` authors a
  two-front encounter from the Player17 fixture (`tools/author_battle_save.py`),
  drives selection + a real order + pause/resume, and the export validator
  (`native_battle_runtime.py`, 18 mock tests) asserts redaction, order
  acceptance, capture variance and encounter persistence across reload.
- Surface colony scene (`58aaf475`): the orbital rasterizer core moved to
  `native_scene_raster.hpp`; `native_surface_scene` ports the
  `SurfaceBuildingVisuals` silhouette grammar — per-type cylinder/box/sphere
  meshes, foundation pads, three construction phases with scaffolding,
  powered/offline/prioritized/capital/outpost markers, `advanced_` second tier —
  into bounded cached sprites. The workspace draws the hub, south-to-north
  building sprites with progress bars, civic ring road + site connectors, and
  translucent placement ghosts; `scene_sprites` is asserted by the surface
  export validator (mock suite extended).
- Audio (`dbf07f81`): `native_audio` mixer — pure-CPU 48 kHz stereo, looping
  `claimed-by-the-void-loop.mp3` via pinned `dr_mp3` (MIT-0), bounded 16/24/32-bit
  WAV decoder, 8-voice SFX polyphony, hover/confirm + event-category routing,
  voice-duck ramp, persisted master/music/SFX volumes beside the campaign save.
  `native_audio_device` opens an SDL3 stream only after every required stream
  decodes; audio failure cannot fail the campaign. `--audio-smoke` reports
  decode/voice/bounds evidence; packaging is exact-hash gated (8 files incl.
  `Licenses/dr_mp3-MIT-0.txt`) with an 18-test mock validator.
- Audio settings UI (`d108435a`): pause-menu AUDIO button opens
  `native_audio_settings` — master/music/SFX sliders apply live via
  `apply_volumes`, DEFAULTS restores reference levels, Done/Escape persists to
  `audio-settings.json` beside the save; drags no longer write per pointer
  move. `--audio-smoke` exercises menu->AUDIO->drag->defaults->Done and the
  export validator now requires the `settings` flag plus a persisted settings
  file with values in [0,1]. `native_ui_layout` grew the menu panel (recentered
  to stay in-viewport at 640x360) with updated layout tests.
- Sealed export `StellarContinuum-windows-native-preview-7a04c0bc-20260915T124212249481Z`:
  118 files, 48 MB ZIP, every relocated/Vulkan smoke flag true, sourceDirty=false,
  150/150 CTest, 16/16 diplomacy validator Python tests. The `--diplomacy-smoke`
  validator ran in the seal: `nativeDiplomacyContacts`, `nativeDiplomacyObserverRedaction`,
  `nativeDiplomacyCommand`, `nativeDiplomacyReload` all true with four BMP captures
  (ordered/proposals × fresh/paused-reload). The run proved the sealed package decodes
  the communications portraits (`portrait=1`, real command `command_accepted=1`,
  unidentified-contact redaction verified).
- Sealed export `StellarContinuum-windows-native-preview-e390b659-20260915T182516174478Z`:
  every flag true — `nativeBattleWorkspace`/`ObserverRedaction`/`Order`/`Reload`
  (tick 20→30), `nativeAudioStreams`/`Device` with `settings=1`, diplomacy,
  ship art, surface, colony, settlement, galaxy-art validators. Instrumented
  smokes show `cpu_mean_ms` 1.2–5.0 / `cpu_p95` <0.5 across all scenes against
  a ~18.9 ms present interval — frame pacing is present-bound on the
  measurement host's ~53 Hz Meta Virtual Monitor, not CPU-bound.
- Prior sealed export `StellarContinuum-windows-native-preview-410753da-20260915T042104279069Z`
  (113 files) remains the baseline for the pre-diplomacy slice.
- Orbital construction presentation (`eb1ab876`): home-system infrastructure markers
  with leader lines, state colors, progress arcs and reference short labels; active and
  complete projects draw the same staged primitive geometry as
  `OrbitalStructureGeometry` via a bounded software rasterizer
  (`native_orbital_structure`, 160px textures, ≤16-entry cache); markers focus the
  system inspector with a staged preview and route to the construction workspace.
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
