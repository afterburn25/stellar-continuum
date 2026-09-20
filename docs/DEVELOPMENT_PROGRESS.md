# Development accomplishments by subsystem

Evidence snapshot: **2026-09-20**, current native integration branch. These
accomplishments describe concrete code now present; earlier implementation
reports remain scoped evidence, not a claim that every related feature is complete.

| Subsystem | Concrete work present | Owners / evidence | Status and unfinished scope |
| --- | --- | --- | --- |
| Engine | C++23 foundation, generational handles, clocks, RNG, jobs, event queue, atomic files, spatial/orbit primitives | `engine/`, `foundation`, spatial/scene tests | PARTIALLY IMPLEMENTED as a general engine; no unified World or general scheduler/LOD |
| Galaxy | Seeded measured anchors, six morphologies, five population states, density footprints, spatial indices and 50k-size paths | `galaxy_configuration.cpp`, `stellar_population_profiles.cpp`, scale tests | IMPLEMENTED BUT NEEDS POLISH; generation fingerprint regression remains |
| Stars | 23 population object types, rare classes, central SMBH states, analytic multiple-star hierarchy and zoom art | `stellar_object.cpp`, `stellar_orbits.cpp`, native markers | IMPLEMENTED BUT NEEDS POLISH; no general N-body integration |
| Planets | Canonical `PlanetAppearance`, constrained taxonomy, physical eligibility, accepted/rejected image pools, globe materials and save migration | `planet_appearance.cpp`, taxonomy JSON, native globe/materials | IMPLEMENTED BUT NEEDS POLISH; see open broad graphical smoke assertion |
| Giants / rings | Accepted replacement giant pool, old-identity migration, ring rules and angular-detail material preparation | `planetary_rings.cpp`, `giant_ring_rules`, ring material/GPU tests | IMPLEMENTED BUT NEEDS POLISH; optics/shadows remain approximations |
| Moons / Sol | 18 supplied moon materials, 19 total canonical moons, parent paths, tilted frames, locking, retrograde Triton, Pluto–Charon displacement | `planetary_satellites.cpp`, `native_moons` | IMPLEMENTED BUT NEEDS POLISH; chart scale / mean elements, no dated ephemerides |
| Belts / debris | Nine field types, separate paths, varied solid/elongated rocks, bounded LOD and slow tumble, approximate ice optics | `small_body_*`, native small-body renderer, scene GPU tests | IMPLEMENTED BUT NEEDS POLISH; bounded representatives rather than every rock |
| Phenomena / skies | Versioned phenomena generation and art, sole faint map-reference sky, clear Sol policy | `galaxy_phenomena.cpp`, `system_background.cpp` | IMPLEMENTED BUT NEEDS POLISH; local sky art is curated, not a real star catalog projection |
| Stellar VFX | Shared saved events, surface attachment, paired stages, independent activity clock; restored sharp curved emission after rejected volume approach | `stellar_activity.cpp`, native eruptions, [timing correction](FLARE_SHARPNESS_AND_TIMING.md) | IMPLEMENTED BUT NEEDS POLISH; physical damage/space weather integration unfinished |
| Simulation / AI | Authoritative integrated campaign, observer-safe commands, strategic intent/planning, exploration and fleet intelligence | `integrated_adaptive_campaign*`, `strategic_*`, `exploration_*` | PARTIALLY IMPLEMENTED as full game; long campaign effectiveness and combined late-game load unverified |
| Economy / logistics | Credits/resource flows, biological burden, construction, shipbuilding, route/fuel/freight rules | `campaign_economy`, `colony_economy`, `logistics`, `freight` tests | IMPLEMENTED BUT NEEDS POLISH; not a generic engine resource framework |
| Research | Native catalogs, eligibility, pressure, funding, labs, expertise/readiness, agendas, foreign tech and outcome snapshots | `adaptive_research_*`, extensive parity tests | IMPLEMENTED BUT NEEDS POLISH; advanced design documents may exceed accepted native scope |
| Diplomacy | Persisted state, lifecycle, observer command validation, contact/agreement/history workspace | `diplomacy_*`, native diplomacy tests | IMPLEMENTED BUT NEEDS POLISH |
| Combat | Tactical and massive-combat simulation/persistence, 3D movement, command/readiness and native battle view | `massive_combat_*`, `combat_*`, native battle tests | PARTIALLY IMPLEMENTED as final game combat; full-AI/large-fleet stress not established |
| UI / audio | Native menu/map/system/globe, production/research/fleet/economy/diplomacy/planetary screens, settings, notifications, music/effects/voice | `app/native_client/`, native UI/audio tests | IMPLEMENTED BUT NEEDS POLISH; no generic advanced UI/localization/spatial-audio framework |
| Saves | Player17 capture/restore/recovery, atomic writes/backups, Developer isolation, large-save serialization and checkpoints | `player_campaign_*`, `developer_qa_host`, persistence tests | IMPLEMENTED BUT NEEDS POLISH; incremental saves/full replay unfinished |
| Assets | Actual cooker, mip/codec quality gate, chunk deduplication, checksummed packages, mounted aliases and bounded preparation | `asset_cooker.cpp`, `texture_cook.cpp`, `asset_registry.cpp` | IMPLEMENTED BUT NEEDS POLISH; distribution size and full streaming unfinished |
| Build / release | Native executables, cooker, portable stage, per-user setup/update/repair/recovery/uninstall and version manifests | `installer/`, `tools/build-*-installer.ps1`, maintenance tests | IMPLEMENTED BUT NEEDS POLISH; dev unsigned/offline; native CI stale |
| QA / diagnostics | Developer content indices, revealed exploration, independent full-research switches, AI control, 25x developer speed, logs/checkpoints/bundles, local fault reports/dumps | `DEVELOPER_QA.md`, `runtime_diagnostics.cpp` | PARTIALLY IMPLEMENTED as a full developer toolset; profiler/editor/replay incomplete |

## Recent repository history versus local work

The audit began at `36ac3205`, nine commits ahead of the fetched tracking branch.
Those existing commits cover native population/art integration, earned gameplay
validation, HUD/navigation, compact fleet lists and settings/window behavior.
The later celestial/cooking/maintenance work existed in the working tree and was
not already on GitHub. This synchronization publishes that current source and
tests as an explicit snapshot commit, not fabricated backdated milestones.

The preceding zoom-crash fix corrected a real retained-mip allocation reservation
failure, retained artwork quality and added local session/fault reporting.
Its targeted cooked/installed replay passed; a separate broad developer smoke
assertion remained open. See [crash investigation](STAR_MAP_ZOOM_CRASH_FIX_20260920.md).

The handoff task adds current docs, Git LFS coverage for required artwork and a
small compiler/link configuration corrections. It does not claim to implement the
future 30-part engine expansion or to finish outstanding gameplay features.
