# Full campaign coordinator migration boundary

The authority is `src/Game/Simulation/GalaxySimulationStepCoordinator.cs`, its constructor wiring, command adapters and ordered `Advance` method. Existing native subsystem libraries are inputs to this conversion, not proof that a full native campaign tick already exists. No partial coordinator may be labelled complete by inserting no-op implementations for missing phases.

## Owned campaign and runtime lifetimes

Extend the owned fresh-campaign state through an explicit runtime aggregate that can retain all authoritative state required by the source coordinator and its dependencies. Fresh initialization output is not automatically a save format. Keep simulation state, derived runtime caches and presentation events distinguishable. Reference the source save schema before deciding which additional state is durable.

Create borrowed subsystem views immediately before each operation. Shipbuilding appends fleets and colonization appends colonies: both can invalidate vector-backed spans, references and cached iterators held by earlier phases. Rebuild the economy fleet projection after ship creation if a later operation reads it; never reuse a pre-shipbuilding span for exploration or combat. Do not pass an old colony span into storage capping after settlement creation. Cache immutable astronomy by campaign identity, but invalidate any graph that borrows a replaced system vector.

The coordinator retains the same stateful strategic and combat runtime objects across ticks. Reconstructing combat per step would lose active engagements and duplicate start notifications; reconstructing strategic AI would reset review scheduling and published priorities. Preserve source campaign-seed reset behavior and do not confuse its derived review clock with accepted simulation delta.

## Ordered Advance phases

1. Validate world presence, then finite nonnegative days. Zero returns an empty result before reserve snapshots or subsystem calls.
2. Snapshot each economy's industry by civilization ID. Source ToDictionary rejects duplicate economy IDs before any phase mutates state.
3. Run `advance_colony_economies` with the source legacy-science flag. Preserve funding, sustenance, production, upkeep and accrual already implemented by the economy gate.
4. Bind campaign industry priorities and advance strategic AI. Its observer-local knowledge, scheduled reviews, industry weights and shipbuilding preferences must be actual ports, not fabricated defaults for an integrated-mode claim.
5. Ensure automatic construction orders, then automatic ship orders, using the same capability and preference providers as the source.
6. Visit non-seeded-ancient civilizations in source order. Read the first matching economy, compute source-clamped available industry and each subsystem's time-bounded demand, and apply the reviewed allocation policy. Budgets overwrite by civilization ID while the result allocation list retains each visit; preserve duplicate semantics.
7. Advance construction, then shipbuilding, with their separate allocated budgets and accepted days. Run legacy research only if the source flag enables it.
8. Advance exploration, freight, combat, then colonization, in that order. A newly constructed ship can therefore participate in later phases of the same step. Do not parallelize these dependent mutations.
9. Apply industry storage caps using the pre-step reserve snapshot and the current post-settlement world.
10. Return ordered allocations and construction, shipbuilding, research, exploration, combat and colonization events with the accepted delta. Freight has no event list in this result. Preserve source combat-outcome projection separately.

On any exception, earlier mutations and stateful runtime changes remain as the source leaves them; the method-local result is not returned. Do not add rollback, retry the entire tick automatically, or publish partial event arrays as though the call succeeded.

## Existing ports and remaining bridges

Native implementations already include `advance_colony_economies`/storage capping, construction automatic orders/demand/advancement, shipbuilding automatic orders/demand/advancement, industry allocation, legacy research and exploration advancement. Freight and settlement knowledge are integrated in 0.1.15; settlement planning and stateful combat simulation in 0.1.16; colony commands/establishment, strategic intent/providers/planning, combat readiness and prototype capability adapters in 0.1.17. Strategic self-state/director/runtime composition, matched combat command/hostility composition, capability projections for the full coordinator, and coordinator command wrappers remain open.

An explicit standalone compatibility configuration may use the source's peaceful hostility or empty strategic knowledge providers. It must be labelled as that source configuration, not as full integrated diplomacy or Adaptive Research. Legacy technology capability mapping and Adaptive Research capability mapping are separate adapters; never substitute the legacy six-tech registry for the modern tree.

## Command and acceptance gates

Port public coordinator commands after their owning implementations, preserving authorization, lookup and mutation order. Matched combat preview/issuance/stepping must share one hostility policy. A standalone raw combat simulation must retain the source's fail-closed preview behavior. Military deployment clears local combat intent before route assignment; civilian exploration/recovery and paid settlement commands keep their separate source rules.

Use actual source multi-step scenarios, including economy-funded construction completion, ship creation followed by movement, survey-dependent settlement, cargo delivery/storage limits, research unlocks, engagement continuity, zero/invalid days and failures late in the phase sequence. Compare full state and ordered events after every call, including runtime cache observations exposed through subsequent behavior. Test campaigns across supported sizes without claiming initialization benchmarks are tick throughput or FPS. A full native campaign CLI and player-save migration require their own validated input/output boundary before enabling a playable export.
