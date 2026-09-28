# New-Chat Handoff Protocol

This file lets Stellar Continuum development move between chats without relying on conversational memory.

## Current shared-project bootstrap — 2026-09-08

Start with `integration`, `work/core-game-integration`, current `docs/WORKSTREAMS.md`, `docs/BRANCH_INVENTORY_2026-09-08.md`, per-lead `docs/handoffs/`, and recent #15/#18/workstream issue comments. Fetch all branches, compare histories, preserve unique changes and synchronize without reset/force-push. Core coordinates integration; specialist ownership and observer-safe contracts remain intact. No work directly on `main` or promotion without separate release readiness.

The canonical Adaptive Research continuation is `research/adaptive-research`, confirmed by #179 after accepted M19 reconciliation. `dev/adaptive-research` and mixed/archive branches below are historical evidence. Do not execute their old main-first merge instructions. The earlier research snapshot below is retained for continuity, not as current shared gameplay/branch status.

## Historical research bootstrap snapshot

### Earlier bootstrap prompt for an Adaptive Research chat

> **Open public repo `afterburn25/stellar-continuum`. Before changing code/data, read `docs/CHAT_HANDOFF.md`, `docs/PROJECT_STATE.md`, `docs/WORKSTREAMS.md`, `docs/DEVELOPMENT_HISTORY.md`, `docs/DECISION_LOG.md`, `docs/GAME_DIRECTION.md`, `docs/ENGINEERING_GUARDRAILS.md`, `docs/ROADMAP.md`, `docs/BRANDING.md`, `docs/ARCHITECTURE.md`, `docs/AI.md`, and every Adaptive Research specification on `main`, especially `ADAPTIVE_RESEARCH_SYSTEM.md`, `RESEARCH_ECONOMY.md`, `RESEARCH_CAPACITY_MODEL.md`, `RESEARCH_EMERGENCE_MODEL.md`, `RESEARCH_MATURATION_MODEL.md`, `RESEARCH_COMPETENCE_MODEL.md`, `RESEARCH_FOREIGN_TECH_MODEL.md`, `RESEARCH_UI_MODEL.md`, `RESEARCH_START_RUNTIME_MODEL.md`, `RESEARCH_AGENDA_AI_MODEL.md`, `RESEARCH_BENCHMARK_MODEL.md`, `RESEARCH_BENCHMARK_BASELINE.md`, `RESEARCH_BIOCHEMISTRY_MODEL.md`, `RESEARCH_BIOCHEMISTRY_MULTISPECIES.md`, `RESEARCH_BIOCHEMISTRY_BENCHMARK.md`, `RESEARCH_DISTRIBUTED_CONTINUITY_MODEL.md`, `RESEARCH_SECRECY_MODEL.md`, `RESEARCH_COLLABORATION_MODEL.md`, and `RESEARCH_CI_KNOWN_ISSUES.md`. Load the relevant `data/research/v1/` JSON and research validators/workflows, inspect `main`, `dev/adaptive-research`, open PRs/issues, VERSION/GameVersion and CI, then state the gameplay baseline separately from research milestones. Summarize the 360-node/21-domain architecture, benchmark baselines, biochemical/multispecies rules, distributed-knowledge rules, secrecy rules, collaboration rules, branch ownership, current milestone, and #61 caveat before editing anything. Do not reveal hidden future research, create fixed species trees, add hidden catch-up/rank cheats, duplicate the graph per population/region/security compartment/partner, weaken validators merely to pass CI, edit another workstream without coordination, expose rare secret-content details, or promote gameplay VERSION from research-only work. Then continue from the recorded state.**

## Workstream / source ownership

- Persistent branch: **`dev/adaptive-research`**
- Owner: dedicated Adaptive Research / Technology chat
- Canonical research data: `data/research/v1/`
- Research validators: `scripts/validate_research_*.py`
- Research-only workflows: `.github/workflows/research-*.yml`
- **Milestone #13 reserved runtime path: `src/Game/Simulation/Research/`**

Other workstreams may consume stable research events/queries/capabilities but should not directly mutate research internals or edit the reserved runtime path without coordination.

The legacy/prototype gameplay research implementation is **not** automatically replaced by milestone #13. Migration/cutover is a separate explicit acceptance boundary.

## Current public seed

- **360 nodes / 21 domains / 59 Pressures / 16 alternative-solution sets / 14 applicability traits / 9 evidence types / 36 knowledge fields / 17 cross-lineage capabilities**

## Non-negotiable architecture

- one hidden shared Technology Possibility Graph; no fully visible universal tree or giant fixed species trees
- RP from physical Effective Research Labs; Pressure contextual and only a hard gate where explicitly configured
- directed concurrency 1 -> 2 -> 4 -> lab-capacity-only through actual institutional development
- functional dependencies use cross-lineage capabilities when implementation does not matter
- scientific knowledge != physical deployment
- competence = theory / experiment / engineering; facilities/tacit expertise matter; Project Readiness bounded
- foreign technology = Understanding / Operability / Reproduction / Adaptation; acquisition never instantly matures a native node
- technology exchange uses actual records/data/hardware/tooling/experts/training/institutions; legal rights != technical ability
- starting civilizations compose historical fragments; future research remains adaptive
- UI/AI sees only legitimate current horizon; hidden placeholders never render
- agenda/scientific culture guides attention/capacity requests, not direct RP or hidden visibility
- fair AI gets better planning, never hidden graph/enemy tech/free RP/labs/evidence
- runtime sparse/event-indexed; never full graph per simulation tick or per frame
- biochemical traits are composable population context; carbon-water common but not universal; ammonia/cryogenic-hydrocarbon/silicon-mineral/synthetic share the same graph
- multiple biochemical populations may coexist without graph copies
- scientific truth, local codified access, local active practice, and deployment are separate
- distributed contexts exist only for material divergence; synchronized colonies have no explicit research context
- records/data obey real communications; experts/tooling/prototypes/institutions do not teleport as data
- no universal distance research penalty
- successor states inherit real local archives/assets/expertise, not full former-polity technology sets
- classification applies to records/projects/assets, not physics/maturity
- classification has no direct RP multiplier; any slowdown must have real authorized-capacity/validation/compartment/communication causes
- reclassification cannot recall distributed copies or un-leak records
- Intelligence/Security owns espionage/theft/interception/compromise detection/protection; Research owns research-side consequences
- collaboration agreements create permission/coordination, never RP/speed multipliers
- active joint research consumes real participant program/lab/facility/expert/data/material capacity
- one joint project uses one canonical diminishing-return curve; multiple flags cannot bypass it
- partner technology trees never merge and treaty existence reveals no hidden nodes
- true co-developers may advance through normal maturation; passive partners are not automatically Mature
- equal records can still yield unequal operability/reproduction
- withdrawal removes future contribution but does not reverse completed work or recall delivered records
- Diplomacy owns agreement negotiation/payments/rights/breach/political consequences; Research owns scientific contribution/progress/result semantics
- secret/rare discovery details remain outside public data

## Validated milestones

- #11 graph/RP/Pressure/Labs -> `f70e122134e87c1449582b573c5e2db8b045d311`
- #12 emergence/evidence/pressure -> `95fa5c9e77642479eecc8f4183c91c06b3709f7e`
- #13 capabilities/maturation -> `101b01a1d6407fee2912c7e8b9175f196bb75ca9`
- #17 competence/facilities/tacit -> `64a4aaa74ca526c8d6d69b9d905a2e9c3e3a6bc6`
- #30 foreign tech/exchange/UI -> `d7bdaa8ee67461ba1811121e9af4719de6583a8d`
- #44 starting histories/runtime/view -> `859099ee3048a2788aa32b33c5ca46aeaee00df9`
- #53 agenda/culture/fair AI -> `8e47ef537af6d35f8d60e9cf2c9953064d6858ef`
- #59 long-horizon benchmarks -> `d3916d3e6551c7a8b606716858d2d782581b1dac`
- #63 alternative biochemistry/multispecies -> `1e69fa65c2ec0df2feef16bd30794b08f0b2000a`
- #73 distributed scientific continuity -> `6c832fc07359ebb4fbe40c4dc079f19e13c11fca`
- #78 secrecy/compartments/compromise -> `172c9c364b161e2e3deac88337429b5880a34e68`
- #83 joint research/scientific collaboration -> **`b56b47c1f23312abde93e04cd8ac9caf819f0176`**

Research-only milestones do not promote gameplay VERSION.

## Benchmark baselines

Long horizon:
- 500y minimum Mature-tree Jaccard **0.457**
- Mature fractions **25.3% / 29.2% / 35.0%**
- unique Mature nodes **11 / 36 / 68**
- core 1000y node-state counts **89 / 105 / 81**

Biochemistry:
- human 4 shared/0 exotic-native; ammonia 10/6; cryogenic-hydrocarbon 10/6; silicon/mineral 12/8
- exotic native-specific pairwise Jaccard **1.000**

Distributed continuity:
- 1000y/120 regions peaks **5 contexts / 17 node exceptions / 10 field-practice exceptions / 9 transmissions**, final contexts 3
- successor-state Jaccard **0.474**

Secrecy:
- restricted program 48 eligible labs / 14 authorized / multiplier **1.0**
- 1000y peaks **18 security records / 6 compartments / 3 known compromises**

Collaboration:
- 20+12 labs -> correct joint **21.6 scaled units / 2160 RP/year before readiness**; wrong separate-bucket result 29.4 is rejected
- withdrawal 24/18.8 -> 16/16.0 units, completed progress preserved
- canonical xenoscience-containment loss hard-blocks despite 10 labs remaining
- classified partners may collectively integrate a result without either receiving the complete independent package
- 1000y: 112 collaborations created; peaks **4 active / 9 contribution records / 4 pending result deliveries**

## Validation

Core:

```text
python3 scripts/validate_research_catalog.py data/research/v1
python3 scripts/validate_research_maturation.py data/research/v1
python3 scripts/validate_research_competence.py data/research/v1
python3 scripts/validate_research_transfer_ui.py data/research/v1
python3 scripts/validate_research_start_runtime.py data/research/v1
python3 scripts/validate_research_agenda_ai.py data/research/v1
python3 scripts/validate_research_benchmarks.py data/research/v1
```

Specialized:

```text
python3 scripts/validate_research_biochemistry.py data/research/v1
python3 scripts/validate_research_biochemistry_benchmarks.py data/research/v1
python3 scripts/validate_research_distributed_continuity.py data/research/v1
python3 scripts/validate_research_distributed_continuity_benchmarks.py data/research/v1
python3 scripts/validate_research_secrecy.py data/research/v1
python3 scripts/validate_research_secrecy_benchmarks.py data/research/v1
python3 scripts/validate_research_collaboration.py data/research/v1
python3 scripts/validate_research_collaboration_benchmarks.py data/research/v1
```

Then .NET restore/build plus shared Godot process smokes.

## Known CI limitation

**Issue #61:** shared Godot runtime smoke can return success while logging inability to instantiate `res://src/Game/Presentation/Main.cs`. Until fixed, never claim semantic runtime health from that step alone.

## Current milestone

**Milestone #13 — plain-C# Adaptive Research runtime implementation foundation.**

Reserved source path: **`src/Game/Simulation/Research/`**.

Required first implementation slice:

- immutable shared catalog loader + stable-ID indexes
- sparse per-civilization research state; static graph never copied into state/save
- visible maturity state, sparse Pressures/evidence/applicability/capabilities, active directed projects, eligible lab allocation, directed-program stage
- capability and blocker queries before consumers integrate
- event/index-driven candidate wakeups; no full graph scan per tick
- revision-cached visible materialized view
- deterministic C# tests/smoke and serialization guards
- distributed/secrecy/collaboration stay layered sparse modules rather than bloating the base state
- legacy prototype research path remains untouched until an explicit migration/cutover milestone
- gameplay VERSION remains unchanged until accepted integration

## Public-repository secrecy rule

Never publish exact hidden discovery probabilities/triggers, secret artifact chains, hidden special-AI eligibility, secret evidence catalogs, or intentionally undisclosed rare technologies.
