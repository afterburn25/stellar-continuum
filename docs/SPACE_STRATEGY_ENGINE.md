# Stellar Engine — Space Strategy & Simulation Engine

> **Mission:** a deterministic, data-driven C++ engine designed for
> massive space simulations, grand strategy, 4X, colony management,
> economic simulation and civilization-scale games.

This document is the specialization charter for the
`engine/space-strategy-simulation-specialization` workstream. It defines
what the engine optimizes for, what it explicitly deprioritizes, and the
milestone map. Factual status lives in
[ENGINE_CAPABILITIES.md](ENGINE_CAPABILITIES.md); active handoff notes in
[AGENT_HANDOFF.md](AGENT_HANDOFF.md); the numbered engineering roadmap in
[ROADMAP.md](ROADMAP.md).

## Identity

Stellar Engine targets games in the category of Stellaris, Surviving
Mars, space colony simulators, economic strategy games, city builders
and civilization/empire managers. It is **not** a general-purpose
shooter/action engine and does not aim to be Unreal.

The engine is unusually strong at:

- thousands–tens-of-thousands of star systems
- hundreds/thousands of colonies and large populations (cohort models,
  not per-citizen entities)
- many civilizations, huge fleets, long-running economies
- production chains and interplanetary/interstellar logistics
- centuries of accelerated deterministic game time
- procedural astronomical environments

## Non-negotiable principles

1. **Determinism** — simulation outcomes never depend on render
   framerate or wall-clock speed. Fixed/strategic clocks are
   authoritative; save→load→resume preserves state; acceleration changes
   pacing, never results.
2. **Simulation/presentation separation** — rendering is not a second
   simulation. Authoritative state lives in engine/Core systems;
   presentation reads observer-safe state.
3. **Massive scale** — nothing updates every frame by default. Every
   system considers cadence, batching, dirty-state propagation,
   event-driven updates, spatial locality, simulation LOD, background
   processing and aggregate representations.
4. **Data-driven** — resources, buildings, species, recipes, tech,
   events are content, not code. Strict validation: file/record/field/
   reason errors, no duplicate IDs, no dependency cycles.
5. **Reusable services, honest boundaries** — engine supplies mechanics
   and frameworks; Core/game defines specific rules. No game-specific
   monoliths in the engine, and no pretend-generality.
6. **No fake implementation** — a capability is not implemented until a
   real consumer and validation exist.

## Deprioritized directions

Working generic capabilities (2D scenes, tilemaps, platformer-style
movement, raycast picking, basic 3D) remain — they are useful and
reusable — but new effort does not go into FPS mechanics, weapon
handling, humanoid hitboxes, ragdoll, cover shooters, racing vehicles,
melee combos, or increasingly elaborate platformer/shooter systems.
Renderer effort prioritizes space content (planets, atmospheres, rings,
stars, belts, fleets, stations, colony lights, strategic overlays), not
character fidelity.

## Milestone map

| # | Milestone | Status | Notes |
| --- | --- | --- | --- |
| 1 | Simulation scheduler + simulation LOD | PARTIAL → IMPLEMENTED (engine layer) | `SimulationScheduler` (tier cadence) + `SimulationExecutor` (tasks, dependencies, wakeups, budgets, JobSystem waves, per-domain stats). `simulation_scale_250…5000` benchmarks. Core/game adoption is the remaining work. See [SIMULATION_LOD.md](SIMULATION_LOD.md). |
| 2 | Generic resource + economy framework | IMPLEMENTED (engine layer) | `EconomyCatalog`/`EconomyGraph`/`analyze_economy` over `ResourceNetwork`: validated specs, dependency queries, bottleneck diagnostics, `to_runtime_recipe` bridge. Core catalog adoption pending. See [ECONOMY_FRAMEWORK.md](ECONOMY_FRAMEWORK.md). |
| 3 | Population framework | IMPLEMENTED (engine layer) | `Population` cohort model: demographics, mortality/fertility attribution, aging, employment, education migration, explicit emigrant slices; 17M-headcount scale test. Colony consumption pending. See [POPULATION_FRAMEWORK.md](POPULATION_FRAMEWORK.md). |
| 4 | Colony/city framework | IMPLEMENTED (engine layer) | `Colony` districts/structures/utilities: slot-bounded construction, shared utility pools, workforce scaling, upkeep/input draws, condition repair; 1000-colony scale test. Core `Colony` adoption pending. See [COLONY_FRAMEWORK.md](COLONY_FRAMEWORK.md). |
| 5 | Infrastructure networks | IMPLEMENTED (engine layer) | `FlowNetwork`: single-resource directed graphs, per-node supply/demand/storage, capacity edges, lazy union-find component cache on topology dirtying, deterministic greedy transport, unmet/saturation diagnostics. Colony/Core adoption pending. See [INFRASTRUCTURE_FRAMEWORK.md](INFRASTRUCTURE_FRAMEWORK.md). |
| 6 | Strategic logistics | IMPLEMENTED (engine layer) | `LogisticsNetwork`: waypoint nodes, explicit multi-leg `FreightRoute`s with transit days + in-flight capacity, deterministic dispatch queue and (eta,id) deliveries. Core lane/freight adoption pending. See [LOGISTICS_FRAMEWORK.md](LOGISTICS_FRAMEWORK.md). |
| 7 | Planetary development model | IMPLEMENTED (engine layer) | `PlanetEnvironment` adapter struct + `HabitabilityProfile`/`evaluate_habitability`: hard ranges with tolerance margins, water floor, required/forbidden tags → suitability + unmet reasons. Core `PlanetaryBody` adapters pending. See [TERRAFORMING_FRAMEWORK.md](TERRAFORMING_FRAMEWORK.md). |
| 8 | Terraforming framework | IMPLEMENTED (engine layer) | `Terraforming` staged physical projects: linear environment deltas + discrete tag changes at stage completion, cancel keeps applied work, species-relative habitability stays external. See [TERRAFORMING_FRAMEWORK.md](TERRAFORMING_FRAMEWORK.md). |
| 9 | Civilization/empire AI | IMPLEMENTED (engine layer) | `StrategicMind` utility machinery: domain-partitioned actions, caller scorers/commits, hysteresis, cooldowns, min-utility gate, bounded decision journal; cadence via executor tiers. Core faction adoption pending. See [STRATEGIC_AI.md](STRATEGIC_AI.md). |
| 10 | Strategic fleet/warfare | IMPLEMENTED (engine layer) | `WarfareModel`: ShipCohort aggregates (not per-ship entities), fleet orders incl. Interdict zones that gate hostile warp (presence never blocks), Lanchester-style deterministic resolve; 2000-fleet scale test. Core fleet adoption pending. See [WARFARE_FRAMEWORK.md](WARFARE_FRAMEWORK.md). |
| 11 | Space-specific rendering | PARTIAL | Native scene3d GPU path, planet/ring/star materials exist; render-graph consumption, instancing, HDR pending. |
| 12 | Specialized editor tools | PARTIAL | stellar-engine.exe shell with Projects/Scene/Scene3D/Assets/Profiler/Localization; genre tools (galaxy/planet/colony/economy/AI debugger) pending. |
| 13 | Galaxy-scale benchmarks | IMPLEMENTED (engine layer) | `simulation_scale_250…5000` executor benchmarks + `combined_scale`: 400 settlements driving population+colony+power grids+freight+fleets+AI through one executor across mixed tiers — 240 ticks, bit-identical checksums, ~870µs mean tick. |
| 14 | Event/history framework | IMPLEMENTED (engine layer) | `EventHistory` complements `event_bus`/`mission_graph`: recorded strategic events with observer-privacy query projection, significance thresholds, bounded store + pruning, `feed()` news substrate. Core recording points pending. See [HISTORY_FRAMEWORK.md](HISTORY_FRAMEWORK.md). |
| 15 | GNN/public-information hooks | PARTIAL | `EventHistory::feed(observer, since, min_significance)` is the public-information substrate; voice/news presentation layer pending. |

Statuses use PLANNED / PARTIAL / IMPLEMENTED BUT NEEDS POLISH /
IMPLEMENTED — a library with tests and a benchmark but no game consumer
is reported as such, never as a finished feature.
