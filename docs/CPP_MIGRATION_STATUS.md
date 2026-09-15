# C++ migration status

Branch of record: `engine/stellar-engine-migration` (head `ac45d958`, engine 0.1.57).
Devin/SWE-2 working branch: `cpp/devin-swe2-native-conversion` (reviewed head
`07d989df`).
Codex working branch: `cpp/codex-native-architecture-integration`, based on `ac45d958`.
Reference: Godot 4.7.2 / C# / .NET 8 under `src/`, retained as behavioral and visual truth.

Evidence basis: states below cite real artifacts — `core/` parity CTests (`*_parity`,
145+ cases), `app/native_client/` presentation modules, `tools/stellar-export/` sealed
validators, and `docs/engine/*_VALIDATION.md` contracts. "PARITY VERIFIED" means the
subsystem has a maintained parity/validation gate that runs in the sealed export.

## Current integration checkpoint (2026-09-15)

- Devin/SWE-2 was reviewed through `07d989df`. Territory presentation commit
  `ef7a4007` was selected: it ports the C# projection's observer-gated anchors,
  smoothed continuous fill geometry, stitched contours, fog mask, unexplored
  dimming and observer-visible claim outlines into a bounded native presentation
  cache. Core behavior and the Player17 schema/payload are unchanged.
- Projection now runs from an observer-filtered DTO on one Engine `JobSystem`
  worker. At most one request is admitted; newer input coalesces. Owner polling
  accepts only the matching generation, fingerprint and clear epoch. Current
  terminal errors report once without retry, and stale results/errors are dropped.
  Snapshot requests are capped at 2 Hz and 2,500 systems, 4,096 visible anchors,
  4,096 claims and 256-byte names. The fill atlas plus fog remains below 2.1 MiB.
- Synchronous cold projection measured 179/347/591/3,915 ms for workloads
  500 systems/3 empires/6 colonies, 500/3/100, 2,500/6/30 and 2,500/6/500.
  Corresponding final owner requests were 0.0937/0.0948/0.4770/0.4718 ms;
  unchanged polls were 0.0432/0.0889/0.4609/0.4915 ms; scheduling-inclusive
  worker completion was 392/479/609/4,985 ms. This is responsiveness/latency
  evidence, not a sustained FPS result.
- The final full-native MSVC build passed. Five focused CTests passed in 1.79 seconds
  (`native_diplomacy_controller`, `native_diplomacy_workspace`,
  `native_galaxy_backdrop`, `native_galaxy_star_markers`,
  `native_territory_projection`). All 95 focused checks passed in 5.778 seconds
  across
  `test_native_diplomacy_runtime`, `test_native_galaxy_runtime` and
  `test_native_client_runtime`; these include 26 diplomacy checks and two
  duplicated-map sidecar rejection cases. Logs:
  `work/native-territory-visibility-runtime.log` and
  `work/native-territory-final-python.log`. Published head `c4744e1c` passed CI
  run `34978964654`; the updated
  GPU-free job compiles the full native app plus territory, star-marker and
  diplomacy-workspace targets. Exact-head CI is pending.
- A closed RELATIONS workspace had retained its view and rendered hidden portrait
  work. Close intentionally retains its view for reopening; visibility-gated
  rendering skips all hidden draw/provider work, and regression coverage verifies
  a clean reopen. Relations Escape/Close
  also cancels the stale map gesture, so the next wheel input reaches map zoom.
- Two repaired Vulkan diplomacy runs passed at acceptance 720p and paused reload
  1080p. Both inspected map BMPs show the rounded cyan home border, purple dashed
  foreign claim, original nebula and no hidden RELATIONS panel. Each reports one
  region/claim, 14 contour and 36 claim draws, one fill atlas, one fog image,
  1,998,656 cached bytes and 19 unknown systems. Acceptance preserved unrelated
  state and claims; paused reload matched the whole Player17 payload and the source
  fixture hash remained unchanged. Evidence:
  `work/native-territory-diplomacy-evidence.json`.
- The two existing 500-system galaxy runs remain valid, making four territory
  runtime runs in total; overview and regional images were inspected. Regional
  fill remains faint and nearby labels overlap. This is polish debt, not graphical
  parity or a sustained-60-FPS claim. The candidate remains unmerged and unreleased.
- Orbital commit `eb1ab876` was not imported. Its project/state projection follows
  the reference schematic, but its most-populous-colony host and screen position
  are invented presentation values, not persisted physical locations. Input
  overlap, synchronous rasterization and cache/thread issues also need redesign.
  Its geometry is suitable only for an explicitly labeled construction preview
  until Core/save data defines physical orbital sites.

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
| Diplomacy (presentation) | DiplomacyRelationsPresenter, ObserverDiplomacyCommandService | `native_diplomacy_controller`, `native_diplomacy_workspace` | OK | focused C++/Python checks + Vulkan smoke | PARTIAL | Observer-safe RELATIONS workspace covers the reference sections; strategic claims are map presentation |
| Territory / exploration | Exploration*, StrategicTerritoryProjection | `core/exploration_*`, `survey_operations`, `knowledge`, `native_territory_projection`, `native_territory_overlay` | OK | exploration/knowledge parity + focused territory checks | PARTIAL | Survey secrecy preserved; reference-shaped native overlay selected, Vulkan/performance proof pending |
| Strategic AI | CivilizationStrategic* | `core/strategic_*` (6 modules) | OK | `strategic_*_parity` | PARITY VERIFIED | Scheduled reviews, bounded work |
| Fleets | Fleet*, FleetTransit | `core/fleet_*`, `fleet_transit`, `fleet_reach` | OK | `fleet_*_parity` | PARITY VERIFIED | `design_id` now in native presentation |
| Combat | Combat*, MassiveCombat* | `core/combat_*`, `massive_combat_*`, `campaign_massive_combat` | OK | `combat_*_parity`, `massive_combat_*` | PARITY VERIFIED | Engine resolution; native battle view not started |
| Events | none in C# | none | — | — | N/A | No event subsystem exists in reference |
| Save/Load | Game/Persistence | `core/player_campaign_*`, `galaxy_payload_*`, `*_persistence` | OK | `player_campaign_*` parity + reload validators | PARITY VERIFIED | Player17 format; paused reload equality |
| Time simulation | SimulationClock, GalaxySimulationStepCoordinator | `core/campaign_frame`, `strategic_clock`, `campaign_coordinator` | OK | `campaign_frame_parity`, `strategic_clock_parity` | PARITY VERIFIED | Deterministic stepping |
| UI (native) | Main.*, panels | `app/native_client/*_workspace` (16+ modules) | OK | workspace + input tests + smoke validators | PARTIAL | Fleet/shipyard/research/construction/colony/surface/settlement/system/startup/diplomacy workspaces done |
| Rendering (native) | Main.VisualMap, renderers | `engine/native_map_platform`, `app/native_client` scene | OK | Vulkan smoke + capture validators | PARTIAL | Galaxy art, star markers, ships, route effects, textured surface ground and selected territory overlay; detailed colony buildings/roads remain, physical orbital sites deferred |
| Audio | AudioDirector | `engine/native_audio`, `app/native_client/native_audio_director` | OK | audio/director CTest + relocated audio startup/reload | PARTIAL | Score, settings, completion cues and bounded fixed scientist speech; broader casting/device recovery remain |
| Input | Main.PlayerCommands, input actions | `native_client_input`, `map_interaction` | OK | input tests | PARTIAL | Map/fleet/confirm flows done |
| Assets | asset library | `assets/` + exact-hash declarations | OK | packaging rejection tests | PARITY VERIFIED | Explicit reviewed manifests only |
| Voice | Main.Voice*, CharacterVoiceResolver | `native_audio_director` / fixed PCM cues | OK | finite channel/director checks + connected-lane smoke | PARTIAL | Three dry British human scientist lines. Legacy optional TTS is C#/Godot; no native dynamic TTS or alien casting yet |
| Packaging | export tooling | `tools/stellar-export`, `export/*.json`, `cmake/Native*` | OK | sealed export validators | PARITY VERIFIED | No Godot/.NET/compiler at runtime |

## What blocks "fully playable native"

1. Surface construction now has textured ground with bounded asynchronous loading. Detailed colony buildings, roads, environment-specific scenery and full 3D remain.
2. Physical orbital sites need an authoritative Core/save host and location contract. A labeled construction schematic can be ported without claiming physical placement.
3. The human scientist now has three fixed British cues, and owned simulation events produce bounded notices/sounds. Full character/species casting, dynamic speech and playback-device recovery remain.
4. The selected territory overlay still needs actual Vulkan/runtime and performance validation. Existing background-art measurements do not establish its cost or complete graphical parity.
5. Background artwork reduces first-scene CPU work to about 3 ms in both Sol and the galaxy; regional scenery transitions now cost about 1 ms. Cold profiling locates the ~67 ms tail inside presentation, even with no image uploads; its precise driver/display cause remains unproven. A developed 500-system campaign with 24 paid ships and nine total colonies averages ~16.7 ms on this host at 8X, including manual saves and exact paused reloads. Combat, much larger fleets and broad-hardware 60 FPS remain unproven.
6. `graphicalParity=false` retained honestly; `cleanMachineTest` needs a separate machine/VM.

## Upstream evidence (engine 0.1.58, Devin branch `cpp/devin-swe2-native-conversion`)

Latest Codex checkpoint fixes a real developed-campaign save failure: Player17
research outcomes were written as string enum names but its strict reader expects
integers. The writer now uses typed values at the seven affected outcome, tacit
asset and foreign-assessment fields. The reader, standalone research codecs and
simulation rules are unchanged. Both failed-hypothesis and successful progression
tests now restore and recapture the complete payload with no fields omitted.

The optional developed fixture pays for 24 ships through ordinary research and
shipyard commands, then issues real routes on day 10154. Four actual Vulkan
active/paused runs and two default system runs passed at 720p/1080p. All 24 fleets
move in each measured interval; they have arrived by the final 1080p save. Interval
means are 16.713/16.714 ms; p95 17.401/17.604 ms. Eight focused CTests and 17 Python
checks pass. See NATIVE_FRESH_PROGRESSION.md and NATIVE_CLIENT_VALIDATION.md for
offline preparation, full workload, source hash and timing limits. Previous head
`a56351f8` passed native CI `34960835811`, covering this repair and developed-fleet checkpoint. Audio foundation `6e44bb40` subsequently passed native CI `34968941076`, including both dedicated audio targets. Settings head `de65ce7b` passed `34972936916`. Scientist/feedback head `2d21d91c` has its own run, `34976618037`; its result must be checked independently of later surface work.

The preceding Codex checkpoint adds a separate opt-in running-campaign profiler. The
canonical 500-system Player campaign runs 600 measured frames at 8X using real
elapsed time and UI speed/resume/pause input, with a manual save during play.
It proves time advances after that save completes, pauses for the final save,
and checks the whole Player17 payload through an independent paused reload.
Actual 720p/1080p runs advanced day 0 to 80.2483424 to 160.514568; interval means
were 16.718/16.722 ms, p95 16.917/17.113 ms, update maxima 5.941/4.434 ms.
Strict native build, four focused CTests, 88 Python checks, four rejected CLI
cases and six Vulkan launches passed. This is early-campaign evidence, not a
busy-fleet benchmark or a universal 60 FPS claim. Previous cold diagnostic head
`86445dde` passed native CI `34952179710`; the new checkpoint needs its own run.

The preceding Codex checkpoint adds an opt-in ten-frame cold profile to the existing
bounded map profiler. The strict native build, 57 Python checks, four 600-frame
Vulkan map profiles and two default diplomacy launches passed. Frame 7 spends
65.471–66.460 ms inside presentation; Sol's submission takes 0.236/0.258 ms and
uploads no images on that frame. Exact paused save/reload and finished-art gates
remain intact. This is diagnostic evidence, not a fixed stall or GPU-only timing.
No normal-play histories or Engine rendering changes were added. See the handoff
for the next active-campaign performance work and the discarded queue experiment.

Committed galaxy checkpoint `63d62d31` prepares deep-field, galaxy and regional scenery on
the existing bounded Engine image worker. Each decoded scenery image is limited
to 8 MiB, with three cache slots; missing/oversized sources retain their path/cause.
Central fog remains visible while scenery prepares, navigation stays responsive,
and capture gates wait for finished imagery. Three focused CTests, 46 Python
checks and six actual Vulkan galaxy/system/diplomacy launches passed. Galaxy cold
scene CPU time fell from 45.432/46.248 ms to 3.163/2.695 ms at 720p/1080p;
regional transition maxima fell from 19.779/19.577 ms to 1.027/0.833 ms. All four
finished galaxy overview/regional BMP captures are byte-identical to the previous
build. This checkpoint requires its own CI; previous system-art head `96b83092`
passed native CI `34946960470`. See the handoff and validation document for limits.

The preceding checkpoint prepares system stars, rings and planet discs on one
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
Profiling head `b491783c` passed native CI `34942125331`; system-art head
`96b83092` passed `34946960470`. Engine remains 0.1.58 candidate.

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
