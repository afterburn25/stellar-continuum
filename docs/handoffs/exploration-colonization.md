# Exploration / Colonization handoff

Updated: 2026-09-08, recovery of the existing shared-body-resolver milestone.

## Ownership and recovered branch state

- Workstream: `work/exploration-colonization`; its previous planetary-body milestone at `a213cc771bf206c70f2b899f670dc267f552d945` is accepted history, not abandoned work.
- Current canonical child and current working branch: `work/colonization-shared-body-resolver`.
- Recovered child tip: `08b846311a1c1788567a297853ef33ceaa0b8815`, nine unique commits and 309 accepted commits behind the recovery baseline.
- Accepted baseline: `c529a1a765776c0940f88002410bc70db740d05a` on `integration`.
- Non-destructive recovery merge: `633653e85eba890ccc97b151b1244052cea4ec1b`. No conflicts; all nine child commits and all accepted baseline history remain reachable. No branch was replaced, deleted or reset.
- Scope recovered from issue #32 comments, branch inventory, unique commit/diff review, and the architecture/engineering guardrails. The current user's `integration` acceptance flow supersedes stale documents that still describe everyday promotion through `main`.

All 19 established Exploration/Colonization family branches were inventoried. Classification is relative to the accepted recovery baseline:

| Branch | Status |
| --- | --- |
| `work/exploration-colonization` | ACTIVE; previous milestone integrated |
| `work/colonization-shared-body-resolver` | SPECIALIST CHILD; canonical continuation, ready for Core review |
| `work/colonization-friendly-reservations` | INTEGRATED |
| `work/colonization-legacy-order-availability` | INTEGRATED |
| `work/colonization-local-reservation-viability` | INTEGRATED |
| `work/colonization-mission-deconfliction` | INTEGRATED |
| `work/colonization-opportunity-actions` | INTEGRATED |
| `work/colonization-opportunity-planner` | INTEGRATED |
| `work/colonization-opportunity-ui` | INTEGRATED |
| `work/exploration-ai-deconfliction` | INTEGRATED |
| `work/exploration-arrived-colony-body-view` | INTEGRATED |
| `work/exploration-local-survey-contact` | INTEGRATED |
| `work/exploration-mission-planning` | INTEGRATED |
| `work/exploration-mission-status` | INTEGRATED |
| `work/exploration-observation-confidence` | INTEGRATED |
| `work/exploration-planetary-bodies` | INTEGRATED |
| `work/exploration-science-signature-events` | INTEGRATED |
| `work/exploration-survey-coverage` | INTEGRATED |
| `work/exploration-survey-operations` | INTEGRATED |

The recovery review also includes the accepted historical `dev/0.0.3-exploration` and `dev/0.0.4-colonies-economy` foundations; neither should be revived. Exploration-named children under `work/civilization-ai-*` belong to the Civilization AI lead, and accepted `work/core-ai-canonical-exploration-work` remains Core history.

## Completed milestone and interfaces

`ColonySettlementBodyResolver.ResolveBestAvailableBody(galaxy, civilizationId, systemId, speciesId)` centralizes the existing deterministic body-less settlement choice. Colonization system-level orders/founding and Exploration read/status surfaces consume it. It requires the acting civilization's completed survey, an unoccupied system under the accepted single-colony rule, and Species-owned colonization viability. Ranking remains viability, natural habitability, unprotected operational capacity, then stable body ID.

The behavior correction is in the read model: a body-less mission now displays the same species-relative world that settlement status and founding choose, instead of the first low-ID legacy compatibility world. The resolver uses embarked population species, including when that differs from the fleet owner's civilization species. Read-only calls add no persistent state and do not set mission intent.

Exact `DestinationPlanetaryBodyId` remains authoritative. A lower-ranked exact world remains selected; an invalid exact ID remains blocked instead of falling back. Exact intent survives system arrival. Idle body-less ships at an occupied colony have no inferred settlement target. Colony founding preserves passenger population/species and consumes the colony ship exactly as before. Existing colony compatibility-body resolution and save formats are unchanged.

The recovered production diff is the existing four source files; this recovery adds boundary regression coverage and this handoff. No new production regression was found during recovery, so no unrelated source changes were made.

## Validation and known limitations

- Package-free compilation used the installed .NET Roslyn compiler and .NET 8 reference assemblies against all actual simulation, persistence, campaign and plain diagnostics source files plus `tests/Game.Simulation.Validation`. No simulation stubs or replacement model implementations were used. Godot presentation and its Godot-dependent support logger were excluded.
- The recovered source passed all 22 central simulation tests and 51 module-initializer validation groups (73 PASS lines). This includes exact-body save/load, v6/v7 migration, population conservation, species suitability, local reservations, order availability, observer survey confidence, directional first contact and military information boundaries.
- After extending the boundary fixture, a caught top-level managed console runner passed both shared-resolver and arrived/idle read-model validations against actual sources. New checks cover passenger versus owner species, another civilization's full survey, reconnaissance withholding, invalid exact intent without mutation, lower-ranked exact founding, exact population transfer and newly occupied-system rejection.
- Local evidence is retained in the recovery workspace's `work/exploration-pure-checks/`: `compile.rsp`, `compile.log`, `run.log`, `boundaries.rsp`, `boundaries-compile.log`, `boundaries-run.log`. Executions used `dotnet` with managed DLLs, not standalone executable helpers.
- Full Godot/NuGet restore and Godot editor/runtime gates remain unavailable in this environment. The existing nullable-analysis warnings in mission status, opportunity planning and a Diplomacy test remain. This package-free result is not a claim that Godot smoke tests or the complete release workflow ran.

## Dependencies and next recommended work

### Combat: observer-specific vessel identity contract

Issue #32 already records Combat's dependency on legitimately observed individual targets. `ExplorationEvent.FleetId` is the observing civilization's own ship; `TargetCivilizationId` identifies a polity. Diplomacy's `civilization:<id>` contact key is also polity-level. Neither identifies a particular foreign vessel. First-contact discovery may originate from a colony and stops once that civilization is known, so it cannot safely double as repeated vessel detection.

Proposed next bounded contract, requiring Core/Combat agreement before implementation:

1. Exploration/sensor authority creates an opaque observer-local vessel contact token only after an actual observation identifies one physical foreign vessel. A civilization contact, detected star, aggregate military pressure, or a colony observation must never generate it.
2. The public observation includes the observer, token, legitimate last observed system/location precision, observation tick, confidence and current/stale/lost condition. Foreign civilization identity and physical descriptors appear only when actually identified. Raw authoritative foreign fleet IDs and hidden destination/role/strength stay out of player/AI read models.
3. An internal command-side resolver maps `(observer, token)` to a still-permitted physical target. Core revalidates observation authority and token lifetime at preview and issuance, then hands the internal ID to Combat's existing target/range/hostility rules. Stale, destroyed, unknown and other-observer tokens must have indistinguishable invalid-target outcomes where distinguishing them would leak hidden facts.
4. Observation memory needs bounded capacity, expiry/reacquisition rules, deterministic token identity and save/load continuity. The persisted-versus-reconstructible decision and age semantics must be agreed before making tokens command-facing; there is no such persistence contract today.
5. Regression requirements: directional observation without reciprocity, colony-only contact produces no vessel token, already-known foreign civilizations can still yield newly observed vessels, hidden vessel changes cannot change another observer's view, cross-observer token rejection, stale/invalid generic rejection, and deterministic save/reacquisition behavior.

No vessel sensor or Attack target-discovery implementation is included in this recovery. Combat should keep proactive target discovery unavailable until this contract exists.

### Galaxy / UI: selected-system exploration projection and revision

Core reports that the current Galaxy candidate bounds refresh to one second but still calls full `ExplorationReadModel.Build`. Its next dependency is an observer-scoped selected-system read contract and a revision/invalidation contract, exposed through the shared simulation coordinator. The selected system should materialize only legitimately known facts and owned relevant mission information. Revision changes must cover the actual knowledge/mission inputs and campaign replacement; consumers must not invent partial invalidation or access hidden state to maintain a cache. The existing whole-galaxy build remains unchanged in this wave. No timestamps, revision state or cache are persisted here.

### Logistics

`IInterstellarOperationalReachView` remains the accepted consumer seam. The default `PrototypeInterstellarOperationalReachView` is intentionally provisional; Solar Economy/Logistics must supply authoritative mission reach/endurance support. Exploration/Colonization must not invent a parallel distance/fuel/supply policy.

## Integration request

Core should review and merge the current committed head of the existing `work/colonization-shared-body-resolver` child into `integration`, retaining the recovered history. Request independent Testing/Release verification of the resulting shared head, including Godot and supported save migrations when the runtime environment is available. No direct `integration`/`main` changes or remote pushes were made by this specialist.
