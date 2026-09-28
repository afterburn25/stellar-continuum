# Validation receipt — late-game scale + deterministic save/continue (2026-09-28)

Scope: grand-strategy platform maturity, milestone 3 (scale + save
continuation). Branch `engine/grand-strategy-platform-maturity`, commit
`2af8d120` + doc follow-up.

## Command

```text
stellar-continuum.exe --simulate-adaptive-campaign --systems 5000
  --civilizations 22 --ancients 3 --ticks 7300 --step-days 5
  --repeat 2 --stress-fleets 50
  --events-root <repo>/data/events --verify-continuation-tick 3650
```

5,000 systems · 25 civilizations · 36,500 simulated days (100 years) ·
automation enabled · 4 data-authored event chains loaded.

## Results

| Metric | Value |
|---|---|
| Repeat determinism | `repeatFinalStatesDeterministic: true` (2 independent runs identical) |
| Save→restore→continue | `continuationDeterministic: true` — dev save at tick 3650 (year 50), second runtime restored + advanced to tick 7300, canonicalized save documents byte-identical |
| Final state hash | `25eac6e30fce280f6baad9c992958cc1f3fb3a0eb4def979fd40bf80081c9f66` |
| Initialization | 4,067 ms total / 2,033 ms mean per repeat |
| Step timing | 953.5 s total, mean 65.31 ms, p95 82.51 ms, peak 664.8 ms |
| Memory | working set 518.5 MB; peak 2,254.8 MB (includes verification JSON trees) |
| Save size | 145,102,997 bytes (developer envelope incl. runtime continuation) |
| Civilizations / colonies / fleets / wars | 25 / 27 / 150 (stress-injected on warp-capable ancients) / 0 |
| Economy totals | credits 2,604,204; industry 409,020 |
| Research / construction / industry events | 36,530 / 186 / 321,200 |
| Exploration / diplomacy / colonization / combat | 30 / 18 / 0 / 0 |
| Scripted event definitions / journal entries | 4 / 128 |

## Supporting checks

- `ctest -R "automation|scripted|player_campaign|campaign_coordinator|
  campaign_command|foundation|economy_persist|fresh_campaign|persistable"`:
  14/14 pass, including all C# parity fixtures.
- 5,000-system sanity run (200 ticks, verify at 100): deterministic.
- Save-fidelity bisect probe (uncommitted tooling): save→load capture
  byte-identical after canonicalization; post-restore continuation
  identical for 30 ticks once advisory proposal state persisted.

## Fixes proven by this run

- `AutomationController::State::last_proposals` — advisory top-pick
  suppression round-trips; restored controllers no longer re-journal.
- Scripted-event snapshots embed definition documents — save restores
  its trigger contract without an external data root.

## Honest limitations observed

- Wars: 0 — organic AI war-making does not emerge at this horizon;
  fleets exist only via `--stress-fleets` injection.
- Colonization: 0 events — civilizations do not expand organically.
- Verification peak memory includes the canonicalization parse trees;
  steady-state simulation memory is ~0.5 GB.

## Follow-up: final-slot research scheduling (commit `fedf6bc4`)

Diagnostic inspection of the century run above showed every pre-warp
civilization maturing ~129 nodes yet never reaching
`experimental_interstellar_transit`: at `single_priority_program` (one
directed slot) deep frontier picks repeatedly won the visible shortlist
while gateway prerequisites such as `field_theory` — the root of the
`warp_metric_theory`→`exotic_energy_coupling`→`micro_field_distortion`→
`warp_field_control`→`prototype_warp_drive` chain — starved investigable
for decades. The campaign-simulation AI now defers over-horizon final-slot
candidates while a bounded startable+affordable alternative exists
(commitment horizon `max(2y, min(4x shortest startable, 15y))`), with the
highest-utility deferred candidate still starting when nothing shorter
qualifies. The shared research policy contract and its parity
fingerprints are untouched; the rule consumes only materialized
shortlist fields (`can_start`, `estimated_years_to_mature`).

Re-run results:

- Same command as above minus `--stress-fleets` (organic fleets only),
  `--civilizations 25` + `--ancients 1` (26 civilizations):
  `repeatFinalStatesDeterministic: true`,
  `continuationDeterministic: true`,
  hash `32dad3a5927bd5fb6ddd93b755db66b526136309007f4de1a2fac2dfdf1ce675`,
  step mean 65.25 ms / p95 82.20 ms, working set 524.5 MB.
  ~half the pre-warp civilizations matured 5/6 of the warp chain with
  `prototype_warp_drive` investigable at the year-100 snapshot — the
  chain unrolls but the capstone lands just past the horizon.
- 2,500 systems / 12 pre-warp / 10,000 ticks (~137 years), seed 8374837:
  **11 of 11 pre-warp civilizations promoted to warp-capable
  organically**, 34 fleets built through canonical shipbuilding (11 of
  them loaded colony ships), 11,168 exploration events, 34 diplomacy
  events, 2 in-system colonization orders assigned at snapshot.
  Colonization *events* still 0 — established colonies had not landed
  inside the run; interstellar colony targets additionally require
  explored/surveyed worlds, so organic multi-system colonization remains
  the open edge rather than a proven outcome.

## Follow-up: autonomous colony settlement completion

The 137-year run above also surfaced a colonization-runtime defect: AI
colony ships held same-system orders whose establishment tick fell
through to the opportunity planner, which reset settlement progress and
re-issued the route every tick (`colonization_runtime.cpp` — the
establishment branch only `continue`d on completion, never on progress).
A valid settlement target now consumes the tick for the whole
establishment interval; stale targets still fall through to re-planning.

Native coverage: `colonization_ai_settlement` (same-system order →
transit clear → settlement accumulation → colony creation → vessel
consumption, plus an idle-fleet full-plan scenario); the regression was
verified to fail against the pre-fix runtime.

Re-run results (fixed binary):

- 2,500 systems / 12 pre-warp / 10,000 ticks (~137 years), seed 8374837:
  11/11 pre-warp civilizations promoted organically, 66 fleets built
  (43 colony ships), and **32 colonies founded through the organic
  chain** — `Thren Observatory` (civ 2) founded 17 and `Ilyr Concord`
  (civ 6) founded 15 across distinct surveyed systems. Vessels were
  consumed and re-tasked correctly; expansion is bounded by survey
  coverage, which is why only the two earliest-promoted civilizations
  completed colonies inside the horizon.
  Final hash `5644146d629a2a28fbe84b7eb0c5bb457fde5dafbdf5fccd9b490e3d3156a3b6`,
  step mean 56.3 ms / p95 280.1 ms, working set ~118 MB (run used
  `--ancients 0`; deterministic repeat unchanged for identical configs).

Remaining honest limitations: wars still 0; expansion is gated by
organic survey coverage so only the earliest warp-capable civilizations
colonized within the run; pre-warp→warp promotion needs ~120+ years at
this tech pacing; the 50k-system headroom and research/diplomacy/sensor
event feeds are unchanged gaps.
