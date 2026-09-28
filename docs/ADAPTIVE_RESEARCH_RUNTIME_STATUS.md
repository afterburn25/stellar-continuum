# Adaptive Research Runtime Status

This research-owned record tracks executable Adaptive Research runtime milestones separately from gameplay VERSION. Adaptive Research is now the authoritative playable-campaign path; the legacy model remains only as a temporary capability compatibility projection and for isolated migration tests.

## Current accepted runtime baseline

- Repository: `afterburn25/stellar-continuum`
- Research branch: `dev/adaptive-research`
- Public possibility graph: 370 nodes / 21 domains
- Gameplay VERSION remains `0.0.6-dev.1`
- No Core gameplay research cutover yet

### Milestone #13 — executable sparse runtime foundation

Merged through PR #86 at `2b5e1e30783db67524fac3788552f667c69879c0`.

Established immutable catalog/index loading, sparse per-civilization visible research state, authoritative eligibility/blockers, Effective Research Lab allocation, staged RP progression, visible-only views, starting-history composition, hypothesis resolution, and standalone research snapshot v1.

### Milestone #14 — competence, institutions, tacit expertise and authoritative readiness

Merged through PR #115 at `0aea9db86f74e95084e0f26430bc223afee13a5c`.

Established 36-field theoretical/experimental/engineering competence, specialist research institutions, scoped tacit knowledge, causal Project Readiness, competence growth/atrophy, `AdaptiveResearchAuthority`, and snapshot v2.

Validated human-like 2050 reference metrics:
- 19 active competence fields
- 12 Effective Research Labs
- Fusion Experimental readiness 70.1/100
- equal raw four-lab readiness: general laboratory pool 65/100 vs matching high-energy complex 100/100

### Milestone #15 — causal Research Pressure and fair strategic planning

Merged through PR #141 at `7eb1ceb96a8487beccacebc805071093ad8f436d`.

Established causal runtime for all 59 Research Pressure rules, sparse metric/event support, five-level research agenda, 12-axis scientific culture, visible-only bounded AI/player shortlists, natural complacency/challenger response, and snapshot v3. No hidden tech rank, catch-up multiplier, or leader penalty.

Validated milestone #15 metrics:
- focused pressure test: 1 active support record
- visible shortlist: 12
- 0.9 sustained energy-shortage support -> Pressure 40 after one year
- Deprioritized military agenda at complacency index 95 -> Critical at legitimate challenger index 90
- human strategic v3 snapshot: 17,764 bytes
- 1,000-year four-signal soak: peak 2 active Pressure records / 1 live metric signal / 1,413-byte final snapshot

### Milestone #16 — executable foreign-technology assimilation and brokerage

Merged through PR #158 at `9fefe82fd6fe79cfd081f0e54655070b0907c3c2`.

Established four independent foreign-technology axes (Understanding / Operability / Reproduction / Adaptation), sparse assessments/packages, evidence/tacit package ingestion, law-vs-physics transfer rights, explicit assimilation, recipient-specific brokerage value, native derivatives without source cloning, and backward-compatible snapshot v4.

Validated milestone #16 metrics:
- sparse assessments: 2
- held packages: 2
- captured drive: Observed / SupportedOperation / ComponentReplication
- foreign reactor: EngineeringUnderstood / NativeDerivative
- incompatible-holder brokerage utility: 98.3/100
- v4 snapshot: 14,527 bytes
- after 1,000 years repeated reassessment: still 2 assessments / 2 packages / 14,527 bytes

### Milestone #17 — foreign discovery materializes the native visible tree

Merged through PR #165 at `38c279e498dd5695483c9680df17afbec7b3d254`.

Established:
- foreign evidence can reveal existing public nodes as Rumored/Hypothesized without granting researchability
- only normal scientific eligibility promotes to Investigable
- evidence-specific candidates use the existing evidence index
- generic cross-lineage methods use only four explicit bounded rules
- no full graph scan on foreign contact
- Rumored/Hypothesized nodes hide project lab costs and capability outputs through the existing view contract
- foreign-aware hypotheses can later promote normally when real prerequisites/evidence become available
- operational/biological incompatibility does not erase legitimate scientific awareness
- v4 persistence already stores these awareness nodes because they use ordinary sparse node state

Validated milestone #17 metrics:
- identical human-like starts: 82 visible nodes each before contact
- one characterized alien-drive contact: contacted tree 82 -> 89; control unchanged
- bounded contact growth: +7 visible nodes
- Foreign Device Forensics: Investigable -> Mature through normal RP
- Reverse-Engineering Methodology: Hypothesized -> Investigable after Forensics Matures
- Cross-Lineage Engineering: Hypothesized
- Hybrid Technology Design: Hypothesized
- incompatible synthetic civilization: Xenobiological Compatibility Science Hypothesized but still applicability-blocked
- contacted v4 snapshot: 26,275 bytes, sparse against the then-360-node graph

All 11 milestone #17 workflows passed on exact PR head `5c3260c9f356a5cb704ed884e6a9812c440ca910`; dedicated foreign-discovery checks built with 0 warnings / 0 errors. `VERSION` remained `0.0.6-dev.1` and no legacy prototype research source changed.

### Milestone #18 — foreign-derived and hybrid engineering branches

Merged through PR #168 at `23cfeacd4556d85b1b4c8b402bde6e635c7d1ccd`.

Expanded the shared public graph from 360 to 370 nodes while remaining at 21 domains. Xenoscience expanded from 14 to 24 nodes with:
- six foreign-derived engineering branches: propulsion, power, materials, manufacturing, control systems, biosystems
- four deeper hybrid architecture branches: propulsion, power, manufacturing, biosystems

Rules now enforced:
- legitimate foreign evidence/contact can reveal derivative branches but does not grant maturity
- derivative branches require real cross-lineage/source-analysis knowledge
- deep hybrid branches are not revealed directly by observation/contact
- hybrid branches require Hybrid Technology Design plus mature derivative knowledge
- no exact source replication is required unless a node explicitly models replication
- every derivative/hybrid node uses ordinary RP, Effective Labs, Pressure, Project Readiness, facilities, evidence, and maturation
- uncontacted civilizations do not inherit another civilization's foreign-derived tree

Validated milestone #18 metrics:
- catalog: 370 nodes / 21 domains
- test research capacity: 92 Effective Research Labs
- 19 prerequisite/branch projects matured normally in the recursive progression fixture
- Foreign-Derived Propulsion Engineering -> Mature through normal research
- Hybrid Propulsion Architecture -> Mature only after Hybrid Design + derivative maturity
- exercised path materialized 7 derivative/hybrid nodes; uncontacted control materialized none of that propulsion path
- all 12 research/build workflows passed exact head `ff925c652b95829d8e138d3818cfd44132bffaeb`
- dedicated foreign-derived checks built with 0 warnings / 0 errors

The graph expansion intentionally tripped the secrecy and biochemistry 360-node baseline guardrails. They were updated to 370 while preserving their substantive limits and then passed.

## Long-horizon baseline

- 500-year same-origin Mature-tree minimum Jaccard distance: 0.457
- 1,000-year core research-state soak remains bounded
- no hard year limit in the research architecture

## Known shared CI caveat

GitHub issue #61 remains outside Adaptive Research ownership: the shared Godot runtime process can exit successfully while logging failure to instantiate `res://src/Game/Presentation/Main.cs`. Do not use that process exit code alone as proof of semantic runtime health.

## Milestone #19 — experimental outcomes, setbacks, disproofs and side discoveries

Implemented and validated in the maintained `AdaptiveResearchOutcomeChecks` console project. The v5 snapshot codec preserves deterministic outcome summaries and bounded recent history, including supported, partial/refined, disproven, setback, hazard and side-discovery results.

## Playable campaign integration foundation

The playable campaign now owns one species-compatible Adaptive Research state for every civilization. All species share the 370-node possibility graph while their historical start composition reflects Terran, pelagic high-pressure, compact high-gravity, or cryogenic hydrocarbon conditions. Campaign format v15 persists the existing v5 state per civilization alongside Diplomacy, preserves the exact inner galaxy catalog version, migrates v9/v11/v13 campaigns by composing bounded starting history, and rejects identity/catalog mismatches. Player and Developer sessions both retain the live state through autosave and recovery.

The campaign clock now advances active Adaptive Research programs in years derived from accepted simulation days and resolves pending hypothesis outcomes deterministically from the campaign seed. The graphical Research page reads the observer-safe visible horizon, starts projects through the authority facade, reports stage progress/readiness/lab assignments, and displays finite free/total Effective Research Labs instead of an accumulating Science stockpile. Mature Adaptive nodes grant the matching temporary legacy capability flags used by construction, shipbuilding and demo objectives; the bridge is one-way and Adaptive Research remains authoritative.

Integrated campaigns now disable legacy research stepping. Non-player civilizations select their own fair-information programs through the Adaptive agenda shortlist, while the player remains entirely command-driven. The first-colony guide and Research page share a reachable early-campaign priority path. Pre-warp civilizations translate the real interstellar-distance barrier into research pressure, and a completed Warp Test Facility supplies the specialist capabilities required by Prototype Warp. A maintained Core regression follows the complete 2050 path to experimental interstellar transit and proves the physical facility gate cannot be bypassed.

Early playable balance now generates 400 RP per Effective Research Lab per year. The maintained calendar regression follows all thirteen ordinary projects, brings the physical Planetary Research Network online after 125 days, and reaches experimental interstellar transit in about 15.7 in-game years with the reference Human start; it fails if the route exceeds 20 years. The prior 100-RP seed value took about 70.4 years. AI projects now carry their civilization's primary species context so population-scoped results mature without an invalid unscoped capability.

The Planetary Research Network now creates a fixed four-unit general-laboratory institution in the owning civilization's Adaptive Research state. Synchronization is idempotent across later simulation steps and save/load recovery, so physical construction increases finite Effective Research Lab capacity without creating an accumulating science balance.

Powered surface Science Labs now materialize location-linked research institutions: a basic lab supplies 1 Effective Research Lab and an advanced campus supplies 2.5. A three-building Research District applies its existing 25% capacity bonus. Power loss, demolition and upgrades replace or remove those institutions on the next accepted simulation step, while save recovery remains idempotent.

Construction, shipbuilding, strategic AI, system infrastructure markers, department choices and campaign objectives now consume the same Adaptive Research state directly. Orbital Manufacturing materializes the registered Orbital Industry capability; Controlled Warp Field construction requires established Adaptive knowledge; first-generation ship designs require Adaptive orbital/spacecraft construction plus Experimental Interstellar Transit. Maintained validation poisons the retired legacy flags and proves they cannot unlock integrated gameplay early. The live one-way legacy flag bridge has been removed; Adaptive Research now projects only the broad Warp Capable civilization stage needed by the rest of the game. Prototype adapters remain for isolated legacy simulation and migration tests.

## Historical milestone #19 plan

**Milestone #19 — experimental outcomes, setbacks, disproofs and side discoveries.**

Make the existing maturation/outcome design executable so research history can reshape the visible tree instead of every project being a guaranteed straight-line purchase.

Requirements:
- ordinary established engineering is not randomly invalidated; setbacks cost time/work or require additional evidence/facility changes without arbitrary total reset
- genuine hypotheses can resolve as supported, refined/partial, or disproven
- disproven hypotheses archive as scientific history and do not immediately reappear as the same active hypothesis
- failed/disproved work still grants bounded competence/evidence/side-discovery value where scientifically justified
- partial results can materialize existing public nodes as Rumored/Hypothesized/Investigable only through explicit side-discovery rules; never reveal arbitrary hidden graph regions
- side discoveries use stable node/evidence/field relationships and bounded indexes, not a full graph scan
- outcome selection must be deterministic/replayable for a campaign seed plus experiment identity while preserving uncertainty to the player before resolution
- player/AI can improve odds/containment through real readiness, evidence, facilities, rigor and relevant safety methods; culture changes willingness to take risk, not physics after the experiment is committed
- hazards must be explicit outputs for the owning physical systems rather than Research inventing colony/fleet damage itself
- snapshots must preserve resolved outcomes/history without unbounded event growth
- add executable validation for supported, partial/refined, disproven, setback and side-discovery cases plus a 1,000-year bounded outcome-history soak

Do not implement Combat/colony damage, political scandal, espionage or economic consequences in Adaptive Research; emit factual outcome/hazard events for owning workstreams.
