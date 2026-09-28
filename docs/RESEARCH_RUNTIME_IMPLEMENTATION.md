# Adaptive Research Runtime Implementation

This document describes the first executable plain-C# Adaptive Research runtime foundation. It is an implementation contract, not a claim that the legacy gameplay research loop has already been replaced.

## Ownership and namespace

Adaptive Research owns:

- `src/Game/Simulation/Research/Adaptive/**`
- `tests/AdaptiveResearchRuntimeChecks/**`
- `.github/workflows/research-runtime.yml`

The legacy prototype files directly under `src/Game/Simulation/Research/` remain untouched during this milestone. They continue to support the current gameplay baseline until a separately coordinated migration/cutover milestone.

Namespace: `Game.Simulation.Research.Adaptive`.

## Implemented runtime layers

### Immutable shared catalog

`AdaptiveResearchCatalogLoader` loads the public possibility graph once from `data/research/v1/` and builds immutable definitions and wake-up indexes.

It derives:

- node definitions
- knowledge prerequisites
- awareness sources
- Pressure affinities and explicit Pressure gates
- evidence requirements
- applicability requirements
- cross-lineage capability requirements
- project RP work from canonical complexity/depth data
- minimum/recommended Effective Research Labs
- canonical lab diminishing-return policy
- directed-program stages
- maturity/deployment grants
- prerequisite / Pressure / evidence / trait / capability wake-up indexes

Declared node outputs are allowed to contain implementation-specific markers. Only IDs registered by `capability_model.json` become cross-lineage functional capabilities.

### Sparse civilization state

`AdaptiveResearchCivilizationState` stores only materialized/active civilization-specific state:

- visible node state
- nonzero Research Pressure
- evidence instances with provenance/confidence/context
- civilization-scoped applicability traits
- sparse population/species applicability contexts
- scoped cross-lineage capabilities
- available research-facility capability IDs
- enabled research-side deployment permissions/cache
- active directed projects
- Effective Research Lab capacity/allocation
- directed-program stage
- revision counters

Unknown nodes are absent. Static definitions are never copied into civilization state.

### Applicability scope

`AdaptiveResearchApplicabilityCatalog` preserves the distinction between:

- civilization-scoped traits; and
- population/species-scoped traits.

A trait on one population context does not make the same technology applicable to every population in the civilization.

### Research facilities

`AdaptiveResearchFacilityCatalog` loads the modular facility index and current facility catalogs. It exposes only research-facing capabilities and explicit stage requirements.

Construction/economy remains responsible for actual facility existence, cost, damage, maintenance, and location.

### Eligibility and blockers

`AdaptiveResearchEligibilityEvaluator` separates:

1. **scientific eligibility** — whether recognized knowledge/evidence/applicability/Pressure/capability conditions support an Investigable project; and
2. **project-start eligibility** — whether the civilization can actually staff/start the project now.

Concrete blockers include:

- missing prerequisite knowledge
- missing alternative prerequisite
- missing applicability context/trait
- missing evidence
- insufficient explicit Research Pressure
- missing cross-lineage capability
- unavailable directed-program capacity
- too few free/assigned Effective Research Labs
- missing specialist research-facility capability

A visible Investigable project may therefore remain unavailable to start because real research infrastructure is insufficient.

### Visible-only materialized view

`AdaptiveResearchViewBuilder` projects only already-visible node state, visible-to-visible edges, recognized Pressures, active projects, directed-program capacity, and safe explanations.

It does not render Unknown placeholder nodes, total hidden-node counts, hidden edges, hidden future costs, or hidden Pressure targets. Hidden prerequisite IDs are sanitized from explanation output.

### Project progression

`AdaptiveResearchProgressPolicy` loads canonical stage work fractions and readiness efficiency:

- Experimental: 0–45%
- Demonstrated: 45–70%
- Engineering: 70–100%

Project progress uses:

`scaled assigned labs × RP per effective lab per year × bounded readiness efficiency × elapsed years`

Research Pressure never multiplies progress speed.

`AdaptiveResearchRuntime` supports:

- indexed Pressure/evidence/trait/capability wake-ups
- bounded basic-science review batches
- start / pause / resume
- lab reallocation
- readiness updates
- time progression
- explicit stage facility checks
- capability/trait/research-capacity grants
- hypothesis resolution

True hypothesis nodes pause at the Experimental evidence boundary and require explicit scientific resolution rather than automatically succeeding.

### Deployment boundary

Research may scientifically enable a deployment event but does not physically perform it.

`AdaptiveResearchDeploymentQueries` can derive deployment permission from the relevant Mature enabling knowledge, so a save/reload cannot lose the permission merely because an earlier transient event has already fired. Physical deployment remains owned by the relevant gameplay subsystem.

### Standalone snapshot

`AdaptiveResearchSnapshotCodec` provides a versioned research snapshot payload with catalog-ID and stable-reference validation.

This is **not** a new campaign save version. Global save integration belongs at a coordinated save boundary.

The snapshot remains sparse and never serializes the static possibility graph.

## Starting-history composition

`AdaptiveResearchStartingProfileComposer` consumes the modular starting-profile index and composes the existing scientific-history fragments into runtime state.

It:

- validates exactly one base-era fragment
- composes the strongest non-conflicting historical node state
- keeps population-scoped traits on a supplied primary applicability context
- keeps civilization-scoped traits global
- aggregates starting Research Pressure by maximum justified value
- validates prerequisite closure and applicability
- applies Mature historical research effects without performing physical deployments
- converts starting research institutions into real Effective Research Lab and research-facility capability capacity
- performs one initialization-only basic-science-aware horizon rebuild

The full hidden future tree is not stored.

### Deferred initialization payload

Current fragments also carry field competence and may later carry tacit-knowledge information. Those details are preserved by `AdaptiveResearchDeferredStartingState` rather than silently discarded or faked.

Future competence/tacit runtime integration can consume this explicit payload. Physical institution seeds are also returned so construction/save integration can own their eventual concrete instances.

## Performance boundaries

Normal campaign runtime must remain event/index driven.

Permitted full public-catalog scans are limited to exceptional offline/initialization operations such as:

- catalog validation/loading
- deterministic offline benchmark tools
- one-time new-game/migration horizon composition

Per-tick/per-frame full graph scans remain prohibited.

## Current validation

`tests/AdaptiveResearchRuntimeChecks` is an executable .NET check project run by `.github/workflows/research-runtime.yml`.

It validates real catalog loading, sparse state, directed-program limits, project progress, population-context isolation, specialist facility requirements, canonical lab diminishing returns, visible-only projection, standalone snapshot round-trip, and reference starting-profile composition.

Existing data validators/long-horizon benchmarks continue to run separately and remain authoritative for the design/data layer.

## Explicitly not claimed by this milestone

This milestone does **not** yet:

- replace `ResearchSimulation` / `TechnologyRegistry` / `TechnologyState`
- change gameplay VERSION
- change global campaign save format
- wire Adaptive Research into the main simulation tick
- implement player UI
- implement full competence/tacit-asset mutable runtime state
- implement distributed/secrecy/collaboration mutable runtime state in C#
- implement diplomacy, intelligence, population, construction, or economy systems owned by other workstreams
- expose secret/rare research content in the public repository

Those are later integration/runtime milestones built around the stable interfaces established here.
