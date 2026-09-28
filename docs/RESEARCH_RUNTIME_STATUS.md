# Adaptive Research Runtime Status

This research-owned continuity record tracks executable Adaptive Research runtime milestones separately from gameplay-version promotion.

## Current accepted runtime milestone

**Milestone #13 — plain-C# Adaptive Research runtime foundation**

- PR: **#86**
- merged: **`2b5e1e30783db67524fac3788552f667c69879c0`**
- final validated PR head: **`0171f786b8f19d08fea026aa05d7d9ce0647e960`**
- runtime namespace: `Game.Simulation.Research.Adaptive`
- owned source path: `src/Game/Simulation/Research/Adaptive/**`
- status: validated side-by-side runtime foundation; **not** gameplay cutover

## Non-cutover boundary

The existing prototype gameplay research implementation remains untouched:

- `src/Game/Simulation/Research/ResearchSimulation.cs`
- `TechnologyDefinition.cs`
- `TechnologyRegistry.cs`
- `TechnologyState.cs`

Milestone #13 does not change gameplay VERSION or the global campaign save version.

## Executable foundation now available

- immutable loader for the current **360-node / 21-domain** public possibility graph
- canonical RP cost/lab requirements, diminishing returns and directed-program stages loaded from machine data
- prerequisite / Pressure / evidence / applicability-trait / cross-lineage-capability wake-up indexes
- sparse civilization research state; Unknown nodes are absent and static definitions are never copied per civilization
- civilization-scoped vs population/species-scoped applicability contexts
- modular specialist research-facility capabilities and stage requirements
- data-driven progression bands: Experimental 0–45%, Demonstrated 45–70%, Engineering 70–100%
- bounded readiness-efficiency policy loaded from canonical data
- authoritative scientific-eligibility and project-start blockers
- visible-only materialized view; no Unknown placeholders/hidden edges/hidden Pressure targets
- directed project start/pause/resume/lab reallocation/readiness/progression
- canonical lab diminishing returns and RP progression
- explicit stage facility blocking
- cross-lineage capability, trait and directed-program-capacity grants
- hypothesis nodes pause at the Experimental evidence boundary for explicit scientific resolution
- durable scientific deployment-permission queries separated from physical deployment
- standalone versioned sparse Adaptive Research snapshot schema v1 (not a campaign save version)
- modular 15-fragment / 7-reference-profile starting-history composer
- starting physical research institutions aggregate into real Effective Research Lab/facility capability state
- field-competence/tacit/institution seeds not yet represented as mutable runtime detail are retained in an explicit deferred initialization payload

## Runtime validation baseline

Dedicated `research-runtime` workflow run **34174514701**, job **101901158958**:

- restore: PASS
- build: PASS, **0 warnings / 0 errors**
- executable runtime checks: PASS

Measured check output:

- public catalog: **360 nodes**
- synthetic progression visible nodes: **24**
- human-like 2050 visible starting horizon: **82**
- ammonia-rich visible starting horizon: **82**
- 32 assigned labs with 16 recommended: **21.6 scaled lab units**
- sparse synthetic snapshot: **3,486 bytes**

Checks prove:

- a new civilization starts with zero per-civilization graph-node copies
- bounded/indexed emergence only materializes eligible possibilities
- one directed project is enforced at the starting research-capacity stage
- normal project maturation and prerequisite-index wakeups work
- population-context applicability does not leak between populations
- Prototype Warp reads canonical high-energy / field-physics / precision-measurement facility requirements
- visible views contain only visible-to-visible edges and recognized Pressure targets
- snapshots round-trip sparse state without expanding the hidden graph
- human-like 2050 starts with `fusion_power` Investigable and `prototype_warp_drive` absent, carbon/water/metabolic/gravity-sensitive biology, **12 Effective Research Labs**, and one directed program
- ammonia-rich starting history uses the same graph while retaining its own native biochemistry and excluding silicon-native branches

## Full milestone #13 acceptance

On the exact final head, all existing research gates passed:

- core catalog / maturation / competence / foreign-tech / start-runtime / agenda-AI validators
- 500/1,000-year long-horizon benchmark
- biochemical structure/applicability
- distributed continuity
- secrecy
- collaboration
- research-runtime executable checks
- .NET restore/build: **0 warnings / 0 errors**

PR #86 changed exactly **17 research-owned files** under:

- `.github/workflows/research-runtime.yml`
- `docs/RESEARCH_RUNTIME_IMPLEMENTATION.md`
- `src/Game/Simulation/Research/Adaptive/**`
- `tests/AdaptiveResearchRuntimeChecks/**`

No legacy prototype research file, VERSION file, or unrelated gameplay source changed.

## Shared CI caveat

GitHub issue **#61** remains: the shared Godot runtime smoke can return success while logging failure to instantiate `res://src/Game/Presentation/Main.cs`. Milestone #13 does not claim semantic Godot runtime health from that process step.

## Next runtime milestone

**Milestone #14 — competence, institutions, tacit knowledge and authoritative Project Readiness runtime.**

Goals:

- add sparse mutable theoretical / experimental / engineering competence by knowledge field
- consume the deferred starting competence payload from milestone #13
- represent active specialist research institutions/capacity without per-building/per-scientist simulation
- add scoped tacit assets: expert cohorts, protocols, tooling, institutions, training pipelines, etc.
- calculate Project Readiness authoritatively from actual field competence + relevant facility readiness + evidence + tacit expertise
- remove ordinary caller-supplied readiness as the authoritative source of project speed
- preserve real hard blockers separately from readiness efficiency
- model active-practice atrophy/recovery without erasing historical scientific knowledge
- persist competence/tacit/institution state sparsely in the standalone research snapshot
- add deterministic runtime checks/long-horizon bounds before any gameplay cutover

The legacy gameplay research loop remains untouched until a later explicitly coordinated migration/cutover milestone.
