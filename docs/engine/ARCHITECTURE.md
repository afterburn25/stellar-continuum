# Stellar Engine migration contracts — 0.1.0 foundation

Status: foundation in progress, not full game migration. Normal gameplay expansion is paused until the migration completion gates pass. Baseline: integration `97091aee84b782bdc917185307cd42b97b8fd7d0`, Stellar Continuum 0.1.7 Alpha, tag `migration-baseline/stellar-continuum-0.1.7`. Pending feature branches are preserved separately. Never remove Godot implementation based solely on a compiling native placeholder.

## Layers and directories

| Layer | Directory | Dependencies and responsibility |
| --- | --- | --- |
| Stellar Engine | `engine/include/stellar/engine`, `engine/src` | C++23 standard library first; platform, ticks, identity, jobs, events, logging, coordinates, renderer/asset contracts |
| Stellar Core | `core/include/stellar/core`, `core/src` | Engine; migrated game data/rules only |
| Application | `app` | Core and Engine; headless command line now, original player UI retained until real replacement |
| Native checks | `native-tests` | Maintained CTest programs, old/new fixtures and benchmarks |
| Export tooling | `tools/stellar-export`, `export/stellar-presets.json` | Development-side build/package validation; no Python/CMake/compiler requirement in exported runtime |
| Reference game | existing `src`, `assets`, `data`, `project.godot` | Preserved C#/Godot application and tests throughout parity work |

Engine must not include civilizations, research IDs, colonies or specific missions. Engine events carry generic typed payloads; gameplay event types belong to Core. There is no dependency from Engine to Core/Application.

## Foundation interfaces (shared ownership contract)

All Engine types below are in `stellar::engine`. Public header `engine/include/stellar/engine/foundation.hpp`; implementations `engine/src/foundation.cpp`. Changing these contracts requires coordinator review before consumers diverge.

- `using Tick = std::uint64_t`. A tick is a monotonic simulation sequence number. Persist tick duration with saves; do not equate a tick with a rendered frame.
- `EntityId { std::uint32_t index; std::uint32_t generation; }`, equality and `value()` returning generation in high 32 bits. Registry owns generations/free slots, validates stale IDs, retires a slot before generation wraps. `EntityRegistry::create()`, `destroy(id) -> bool`, `contains(id) const`, `size() const`. Scene-node addresses and dense array offsets are never durable identity. Import legacy game IDs with an explicit mapping later, not reinterpretation.
- `FixedClock(std::chrono::nanoseconds step)`, `set_paused(bool)`, `set_speed(std::uint32_t)` (1..64), `advance(elapsed,max_ticks=4096) -> std::uint64_t`, `tick()`, `backlog()`. Integer wall-time accumulation multiplied by speed; paused time is discarded. Bounded catch-up retains backlog, never skips simulated time. Invalid/overflow inputs throw. No wall-clock reads inside simulation rules. Headless work can drive explicit ticks directly.
- `DeterministicRandom(std::uint64_t seed)`, `next_u64()`, `unit_double()`. SplitMix64 with fixed constants and high-53-bit conversion. This is a new engine utility, not automatic parity with System.Random. Existing galaxy generator keeps its legacy RNG until an explicit parity adapter is validated. Independent jobs receive streams derived from stable scenario/system IDs, never scheduling order.
- `EventQueue<T>::publish(T)`, `drain() -> std::vector<T>`. Simulation-owner queue; publication order retained, drain swaps the current batch so later events wait for next batch. Worker tasks return events in result buffers; owner merges by job index. No unsynchronized cross-thread publishing.
- `JobSystem(std::size_t workers)`, `submit(std::function<void()>) -> std::future<void>`, `wait_idle()`, `worker_count()`. Bounded worker count, persistent threads, queued jobs. Exceptions travel through futures. Destructor drains accepted work then joins. Dependency orchestration is owner-side barriers/futures; jobs must not synchronously wait on work in the same pool. Concurrent mutation of campaign state is forbidden: immutable inputs, partition-owned output, deterministic owner merge.
- `log(std::string_view level,std::string_view message)` writes synchronized diagnostics. `require(bool,std::string_view)` throws a useful exception. App top level catches standard/unknown exceptions, reports type/message/build/context and returns nonzero without uncaught exception dialogs. Signal/access-violation dump collection is a later Windows crash-service gate.

## Universe and persistence boundaries

Core's first real migrated component is the existing `InterstellarDistance` separation rule. `stellar::core::StarPosition { float x, y; std::optional<double> depth_light_years; }`; functions `distance_light_years(a,b)` and `squared_distance_light_years(a,b)` preserve legacy float 2D behavior when both depths are absent, otherwise double 3D behavior with missing depth=0. Golden fixtures come from the actual preserved C# implementation. This is distance parity, not galaxy-generation or full simulation parity.

Future hierarchical positions use a stable frame entity ID plus double local SI offset; resolve relative positions through a validated acyclic frame graph, then convert camera-relative doubles to floats only at render submission. Never store the entire universe in one GPU float frame. Analytical orbital propagation retains existing element/epoch semantics before extending precision. Physical light-year imports are converted explicitly; gameplay never reads presentation scale.

Native snapshots have magic, version, content/schema IDs, tick semantics, stable identity records and checksum/validation. First fixture/checkpoint format is explicitly a foundation scenario, not a loadable migration of save-v16. Keep the original JSON save reader until all state, once-only events/rewards and IDs can round-trip. No replay of completed commands after restore. Filesystem backend commits temporary files atomically and preserves a previous valid save.

## Contracts reserved for later adapters

| Contract | Boundary |
| --- | --- |
| Render submission | Immutable camera-relative instance/line/mesh batches + generational asset references; no authoritative writes |
| Assets | Stable content ID + content version/hash; explicit load state and lifetime; sources compiled into runtime manifests |
| Input | Timestamped device/window events normalized to drawable coordinates; UI capture precedes world orders |
| UI data | Immutable observer-filtered snapshots; validated commands return result IDs and costs, no direct campaign writes |
| Audio | Cue/voice IDs and gains through an audio backend; no simulation timing dependence |
| Events | Tick + stable sequence + typed Core payload, deterministic owner dispatch; save relevant pending events |
| Jobs | Read snapshot, write disjoint result buffer, merge in stable index order at a tick boundary |
| Profiling | Monotonic scoped spans and counters; tick durations separate from render CPU/GPU, no invented GPU timings |
| Filesystem/export | Scoped project inputs, explicit runtime allowlist, hash manifest, build metadata, clean-directory smoke tests |

SDL3 and Vulkan are the preferred Windows renderer/platform foundation, deferred until the headless boundary works. SDL uses zlib licensing ([official license](https://www.libsdl.org/license.php)); no SDL binary is linked in foundation 0.1.0. Review and pin each actual third-party addition with its redistribution notice. No full Vulkan SDK ships to players. Unsupported GPU/driver errors must be handled before declaring renderer parity. Scripting, general editors and graphics feature expansion remain deferred.

## First milestone and future gates

First milestone: audited baseline, these contracts, native C++23/CMake build, logging/errors, stable entities, clock, events/jobs, headless executable, real distance parity, meaningful tests/benchmarks, versioned portable headless Windows export with manifest and relocated launch. It is deliberately not a replacement playable game.

Subsequent migrations preserve rules in this order unless dependency evidence requires otherwise: galaxy/bodies, clock/civilizations, economy/logistics/colonies, fleets, research/diplomacy/AI, territory, missions/events/artifacts/combat, then renderer/input/audio/player UI parity. New framework utilities are not marked migrated gameplay.

Full integration requires every existing game-loop and save/recovery capability to run without Godot, measured acceptable performance, equivalent player UI/audio/assets/views, all regressions passing and a standalone Windows x64 release export working without development tools. Graphical presets must fail clearly while graphical parity is absent. No engine-complete announcement or production merge before these gates.
