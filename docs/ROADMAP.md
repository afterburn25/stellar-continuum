# Engineering roadmap — native C++ branch

Audited 2026-09-20. **Future work is not completed work.** Existing foundations
are named explicitly below; statuses apply to the full proposed capability.
The [architecture](ENGINE_ARCHITECTURE.md), [current state](PROJECT_STATE.md) and
[known issues](KNOWN_ISSUES.md) govern this branch. The previous roadmap is
preserved as [historical design/status text](history/ROADMAP.pre-native-sync-20260920.txt).

## Immediate reliability work

1. Make a clean native checkout build reproducibly and restore the test/CI
   contract. Review generation fingerprint drift semantically; repair Python
   fixture/catalog assumptions; remove retired CI target references.
2. Investigate the broad developer smoke's supplied-planet-map assertion with
   canonical material identity, actual texture size and captured visual evidence.
3. Run a combined late-game campaign workload with AI, fleets, research, combat
   and autosave. Report per-phase time, memory and save latency, not only FPS.
4. Preserve art sharpness while improving lossless cook fallback rate, streaming
   and package size. Every visual change needs source/cooked/in-game comparison.
5. Validate the next exact-base update against a fresh install and a copied
   real campaign. Keep installed saves untouched and publish only update installers.

## Next engine expansion: 1–30

| # | Capability | Current status / existing foundation | Next deliverable and acceptance boundary |
| --- | --- | --- | --- |
| 1 | Unified Entity/World System | PARTIALLY IMPLEMENTED — generational `EntityRegistry` plus engine `world.cpp` typed store (`engine_world_tests`); domain containers remain separate, library unconsumed | Adapters bridging Core domain containers to the typed store with stale-handle and save-identity tests; no blanket rewrite |
| 2 | Simulation Scheduler + Simulation LOD | PARTIALLY IMPLEMENTED — fixed/strategic/tactical clocks, Core phases, and engine `simulation_scheduler.cpp` (`simulation_scheduler_tests`) library | Drive Core phases through the scheduler (dependency/cadence/LOD policy); deterministic pause/backlog and large-campaign parity |
| 3 | Full Job/Threading System | PARTIALLY IMPLEMENTED — `JobSystem` with priorities, cooperative cancellation, `submit_graph` dependency graphs and per-tag stats + `job_system_tests`; consumers: image preparation, audio director, territory overlay, campaign session | Wider consumer adoption and work-stealing/affinity; owner-safe publication audited per consumer |
| 4 | Render Graph | PARTIALLY IMPLEMENTED — engine `render_graph.cpp` library + `render_pipeline_tests`; direct scene/UI submission remains in the client | Migrate real consumers onto the declared pass graph; measure before/after |
| 5 | GPU-Driven Rendering | PLANNED — CPU batching/culling and cached GPU resources exist | Measured indirect/instanced path, GPU culling and feature fallback without changing object identity |
| 6 | Virtual/Streaming Texture System | PARTIALLY IMPLEMENTED — mip selection, async reads, bounded caches | Residency policy and incremental promotion under measured VRAM pressure; fault/budget tests |
| 7 | Shader Library + Shader Cache | PARTIALLY IMPLEMENTED — source/embedded SPIR-V hash contract | Variant keys, compilation/cache lifecycle and invalidation; offline shipping remains supported |
| 8 | General Particle/VFX Framework | PARTIALLY IMPLEMENTED — specialized eruption timelines, billboards, procedural gas | Shared lifetimes/attachments/LOD/budgets with sharp authored detail; no return to rejected blurry flare volume |
| 9 | Physics Layer | PARTIALLY IMPLEMENTED — analytic orbit, kinematics and continuous primitive queries | Unit/frame/time contracts and reusable collision broadphase; define scope before N-body or rigid-body expansion |
| 10 | Spatial Query Framework | PARTIALLY IMPLEMENTED — point/region/3D indices and parent chains | Unified typed query/lifetime API and batched result ownership; scale/parity tests |
| 11 | Navigation & Route Engine | PARTIALLY IMPLEMENTED — Core lanes/reach/fuel/freight planning | Reusable routing jobs/cache invalidation with observer and resource constraints |
| 12 | Knowledge/Fog-of-War Engine | PARTIALLY IMPLEMENTED — authoritative Core observation/knowledge | General visibility/provenance queries retaining privacy, persistence and safe Developer boundaries |
| 13 | Generic Economy/Resource Framework | PARTIALLY IMPLEMENTED — Core credit/resource/industry/biology services | Explicit resource graph/transactions and reusable schedules; preserve current rule authority |
| 14 | Event Bus | PARTIALLY IMPLEMENTED — owner-thread `EventQueue<T>`, domain events | Typed subscriptions/lifetimes/order and thread boundary; no uncontrolled global callbacks |
| 15 | Mission/Event Framework | PARTIALLY IMPLEMENTED — engine `mission_graph.cpp` library + `mission_graph_tests`; no game consumers yet | Persisted trigger/state/action contracts, observer filtering and a real mission consumer; no hidden UI-only mission state |
| 16 | Advanced Save System | PARTIALLY IMPLEMENTED — Player17 DTO/JSON, atomic backup/recovery | Versioned incremental snapshots/chunks, migration suite and bounded large-save memory |
| 17 | Deterministic Replay | PARTIALLY IMPLEMENTED — engine `replay.cpp` command journal consumed by the native client (`--record`/`--replay`, verified `diverged:false`) plus fixtures/checkpoints/QA metadata | Replay divergence localization and build/catalog identity attestation |
| 18 | Crash Reporter | PARTIALLY IMPLEMENTED — real local bounded logs, reports and Windows dumps | Symbol-matched diagnosis/bundle UX, better fault coverage; no implicit upload |
| 19 | Profiler | PARTIALLY IMPLEMENTED — phase timers, QA throughput/memory and counters | CPU/GPU timeline, frame capture and scenario comparison with minimal perturbation |
| 20 | Memory Tracking | PARTIALLY IMPLEMENTED — queue/cache ledgers and process samples | Tagged allocation/high-water/VRAM attribution with low-cost disabled path |
| 21 | Proper Input System | PARTIALLY IMPLEMENTED — native events and routing/controllers | Action mapping, rebinding, device policy and focus/capture tests |
| 22 | Audio Engine | PARTIALLY IMPLEMENTED — SDL output, decoder, music/SFX/voice settings | Incremental streaming, buses/spatial audio and robust device recovery |
| 23 | Animation System | PARTIALLY IMPLEMENTED — specialized slow spin, orbit and event-stage interpolation | Generic deterministic/cosmetic clock ownership, tracks and lifecycle/LOD contracts |
| 24 | Advanced UI Framework | PARTIALLY IMPLEMENTED — shared skin/text/layout and native workspaces | Focus/navigation, reusable widgets/layout/accessibility and view-model boundaries |
| 25 | Localization | PARTIALLY IMPLEMENTED — engine `LocalizationTable`/`LocalizationService` (fallback chain, `{n}`/`{name}` formats, plural selection, reload) + `localization_tests`; UI strings still hardcoded English | Extract stable message IDs into catalogs and wire real consumers; layout expansion tests |
| 26 | Accessibility | PARTIALLY IMPLEMENTED — engine `AccessibilitySettings` (validated JSON round-trip, scales/contrast/color-blind/reduce-motion/subtitles) plus client adoption: General Settings persists `reduceMotion`, honored by decorative animation (system tumble, eruption loops) | Apply `effective_text_scale`/contrast/color-blind/reduced-flashing across layout and rendering; keyboard/assistive-technology requirements and tests |
| 27 | Platform Layer | PARTIALLY IMPLEMENTED — Windows/SDL services, paths/leases and settings | Explicit portable interfaces and platform tests; Linux/macOS support is not yet established |
| 28 | Steam Integration Layer | PARTIALLY IMPLEMENTED — engine `PlatformServices` facade + null backend + `package_platform_tests`; client owns the facade and reports backend status in diagnostic bundles | A real Steamworks backend for lifecycle/cloud/achievements after offline save/release contracts stabilize; no executable plugin trust |
| 29 | Mod Architecture | PLANNED — versioned data catalogs are not a mod loader | Namespaces/overrides/dependencies/validation and save compatibility; no executable plugin trust assumed |
| 30 | Engine Editor / Stellar Tools | PARTIALLY IMPLEMENTED — cooker/import CLI and Developer inspectors | Unified editor/tool document lifecycle and reuse of Engine APIs; other editor branches are not integrated evidence |

## Dependency and completion policy

Prioritize ownership, scheduling, jobs, spatial queries and diagnostics before
broad renderer/editor systems. Build save/replay compatibility alongside World
and scheduling changes, not afterward. Drive renderer/streaming investment from
measured workloads; preserve the supplied artwork and stable frame behavior.

A capability is done only when its reusable owner, API, consumers, tests,
performance/save impact and limitations are documented in the
[capability registry](ENGINE_CAPABILITIES.md). Every significant workstream leaves
a GitHub commit and updated handoff. A design document or requested feature
alone is never evidence of implementation.
