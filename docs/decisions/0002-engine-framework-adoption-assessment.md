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
2. **Consumer depth on existing projections** where it buys diagnostics:
   the colony projection already surfaced a finding class no check covered
   (`degraded_structures`). Similar cheap wins may exist on the logistics
   and warfare projections.
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
