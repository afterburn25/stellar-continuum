# ADR 0002 — Engine-framework adoption assessment (Core authority)

Status: **Assessment — pending owner decision**, recorded 2026-10-08.

This is the per-framework audit the
[2026-09-24 projections-first decision](../DECISION_LOG.md) calls for and
`AGENT_HANDOFF.md` flags as the recommended next workstream. It applies the
four graduation criteria to each engine simulation framework and records a
recommended posture. It changes no authority today.

## Graduation criteria (from DECISION_LOG 2026-09-24)

A framework moves from projection to authority only when:

1. its semantics are a strict superset or provably equivalent for every
   observable Core rule it would replace;
2. a save-format migration path exists (new fields, defaults, round-trip);
3. a parity oracle demonstrates identical outcomes over a seeded campaign
   corpus before the bespoke path is retired;
4. determinism and observer-privacy contracts are preserved.

## Inventory and verdicts

| Framework | Current Core consumer | Mode | Criterion-1 assessment | Recommended posture |
|---|---|---|---|---|
| `EventHistory` (`history.hpp`) | `campaign_event_history` — runtime owns it; persisted in v17 saves (`"EventHistory"` tail) | **Authority** | Graduated — observer-safe `feed()` projection already serves notifications/chronicle | Done; no further action |
| `SimulationExecutor`/`SimulationScheduler` | `GalaxySimulationStepCoordinator` — 12 strategic phases as Active-tier dependency-chained tasks (`campaign_coordinator_parity` oracle) | **Authority (orchestration)** | Graduated for phase scheduling | Next step already scoped: per-entity executor cadence inside heavy phases |
| `GalaxyMap` | `project_galaxy_map` + framework codec; lane traversal stays `lane_network` | Model adopted, traversal stays Core | Partial by design — chart model is shared, route authority is Core's | Permanent split; no migration needed |
| `PlanetEnvironment`/`evaluate_habitability` | `planetary_adapter` | Projection | Fails — `assess_species_planet`/`species_environment` carry game-specific species biology the engine tags deliberately approximate | Permanent projection (header already contracts this) |
| `World` | `campaign_world_projection` | Projection | Fails — flat legacy-id namespaces vs Core's per-domain ids; store is query/inspection only | Permanent projection |
| `Colony` | `campaign_colony_projection` | Projection (diagnostics findings: `degraded_structures`, logistics) | Fails — engine Colony models surface buildings + powered allocation; Core `colony_economy` adds sustenance, biology, labor, stability chains | Permanent projection; extend findings coverage as needed |
| `Population` | `campaign_population_projection` | Projection (`migration_pressure` etc. — header forbids `advance()`) | Fails — Core population is intentionally scalar millions + species profile; engine cohorts would create a second growth authority | Permanent projection |
| `WarfareModel` | `campaign_warfare_projection` | Projection (FleetReport, Lanchester previews) | Fails — documented mapping gaps: `defense=0`, `interdiction=0`, single-vessel fidelity; Core combat is tactical `massive_combat` + strategic fleets | Permanent projection; useful for diagnostics/preview tooling |
| `analyze_economy` | `campaign_economy_projection` | Projection (demand/bottleneck analysis) | N/A — analysis utility, not a simulator; nothing to graduate | Projection is the correct end state |
| `LogisticsNetwork` | `campaign_logistics_projection` | Projection | Fails — documented unit mismatch: engine capacity is absolute in-flight quantity, Core is per-day rate | Permanent projection |
| `StrategicMind` (`strategic_ai.hpp`) | `campaign_diagnostics` advisor spotlight (severity-weighted finding ranking) + engine shell debugger | Query/eval only | Fails — Core `strategic_*` stack (intent/planning/runtime/input builder) is richer and observer-sealed per the Fair-AI decision | Permanent query consumer; do not route AI authority through it without re-proving Fair-AI compliance |
| `Terraforming` | Engine shell planet demo only | No Core consumer | No incumbent — Core has no terraforming simulation (one flavor string only) | **Greenfield candidate**: if terraforming gameplay is ever wanted, adopt the framework as first authority — there is nothing to displace |
| `FlowNetwork` | Engine shell tool only | No Core consumer | No mapping — Core power is per-structure scalar (`stored_power_days`), not topology | Dormant; revisit only if Core ever models grid topology |
| `ResourceNetwork` (`resource_economy.hpp`) | Engine shell tool only | No Core consumer | Fails — `campaign_economy` + `analyze_economy` projection already cover the observable demand surface | Dormant; a projection is possible but adds nothing `campaign_economy_projection` doesn't already show |
| `MissionGraph` | Engine shell tool only | No Core consumer | Fails — Core missions are typed runtimes (`exploration_planning`, `civilian_recovery`, `colonization_runtime`), not a generic dependency graph | Dormant; models don't align |

## Recommendation

**Keep the projections-first posture; no framework currently meets criterion 1.**
The adoption frontier is not authority migration — it is:

1. **Executor granularity** (already scoped in the handoff): per-entity
   `SimulationExecutor` cadence inside the heavy coordinator phases. This is
   scheduling, not a second authority, so the parity oracle pattern already
   established (`campaign_coordinator_parity`) carries over directly.
   **Entry-point sketch** (scoped 2026-10-08): the 12 phase tasks run on a
   single coordinator-owned executor with per-key tier demotion, dirty/event
   wakeups and domains already available; per-entity cadence has two
   structural options —
   (a) *phase-internal cadence*: a phase keeps a per-entity next-due
   accumulator and integrates each entity only when due, consuming the
   accumulated span — the same coarse-integration contract as phase
   demotion, moved inside the entity loop. No executor or ordering change;
   divergence risk is entity-to-entity interaction between runs, so
   adoption is per-entity-class and gated on a seeded parity oracle
   (candidate first targets: `exploration` and `freight` entity loops —
   distant dormant fleets are the clearest wasted work);
   (b) *executor-level entity tasks*: register one task per entity in a
   shared domain — uniform tier/wakeup handling but explodes task count
   (50k-system galaxies) and replaces intra-phase ordering with domain
   ordering, a materially larger contract change. Option (a) is the
   right-sized first move; (b) only if cadence bookkeeping itself becomes
   the hot path.

   **Status (landed):** the first option-(a) deployment is in
   `ExplorationSimulation::advance`. The idle-AI survey-fleet path is a
   pure-verdict class — for it "due" means "a verdict-affecting input
   changed", so the cadence is implemented as a revision-gated memo rather
   than a time accumulator: `idle_survey_verdicts_` retains the
   "no supported survey work" outcome per fleet, keyed on knowledge
   survey-level revisions (`CivilizationKnowledgeState::
   survey_level_revision`, bumped on survey-entry insertion and level
   transitions), lane-network identity (`InterstellarLaneNetwork::
   instance_nonce`, which survives in-place rebuilds through
   move-assignment), a content signature over the colony set, and every
   fleet field the reach/return assessments consume. Reservations and
   candidate ordering are deliberately outside the key because they cannot
   flip an empty verdict; injected `ExplorationReachAssessment` providers
   bypass the memo entirely since their input set is unknowable.
   Diagnostics: `ExplorationSimulation::idle_verdict_stats()` reports
   hits/misses/stored. Coverage: `exploration_idle_cadence` (revision,
   fleet-field, colony-set invalidation, injected-reach bypass) and
   `exploration_idle_cadence_400` (scale run; measured ~44x on the idle
   evaluation path — 80k retained evaluations in ~2.7 ms against a fresh
   `select_mission` baseline). Seeded-oracle gate:
   `campaign_coordinator_parity` unchanged.

   **Second deployment (landed):** `FreightSimulation::advance` was the
   other named candidate, but its audit showed the waste is not idle
   re-planning — active freighters perform real transfer work every tick,
   and a time cadence would alter clamp-boundary arithmetic. The repeated
   cost is rebuilding loop-invariant projections per freighter inside one
   `advance`: `construction_projection`, `economic_fleet_projection`,
   `industry_storage_capacity` (colony infrastructure, construction
   completion, civ flags — none mutated by freight), and
   `port_transfer_capacity_per_day`. The adopted equivalent work reduction
   is an exact per-advance hoist: port capacity is memoized per colony id
   and industry storage capacity per civilization, while
   `economy.industry` — which unloads mutate mid-loop — stays live in the
   subtraction, preserving the second-freighter clamp. No scheduling or
   ordering semantics changed; seeded-oracle gate: `freight_parity`
   (87 C# cases + 2 native boundaries) unchanged, covered by
   `freight_cadence` (mid-loop clamp on the second freighter, port-capacity
   reuse, outpost loading, idle/ineligible skips). Executor-level entity
   tasks (b) stay deferred.

   **Third deployment (landed):** the wider coordinator-phase audit found
   the same repeated-scan pattern in the economy/storage-cap phases.
   `advance_colony_economies` called `credit_flow` per economy, and both
   `credit_flow` and the mutable advancement loop re-scanned the full
   colony span filtering by civilization id — O(economies × colonies)
   visits per step; `apply_industry_storage_caps` →
   `industry_storage_capacity` did the same. The reduction is a per-step
   civilization→colony pointer index built once (`colonies_by_civilization`,
   preserving world order inside each bucket), so each economy sees exactly
   its own colonies — accumulation and mutation order are bit-identical.
   `economy_credit_flow`'s lazy `economy_for` lookup is deliberately
   preserved: `include_research=false` callers with a missing economy row
   must not throw. In `CampaignCoordinator::run_industry_allocation` the
   `ConstructionWorld`/`ShipbuildingWorld` span views were also hoisted out
   of the civilization loop — pure view builders, same spans and closures.
   The win is asymptotic (skipped iterations were cheap civ-id branches);
   it removes E×C rescans and per-civ view rebuilds per step.
   Seeded-oracle gate: `campaign_economy_parity`,
   `campaign_coordinator_parity`, `campaign_frame_parity`,
   `industry_allocation_parity`, `galaxy_economy_persistence_parity`,
   `economy_scale_5000_colonies` — all unchanged.

   **Fourth deployment (landed):** the same audit extended to the
   industry-allocation, construction and shipbuilding phases, whose batch
   loops linearly rescanned civ-keyed rows per civilization —
   `state_for`/`economy_for`/`civ_for`/`first(shipyards)` are all
   `find_if` scans. `advance_construction`,
   `ensure_automatic_construction_orders`, `advance_core` (shipbuilding)
   and `ensure_automatic_ship_orders` now build per-call
   `unordered_map<int, T*>` indexes (`civ_index_of`, `emplace` preserving
   find_if's first-match) and resolve each row once; the per-civ body of
   `advance_construction_for_civilization` was extracted to
   `advance_construction_resolved` so the batch path reuses resolved
   pointers while the public entry point keeps its lookup semantics and
   throws. Throw ordering and lazy-lookup positions are preserved exactly.
   `campaign_industry_weights` gained a `const CivilizationEconomy*`
   overload so the industry-allocation loop resolves weights from the row
   it already found. The colony sweeps then followed the same pattern:
   `advance_construction` builds a `civ_buckets_of` colony index once and
   feeds each civ's ordered bucket through
   `surface_construction_industry_demand`/`advance_surface_construction`
   owned-pointer overloads, with the resolved `CivilizationEconomy*`
   passed through so `civilization_operating_funding` and `economy_for`
   no longer rescan — the public span signatures delegate, preserving the
   lazy missing-economy throw (funding still falls back on a null row;
   the spend still throws only when budget > 0). Remaining rescans,
   documented rather than fixed: `select_ai_design`'s
   fleet `any_of` scans, `promote`/`lock`'s bounded promotion-time
   lookups, and `strategic_.advance`/colonization/combat internals.
   Seeded-oracle gate: `construction_projects_parity`,
   `surface_construction_parity`, `shipbuilding_parity`,
   `shipyard_state_parity`, `industry_allocation_parity`,
   `campaign_coordinator_parity`, `campaign_frame_parity` — all unchanged.

   **Fifth deployment (landed):** the allocation loop's last per-civ
   rescans. `run_industry_allocation` now builds
   `unordered_map<int, const T*>` indexes for `campaign.construction`,
   `campaign.shipyards` and a `civ -> ordered colonies` bucket map once
   per phase, then resolves each civilization's rows a single time.
   `construction_industry_demand` gained a resolved-input overload
   (`const ConstructionState*` + `span<const Colony* const>`) and
   `shipbuilding_industry_demand` a `const ShipyardState*` overload;
   `surface_construction_industry_demand` gained the const-element span
   overload both need. Each overload throws the same
   `out_of_range("Sequence contains no matching element")` on a null
   resolved row that `state_for`/`first` produced, and colony demand
   iterates the civ's bucket in world order — identical to the filtered
   full-span scans they replace. The public civ-id overloads remain;
   the shipbuilding one delegates to the resolved body. Seeded-oracle
   gate: same set as the fourth deployment, unchanged.

   **Sixth deployment (landed):** the remaining audit items.
   `select_ai_design` replaced its up-to-four `any_of` fleet scans per
   AI order decision with one pass filling `(role, populated)` flag
   arrays — out-of-range role ordinals still report absent, matching the
   original equality-compare misses. `ColonizationSimulation::advance`
   previously rescanned economies (`civilization_operating_funding` in
   the skip guard and inside `advance_establishment`), civilizations
   (`require_civilization`), bodies (`body_in_system`) and all colonies
   (`occupied` test) per candidate fleet, and rebuilt the AI
   opportunity branch's system/body maps per idle colony fleet. Each
   index builds **lazily on first use**: civ-keyed
   economy/civilization indexes, a `(body_id, system_id)`-keyed body
   index and an `occupied_systems` set (colonies founded mid-loop join
   the set through `occupy`, preserving the original visibility), and
   the planner maps — so duplicate-key throws still fire only when a
   fleet reaches the branch. `advance_establishment` now takes the
   resolved `const CivilizationEconomy*`. Lazy construction was forced
   by measurement, not taste: an eager per-advance build cost more than
   the scans on the dominant "no candidates" path — the canonical
   benchmark (2500 systems, 1000 stress fleets, 4000 ticks) showed
   colonization ballooning to 6,978 ms total (step mean 5.6 ms);
   lazily built it is 328 ms, with an identical `finalStateHash`.
   `LegacyResearchSimulation::advance_core` gains the same three-index
   pattern for technologies/construction/economies with identical
   missing-row throw order. Seeded-oracle gate: colonization,
   settlement, shipbuilding, industry-allocation, construction,
   legacy/adaptive research and coordinator/frame parity — unchanged.
   **Audit closeout measurement** (quiet machine, canonical scenario):
   step mean 2.75 ms / p95 4.01 ms vs the ~2.59 ms pre-audit baseline —
   the cumulative phase work is within run-to-run noise of baseline
   while removing the O(civs × world) asymptotes; colonization sits at
   214 ms total. `finalStateHash` `b57c03d1…` is identical to every
   pre- and post-audit run. The remaining core step cost is dominated
   by `adaptive_research` (4,526 ms) and `combat` (2,517 ms) — outside
   the 12 audited coordinator phases — plus autosave latency
   (1,691 ms mean for 66.7 MB saves), which is the persistence debt
   item rather than a phase problem.
2. **Consumer depth on existing projections** where it buys diagnostics:
   the colony projection already surfaced a finding class no check covered
   (`degraded_structures`). **Status (landed):** `campaign_diagnostics`
   now exercises every previously untriggered finding class —
   `degraded_structures` (settlement projection condition),
   `power_shortfall` (sustenance analysis bottleneck),
   `foreign_armed_presence` (warfare theater projection),
   `treasury_arrears` and `treasury_depleted` (credit flow +
   `assess_treasury`), `freight_corridor_gap` and `logistics_critical`
   (logistics coverage projection) — each staged through authoritative
   state, not fabricated records.
3. **Greenfield, not graduation, for genuinely new capability:**
   `Terraforming` is the one framework where first-authority adoption is
   realistic — Core has no competing simulation, so criterion 1 is vacuously
   satisfied if and when terraforming gameplay enters scope. It would need a
   save-format story (criterion 2) at that point, but no parity oracle
   (criterion 3 is trivially met by construction).

The durable risk the projections-first decision guards against — two live
simulators diverging — is unchanged. Every `campaign_*_projection` header
documents its no-write-back contract; keep enforcing it at review time.

## Consequences

- "Adoption pending" rows in `ENGINE_CAPABILITIES.md` stay as-is; per the
  decision log, permanent projection is an acceptable end state.
- If a future proposal migrates a framework to authority, the four criteria
  above plus this table's per-framework notes are the checklist — in
  particular the parity-oracle requirement (criterion 3) before any bespoke
  Core path is retired.
- Owner sign-off needed before any posture marked "permanent projection" or
  "dormant" is revisited.
