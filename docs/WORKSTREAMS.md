# Concurrent Development Workstreams

This file records branch/workstream ownership rules for concurrent ChatGPT development. It exists to reduce branch collisions, duplicate implementation, and cross-chat design drift.

## Current Core-led coordination — 2026-09-08

`integration` is the authoritative accepted shared baseline. Core continues on `work/core-game-integration`; `main` is reserved for separately approved release promotion. The recovery inventory is [BRANCH_INVENTORY_2026-09-08.md](BRANCH_INVENTORY_2026-09-08.md). Historical branch/issue text does not override this current flow.

| Lead | Existing branch / family | Persistent issue |
|---|---|---:|
| Core | `work/core-game-integration`, `work/core-*` | #18 |
| Civilization AI | `work/civilization-ai`, related children | #28 |
| Combat / Military | `work/combat-military`, `work/combat-*` | #40 |
| Diplomacy | `work/diplomacy-first-contact` | #37 |
| Exploration / Colonization | `work/exploration-colonization`, `work/exploration-*`, `work/colonization-*` | #32 |
| Solar Economy / Logistics | `work/solar-economy-logistics` | #26 |
| Species / Races | `work/species-race-mechanics`, `work/species-*`, `work/habitat-*` | #16 |
| Adaptive Research | `research/adaptive-research`; old dev/reconcile/archive refs are evidence | #179 |
| Galaxy / Star-System Visuals | `work/galaxy-star-system-visuals` | #219 |
| UI / Player Experience | `work/ui-player-experience` | #27 |
| Visual Style / Assets | `work/visual-style-assets` | #217 |
| Testing / Release | `work/testing-release` | #29 |

Shipbuilding remains an established simulation owner (#14); its accepted reconciliation branch is preserved. Core coordinates shipbuilding seams with the consuming leads rather than duplicating construction logic.

Each lead resumes its family history in an isolated worktree. Reuse an existing appropriate child. Never assign two coding agents to the same branch concurrently. Re-fetch before publication and use only non-forced updates. Three specialist execution slots are scheduled in waves alongside Core; completed stages remain recorded, not continuously running agents.

Use the workstream issue for detailed progress and #15 for dependencies, interfaces, blockers, regressions and integration requests. Each lead also maintains `docs/handoffs/<workstream>.md`: branch/canonical child, completed and unfinished work, dependencies, changed interfaces, exact tests and limitations, known problems, integration request and next milestone. Core owns shared status/coordination edits.

Preserve observer-safe interfaces throughout AI, presentation and simulation. Civilization-level awareness does not supply a foreign vessel identity. Attack target discovery remains blocked on legitimate observed-vessel knowledge; no owner may substitute raw foreign fleet scans.

## Adaptive Research / Technology

- Branch: **`research/adaptive-research`**
- Scope owner: **the dedicated Adaptive Research chat/workstream**
- Status: active design/data development and isolated plain-C# runtime foundation

This branch owns:

- Technology Possibility Graph and research-domain node data
- Adaptive Research visibility/emergence rules
- Research Points / Research Pressure / Research Labs
- research laboratory capacity and parallel research
- evidence/applicability traits/cross-lineage capabilities used by research
- research maturation, hypotheses, setbacks, side discoveries
- field competence, specialist research facilities, tacit expertise, Project Readiness
- foreign-technology interpretation and research-side exchange/package semantics
- starting research-history/runtime/view contracts
- research agenda/scientific culture/fair-information AI planning contracts
- biochemical/multispecies research applicability
- distributed scientific knowledge/regional continuity
- research secrecy/compartmentalization/compromise interpretation
- cross-polity scientific collaboration contribution/progress/result semantics
- research-specific validation/benchmarks and canonical research documentation
- research-facing materialized-view/query contracts

### Reserved runtime source path — milestone #13+

Adaptive Research additionally owns the dedicated plain-C# runtime path:

- **`src/Game/Simulation/Research/`**

Other workstreams should not independently create/edit files under this path while `research/adaptive-research` is active without coordination.

The runtime path must expose stable event/query/capability interfaces rather than asking consuming systems to reach into research internals.

The initial implementation must remain isolated from the legacy/prototype gameplay research path. Do **not** silently replace or migrate the existing prototype research implementation until a later explicit integration/cutover milestone is reviewed, tested, and accepted.

### Canonical research paths

Primary research-owned paths include:

- `data/research/v1/`
- `src/Game/Simulation/Research/`
- `scripts/validate_research_*.py`
- `.github/workflows/research-*.yml`
- canonical `docs/RESEARCH_*.md` specifications
- `docs/ADAPTIVE_RESEARCH_SYSTEM.md`
- `docs/RESEARCH_ECONOMY.md`
- `docs/RESEARCH_CAPACITY_MODEL.md`
- `docs/RESEARCH_EMERGENCE_MODEL.md`
- `docs/RESEARCH_MATURATION_MODEL.md`

## Rules for other concurrent branches

Other workstreams may **read and depend on** research interfaces/capabilities but should not independently edit the canonical research paths above while `research/adaptive-research` is active.

Examples:

- shipbuilding may ask whether a design has `spacecraft_construction`, `interstellar_transit`, or a specific visible research maturity when the exact implementation matters
- colony/logistics may consume research capabilities and context-scoped applicability
- diplomacy may create/cancel technology deals or scientific collaborations while Research owns scientific contribution/result semantics
- intelligence/security may report legitimately obtained records/observations/compromise facts while Research owns their scientific interpretation
- species/population may provide biological/context facts that map to research applicability
- communications may provide path/latency/bandwidth/security facts for distributed research without owning research knowledge state
- UI may consume materialized visible research snapshots without querying the hidden graph directly

If another branch needs a research schema/runtime change, record/request the needed interface and let the Adaptive Research branch implement the research-side change. This avoids competing internal representations.

## Capability interface rule

Other systems should prefer functional **capabilities** when they do not care which technological lineage produced the result.

For example, logistics should ask for reliable `interstellar_transit` rather than hard-coding `stable_warp_drive` unless the feature is specifically about warp technology.

This keeps alien, biological, synthetic, foreign, hybrid, and future secret technological lineages interoperable.

## Runtime integration rule

Owning systems send factual events into Adaptive Research and consume stable queries/output events.

They should not directly mutate:

- node state
- hidden visibility/emergence candidates
- research Pressure/evidence internals
- competence/tacit state
- classified/distributed/collaboration research state

Similarly, Adaptive Research must not take over physical construction, population migration, communications routing, diplomacy negotiation, espionage/security actions, or presentation implementation. It consumes those systems' factual outputs through the established contracts.

## Merge discipline

- All specialist milestone PRs target `integration` after specialist validation and Core review. Testing/Release independently validates the combined result. Promotion to `main` requires separate explicit release readiness and validation; it is not authorized by the current Core recovery session.
- Shared Godot process smokes remain subject to known issue #61 until Testing/Release repairs the false-positive runtime gate.
- A research design/data/runtime-foundation merge does not promote gameplay VERSION by itself.
- Continue from the existing canonical research lineage. Preserve unique research work and inspect integration differences before any non-destructive synchronization; do not recreate the mixed integration lineage.
- Do not create a permanent new research branch for each feature unless a temporary recovery/experiment branch is specifically needed.

## Ownership is organizational, not a permanent code wall

Once Adaptive Research is intentionally integrated into gameplay, implementation may need coordinated edits to shared simulation APIs. Branch ownership exists to coordinate those edits, not prohibit necessary integration.

When cross-workstream source changes become necessary, keep the research schema/meaning authoritative in this workstream, document the interface, and coordinate the consuming branch around that boundary.
