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

Remaining honest limitations: expansion is gated by
organic survey coverage so only the earliest warp-capable civilizations
colonized within the run; pre-warp→warp promotion needs ~120+ years at
this tech pacing; the 50k-system headroom and research/diplomacy/sensor
event feeds are unchanged gaps.

## Follow-up: autonomous warfare coordination (commit `06e88f8b`)

The century runs above all reported `wars: 0` — the strategic evaluator
already computed `recommend_war`, but no production caller existed: the
knowledge provider never filled `has_military_estimate`, no territorial
tension was generated, and nothing issued the canonical `declare_war`
command or military orders.

A new `WarfareCoordinator` phase now runs inside
`IntegratedAdaptiveCampaignRuntime::advance` after diplomacy processing.
On a phase-staggered ~7-day review cadence (step-size agnostic floor-
quotient boundary crossing) each AI-controlled civilization: records
canonical `record_trespass` events for identified foreign military
presence inside its colony systems (skipping already-at-war
counterparts — the war is the fact); builds `KnownCivilization` inputs
from the observer's diplomacy view plus fleet-power observations, with a
deterministic neutral prior at 0.10 confidence when no scanner intel
exists; and calls `StrategicDecisionEvaluator::evaluate_war` — a
`recommend_war` issues `ObserverDiplomacyCommandService::declare_war`.
Every tick, armed fleets at war receive canonical
`issue_engage_hostiles` orders when hostiles co-locate, and idle armed
fleets route to the nearest hostile-occupied system via
`assess_operational_reach` + `assign_fleet_route`. No new persisted
state; stable-id ordering throughout; evaluator weights untouched.

Native coverage: `warfare_coordination` 5/5 — aggressive border contact
declares war and transitions to `at_war`; passive contact under
identical geometry does not; co-located at-war fleet receives `Attack`;
idle at-war fleet deploys toward hostile space; trespass recorded.

Re-run results (2,500 systems / 12 pre-warp + 1 ancient / 10,000 ticks /
~137 years, seed 8374837):

- **wars: 3 organically** — civ 8 declared on civs 3 and 0, civ 5 on
  civ 2 — plus 61 combat events, 77 diplomacy events, 86 fleets (86
  shipbuilding events: wartime militarization under `Defend`), 48
  colonies, 36 trespass events, no journal flooding.
- `repeatFinalStatesDeterministic: true`, final hash
  `e98e42b82e2315932464d861b922ed5819b832ca17bc0a4c5db7526e7a61d56b`,
  step mean 51.5 ms / p95 269.5 ms, warfare phase mean 0.19 ms,
  working set ~117 MB.

Remaining honest limitations: wars are durable — no autonomous
peace/ceasefire proposal-response loop yet (`peace_offer`/
`ceasefire_offer` exist but need a recipient-side evaluation pass);
military estimates without scanner research use the uncertain prior;
engagements require fleet co-location — there is no operational war
plan (no concentration, retreats, or orbital assault); war declares
happen at a 120 ly frontier-adjacency radius, not borders.

Canonical-scale confirmation (5,000 systems / 24 pre-warp + 1 ancient /
10,000 ticks / ~137 years, seed 8374837, organic fleets only, scripted
event definitions loaded):

- **wars: 2 organically** — `Sundered Pact` (civ 3) declared on `Avest
  Dominion` (civ 21) at tick 43260000 and `Dravak Compact` (civ 6) on
  `Omethe Synod` (civ 17) at tick 46290000 — both organic civ-vs-civ
  declarations; plus 36 trespasses, 7 engagement orders, 4 deployment
  orders, 15 combat events, 68 diplomacy events.
- Organic expansion at scale: 51 colonies founded (78 total), 111
  fleets built through canonical shipbuilding, 58 diplomatic contacts,
  35 relationships, 17,600 exploration events.
- `repeatFinalStatesDeterministic: true`; final hash
  `252ad3fd80d4ec2a3d3bfa419ec89fca24dc57b82c473a2a059419cd645e46ab`.
- Step mean 165.4 ms / p95 838.8 ms / peak 3,356 ms — late-game steps
  are dominated by exploration at 5,000 systems once many civilizations
  are interstellar; the warfare phase itself remains sub-millisecond.
  Working set ~217 MB, peak ~366 MB.
- The 100-year canonical run (same scale, 7,305 ticks,
  `--verify-continuation-tick 3650`, `--repeat 2`): deterministic
  (`eb7db25782ec849db581272e97d59546c9706820450058a8d1e2adafe8757387`),
  continuation byte-identical, save 145,173,990 bytes, step mean
  39.9 ms / p95 50.4 ms — but wars/colonies/fleets remain 0 inside the
  century because pre-warp promotion lands just past 100 years at this
  pacing. Organic wars at canonical scale are a ~137-year-horizon
  result, not a century result.

## Follow-up: autonomous war settlement (commit 61de5ba4)

Wars can now end. Two parity-safe seams close the loop:

- **Belligerent contact reacquisition** — an active war or ceasefire is
  continuing mutual contact, so the warfare coordinator re-observes
  every belligerent through the canonical `process_contact_opportunity`
  pipeline when the contact record is missing or `stale_or_lost`. This
  preserves the `war-visible-stale` observer-command parity fixture
  (which pins that `declare_war` on a stale contact is *rejected*) while
  keeping wars settleable: contacts drift stale mid-conflict, and
  observer views are knowledge-filtered so a victim that never
  identified its aggressor would otherwise not even see the war.
- **`StrategicDecisionEvaluator::evaluate_peace`** — additive scoring
  (`evaluate_war` weights untouched): war weariness normalized to ten
  years against the `war_declared` journal event, strength ratio,
  survival priority, hostility/fear. Offers/answers flow through
  canonical `send_proposal`/`respond_to_proposal`; acceptance activates
  `ceasefire`/`peace` agreements. A ceasefire respect window (four
  review intervals) prevents same-tick ceasefire violations.

Re-run results (2,500 systems / 12 pre-warp + 1 ancient / 10,000 ticks /
~137 years, seed 8374837, `--repeat 2`):

- **wars declared: 6** (3 per repeat); **peace offers sent: 158,
  accepted: 8, rejected: 150**; **114 belligerent contact reacquisitions**;
  14 communication channels established.
- Two wars resolved through the canonical **ceasefire → peace** ladder:
  `Kesh Exchange ↔ Tarkesh Reach` (ceasefire tick 41,405,000 → peace
  41,410,000) and pair (2,5) (ceasefire 46,160,000 → peace 46,170,000)
  — agreements recorded in authoritative diplomacy state. One war
  (0↔8) still active at horizon.
- `repeatFinalStatesDeterministic: true`; final hash
  `8b6963c2ccf0df2ba3ebedbd8aa9087fc740b690046e918b591119d6e3aa64c4`.
- `warfare_coordination` 11/11 — new scenarios: stale war contacts
  reacquire → channel opens → settlement flows; freshly accepted
  ceasefire defers redeclaration until the respect window expires.
- Full diplomacy/strategic/campaign parity batch: 26/26 green.

Remaining honest limitations: most offers are rejected (75/79) —
the peace scorer is intentionally conservative; trespass counts rise
post-settlement as parked fleets become trespassers again under
peace; engagement still requires co-location (no operational war plan).

## Performance refresh: canonical run at commit `a6084828`+ (2026-09-28)

Same command as the header run (5,000 systems / 22 pre-warp + 3 ancients /
7,300 ticks / step 5 days / `--repeat 2` / `--stress-fleets 50` /
`--events-root data/events` / `--verify-continuation-tick 3650`), re-run
after the late-game performance arc (lazy exploration mission selection,
indexed per-advance lookups, colonization catalog indexing, strategic
input lazy probe, shared adaptive-research credit-flow index, cached
automatic-orders body index):

- `repeatFinalStatesDeterministic: true`, `continuationDeterministic:
  true`; continuation save 145,103,156 bytes (byte-identical size to the
  pre-refresh run).
- Final hash `f9c5f28cd673ddf6d213db342779cdfa1c920b2891ad910a2ddc26052e20d012`
  — differs from the header receipt because semantic changes (organic war
  settlement, belligerent contact reacquisition, research scheduling)
  landed since `2af8d120`; this hash is the current-commit baseline and
  is **bit-identical across the entire index-sharing arc** including the
  strategic-input and adaptive-research catalog indexes (`c8589b47`).
- **Step mean 5.35 ms** (was 65.31 ms in the header receipt — ~12.2x),
  p95 7.47 ms, peak 42.85 ms; working set 523.2 MB / peak 2,268.7 MB.
- Phase means: adaptive_research 1.67, automatic_orders 1.19, economy
  0.75, exploration 0.53, warfare 0.39, strategic_ai 0.22, combat 0.16,
  construction 0.04, core_total 3.32.
- Metrics unchanged in shape: 25 civilizations / 27 colonies / 150 fleets
  / 0 wars; 4 scripted event definitions / 128 journal entries; research
  37,570 / diplomacy 18 events.

## 200-year endurance extension (2026-09-29)

Same host command with `--ticks 14600 --verify-continuation-tick 7300`
(200 simulated years, mid-run restore at year 100). Two purposes: prove
determinism at 2x horizon and measure late-game step-time growth.

Baseline run (pre dense-route work, capacity-bumped lane cache) and the
final run (dense `RouteTree`, component-membership pruning,
contact-presence index, planning work memos) both produced
`finalStateHash e5a1d5bf768174022efc4bbbb7f058dbc2365ce2e448ea739565c2ddf2f4095a`
with `repeatFinalStatesDeterministic: true` and
`continuationDeterministic: true` — identical flags including
`--events-root data/events` are required for a valid A/B (an events-off
control run produced `a69d93b6…`, a different-but-valid trajectory with
72 colonies / 257 fleets / 0 scripted definitions; byte-identical
autosave checkpoints at ticks 11,680 and 13,140 plus a stashed-binary
A/B confirmed the two code trains are simulation-equivalent).

| Metric | Baseline | Dense-route + presence-index | Δ |
|---|---|---|---|
| Step mean | 76.8 ms | 74.2 ms | −3.4% |
| Step p95 | 344.7 ms | 388.5 ms | +13% (load noise) |
| Step peak | 3,114 ms | 1,486 ms | −52% |
| Peak working set | 3.29 GB | 2.86 GB | −13% |
| `exploration` phase | 46.2 ms | 43.2 ms | −6.6% |
| `strategic_ai` phase | 12.5 ms | 13.0 ms | ~noise |
| `colonization` phase | 8.5 ms | 8.2 ms | −3% |
| `core_total` | 71.8 ms | 69.1 ms | −3.8% |

`STELLAR_EXPL_PROF=1` sub-phase attribution (per advance): mission
`select` remains dominant (~20 ms/selection vs ~24 ms baseline),
`transit` contact cost fell 0.67 → 0.29 ms per warp hop via the
presence index; refuel/recovery are sub-ms. Metrics: 25 civs / 78
colonies / 264 fleets / 0 wars; 4 scripted definitions / 128 journal
entries; 54,094 adaptive-research events; continuation save
~150 MB.

Late-game honest note: step time still grows ~14x from the year-100
mean (5.35 ms) to the year-200 mean (74 ms); the growth concentrates in
per-idle-fleet mission selection. The remaining term is bounded
(heap-ordered lazy assessment + revision-gated work memos) but further
gains need either per-target assess elimination or work-list revision
buckets per civilization.

Follow-up pass (same canonical flags): `find_shortest_route_into`
caller-buffered routes into a per-`OperationalReachBatch` scratch
vector (slot-space predecessor walk, no per-hop hash lookups, no route
vector allocation per pop) plus `ExplorationMissionPlanner::DrainVerdict`
— a cross-call negative-selection memo replaying "no supported target"
verdicts while every verdict input (civ, role, origin, fuel
reserve/capacity, leg range, fuel policy, survey-level revision,
exact refueling-site projection, systems/lane identities) is unchanged.
4,000-tick events-enabled A/B between committed and new binaries is
bit-identical (`63ed7d54606173543b2ce99e69352cc5a36c4a947d6c5c14b91207f267ccd385`);
full suite 291/291.

Canonical 200-year verification on the final binary (exact flags:
`--simulate-adaptive-campaign --events-root data/events --seed 8374837
--systems 5000 --civilizations 22 --ancients 3 --ticks 14600
--step-days 5 --repeat 1 --stress-fleets 50
--verify-continuation-tick 7300` — note `--civilizations 22 --ancients
3` totals 25 civilizations; `--civilizations 25` alone totals 26 and
produces the different-but-deterministic `7e32a1a7…` trajectory):

- `finalStateHash e5a1d5bf768174022efc4bbbb7f058dbc2365ce2e448ea739565c2ddf2f4095a`
  — bit-identical to the dense-route baseline; scratch-buffer routes
  and the drain memo are simulation-neutral.
- `repeatFinalStatesDeterministic: true`, `continuationDeterministic:
  true`; continuation save 149,969,876 bytes (identical size to the
  baseline — same trajectory).
- Metrics identical to baseline: 25 civs / 78 colonies / 264 fleets /
  0 wars; 4 scripted definitions / 128 journal entries.
- Timing: step mean 42.4 ms (vs 57.6 ms at `4b5619ee`, −26%), step
  total 619 s (vs 840 s), exploration phase 21.1 ms mean (vs 29.3 ms),
  step peak 1,271 ms; peak working set 2.85 GB.
- `EXPL-PROF`: select 406.7 s total (vs 773.6 s, −47%); pops/assesses
  25.5M (vs 31.5M, −19%); drains 6,252 (vs 7,776) — subsequent selects
  on unchanged draining fleets replay the memoized verdict instead of
  re-scanning ~4,300 candidates; transit 112.4 s; survey 54.0 s.
