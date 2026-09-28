# Species / Races handoff

Recovered and validated 2026-09-08 against accepted `integration` commit `c529a1a`.

## Current continuation

- Current/canonical child: `work/habitat-support-capability-foundation`.
- Its existing remote tip `f94bfaa` was fast-forwarded locally to `c529a1a`; no replacement branch, reset, or history rewrite was used.
- Established top-level `work/species-race-mechanics` remains intact at `0bfeb63`. Its pre-aggregate implementation is superseded, not a pending integration candidate.
- This stage fixes one test compilation ambiguity and validates the accepted Species, persistence, population, and habitat-burden boundaries. No simulation interface or save format changes.

## Recovered family classification

All tips below were compared with `c529a1a`, including commit histories and existing Species documents. Except the old top-level, every recovered tip is an ancestor of that accepted baseline.

| Existing branch | Recovered tip | Classification / disposition |
| --- | --- | --- |
| `work/species-race-mechanics` | `0bfeb63` | SUPERSEDED; retain its nine unique historical commits |
| `work/species-v8-latest-sync` | `3a77c04` | INTEGRATED; historical staging child |
| `work/species-race-mechanics-followup` | `0804922` | INTEGRATED; species-relative exact-body readiness |
| `work/species-race-persistence-hardening` | `b10b5fb` | INTEGRATED; population reservation save hardening |
| `work/species-demographic-pressure` | `fc5b0b2` | INTEGRATED; life-history population pace |
| `work/species-fleet-crews` | `3db72e3` | INTEGRATED; reconstructible fleet crew physiology |
| `work/species-fleet-biological-load` | `99e3bac` | INTEGRATED; crew/passenger biological demand |
| `work/species-compatible-homeworlds` | `c3b657c` | INTEGRATED; physical natural homeworld planning |
| `work/species-environmental-demographics` | `a93fd36` | INTEGRATED; exact-body environmental population pressure |
| `work/species-habitat-support-burden` | `c2472ef` | INTEGRATED; raw colony requirements |
| `work/species-habitat-burden-aggregation` | `62ef3f4` | INTEGRATED; civilization/system aggregation |
| `work/habitat-support-capability-foundation` | `f94bfaa` | ACTIVE / SPECIALIST CHILD; accepted baseline and newest existing continuation |

The capability-foundation branch name is a continuation point, not evidence that an authoritative habitat-support capacity or upkeep system already exists. The accepted contract currently describes raw burden and its aggregation.

### Why the original top-level must not be replayed

[Issue #16's accepted follow-up](https://github.com/afterburn25/stellar-continuum/issues/16#issuecomment-5577485635) records the original Species/save-v8 milestone accepted via PR #103 (`ed2f567`), retirement of PR #21, and continued work from accepted integration through PR #109.

The nine commits unique to `0bfeb63` were inspected individually:

- `67b49d1` and `0bfeb63`: readiness implementation/test files are byte-identical to accepted `0804922` at the follow-up milestone.
- `2dde55c`, `16c2f3f`, and `22b18d1`: historical explicit-body mission composition and temporary synchronization. Accepted integration retains explicit body identity and adds arrived-mission continuity and confidence metadata; restoring old composition would regress it.
- `2fb2756`, `608c998`, and the Program portion of `22b18d1`: temporary shared validation alignment, superseded by current validation registration.
- `a221a0a`: extra shared regression calls are registered on integration through `CombatRepairDemandSyncValidation`, `CombatRepairApplicationSyncValidation`, `MissionPlannerSyncValidation`, and `ExplorationMissionStatusSyncValidation`.
- `8fbdec3`: historical mixed synchronization merge, retained as history.

No stranded production behavior was found that warrants merging these commits.

## Completed validation and fix

`tests/SpeciesMechanicsChecks/Program.cs` now calls `Enumerable.Reverse(detailedTerranCohorts)` explicitly. The installed SDK 10.0.302 compiler with latest language selection otherwise resolves array extension syntax to span reversal returning `void`, producing CS1503. The explicit call preserves the existing non-mutating input-order determinism assertion and also works with the project's .NET 8 target.

An offline source-linked harness compiled actual production simulation/persistence sources and unchanged test sources using SDK 10.0.302, .NET 8 reference assemblies, and the .NET 8 runtime. A separate caught reflection runner invokes the test entry through `dotnet`; no standalone test executable was launched. All checks passed:

- Full existing `SpeciesMechanicsChecks`: four prototype species, biology, supported habitats, bounded cohort reduction, adaptation, deterministic assignment, scalar population bridge, v8 round trip, v7 migration, and unknown-species rejection.
- Natural homeworld planning/founding-body anchoring across the existing seed set, crew/passenger biological separation, and combined fleet demand.
- Exact-body and legacy-unknown environmental demographic pressure, raw colony burden, and civilization/system aggregation conservation, determinism, and non-mutation.
- `ShipyardPopulationReservationSerializationValidation`: v8/v9 reject invalid active/queued designs or queue overflow that would discard reserved population; harmless zero-population overflow remains bounded.
- `SpeciesCombatSaveValidation`: shared v8 Combat state round trip and v7 defaults.
- `SpeciesPlanetaryReadModelValidation`, `ColonySpeciesEnvironmentValidation`, and `SpeciesDemographicEconomyValidation`: surveyed-only suitability, read-only environmental facts, and Species effects on population pace without direct productivity bonuses.
- `git diff --check` passed.

Local evidence remains in workspace `work/species-pure-checks/compile.rsp`, `compile.log`, `runner.rsp`, `Runner.cs`, and `run.log`. The harness adds only global imports, production source references, and an invocation of the existing Combat save check; it does not replace simulation services or rewrite test assertions.

Limitations: this is a real plain-C# subsystem validation, not a full Game.csproj/Godot build, engine smoke, visual, or full campaign-runtime certification. Full dependency restore remains environment-blocked. Compilation reports six existing nullable warnings in colony burden tests and Colonization/Exploration sources; no warning was hidden. Engine validation remains a release gate.

## Dependencies and preserved ownership

Economy remains the current scalar population owner; authoritative multi-species cohorts must not be layered beside `ColonyState.PopulationMillions`. Colonization owns settlement and mission intent, Shipbuilding owns reservation transfer/queues, and Core owns campaign persistence composition. Species supplies biological facts and demographic factors.

Legacy colonies without an exact body retain unknown environmental requirements. Crew and embarked passengers retain separate identities. No new support supply, costs, research unlocks, or observer access were introduced. Any next support-capacity interface needs Core agreement with Solar Economy / Logistics and Colonization; Research may expose capabilities without taking over operational supply.

## Integration request and next priority

Core may integrate this narrow compiler-compatible test call and handoff from the existing canonical child after review. No merge of the retired original top-level is requested; no direct `main` promotion is appropriate.

[PR #128](https://github.com/afterburn25/stellar-continuum/pull/128) was independently verified open on 2026-09-08. It is an old synchronization PR from `integration` into `work/species-race-persistence-hardening` at `b10b5fb`, not pending unique hardening work destined for integration. That base tip is already an ancestor of `c529a1a`; all useful hardening is accepted. Recommend Core close the obsolete synchronization PR while preserving its branch. This specialist did not close or modify the PR.

Next priority: restore and run the full shared build/engine gate, then have Core decide whether the early playable loop actually requires operational habitat support. If required, continue this existing child with a bounded capacity/reliability contract jointly agreed with Logistics and Colonization before removing the prototype supported-world fallback. No speculative support economy or population-owner replacement is underway.
