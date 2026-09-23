# Simulation Scheduler + Simulation LOD

Reusable engine layer for civilization-scale simulation pacing.
Modules: `engine/include/stellar/engine/simulation_scheduler.hpp`,
`engine/src/simulation_scheduler.cpp`,
`engine/include/stellar/engine/simulation_executor.hpp`,
`engine/src/simulation_executor.cpp`.
Tests: `simulation_scheduler`, `simulation_executor`,
`simulation_scale_250/500/1000/2500/5000` (ctest).

## Model

One **task** per simulatable thing — a star system, colony, fleet,
economy node, AI planner — keyed by a stable `uint64` the owner chooses.
Each `advance()` is one authoritative tick:

1. **Eligibility** — items due at their tier cadence, plus dirty and
   event-woken items.
2. **Ordering** — topological over `depends_on` (dependencies that are
   not due this tick do not gate; they only order concurrently-due
   work), ties by `JobPriority`, then **aging** (largest elapsed first),
   then key. Fully deterministic across runs and platforms — due lists
   are sorted, unordered-map iteration never leaks into ordering.
3. **Budget** — `SimulationBudget{max_wall_time, max_tasks}` caps a
   tick's work. Deferred items are not marked ran, so their `elapsed`
   keeps accumulating and they re-enter eligibility next tick —
   catch-up arrives as `elapsed_ticks > 1`, which coarse tasks must
   integrate. Aging makes sustained budget pressure starve no key.
4. **Execution** — `advance()` runs inline in deterministic order;
   `advance_parallel(JobSystem&)` groups the tick into dependency waves
   and submits each wave as tagged/prioritized jobs, waiting between
   waves.

## Simulation tiers (the LOD axis)

`SimulationTier` doubles as the simulation-LOD level. Default periods:

| Tier | Cadence | Domain name | Intended content |
| --- | --- | --- | --- |
| Active | every tick | REALTIME/TACTICAL | viewed system, engaged fleets, active UI |
| Nearby | every 2 | LOCAL | adjacent/observed systems |
| Normal | every 4 | STRATEGIC | established colonies, routine economy |
| Background | every 16 | BACKGROUND | quiet mature systems, distant economy |
| Dormant | event-driven | DORMANT/EVENT-DRIVEN | unobserved systems — accumulate elapsed, run on `wake()`/`mark_dirty()` or owner bulk propagation via `dormant_items()` |

Owners promote/demote with `set_tier` or bulk `evaluate_tiers(fn)`
(deterministic sorted-key iteration). A task's tier expresses *how
often* it runs; its `domain` string ("economy", "colony", "fleet",
"population", …) expresses *what* it is and groups timing stats.

LOD preserves game-significant state by construction: coarse and dormant
items always receive their true accumulated `elapsed_ticks`, so a
Background economy tick integrates 16 ticks of production, and a Dormant
system woken after 400 ticks sees `elapsed=400`. Nothing is approximated
at a finer cadence than the owner chooses — cadence changes cost, never
results.

## Wakeups

- `mark_dirty(key)` — one-shot dirty flag: runs next tick regardless of
  cadence, then clears (consumed only when the task actually runs —
  deferred items keep the flag).
- `wake(key)` — one-shot event wakeup; on a Dormant item runs it once
  with accumulated dormant elapsed and consumes the accumulation without
  leaving the tier.
- `wake_domain(domain)`, `wake_all()` — bulk event wakeups.

## Pause / acceleration

`set_paused(true)` freezes the tick — `advance()` returns a paused
report without incrementing the clock. Speed/acceleration belongs to the
caller's clock (core `StrategicClock`): the executor only sees ticks.

## Observability

`SimulationStepReport` per advance: eligible/ran/deferred counts, dirty
and event wakeup counts, tick wall time, jobs submitted.
`domain_stats()` per domain: runs, total/max/last ns. `tick_history()`
retains the last 1024 tick wall times for percentiles.
`total_wakeups()`, `tier_counts()`.

## Determinism contract

Eligibility, ordering, elapsed accounting and deferral are fully
deterministic. In `advance_parallel`, tasks within one dependency wave
run concurrently — **task bodies must mutate only state owned by their
own key** (or publish into owner-managed merge buffers). The executor
never merges results; the serial-vs-parallel benchmark asserts identical
final checksums.

## Persistence

Tasks are code — they are re-registered on load, not serialized. The
scheduler's cadence state is derivable (`last_run` timestamps reset to
registration tick on `add`); owners persist simulatable state, not
executor internals. If a game needs durable cadence bookkeeping it can
record `(key → tier, dormant_since)` — both are queryable.

## Benchmark harness

`stellar_simulation_scale_tests <systems> [ticks]` (ctest entries
`simulation_scale_250` … `simulation_scale_5000`) models one task per
system across a plausible LOD mix (2% Active / 8% Nearby / 30% Normal /
40% Background / 20% Dormant), four domains, dependency edges on every
tenth task, periodic domain wakes and `wake_all` storms. Each tick runs
serially and through `advance_parallel`; final state checksums must
match. Output: mean/p50/p95/p99/peak tick wall time per mode, JobSystem
submitted/completed, wakeup count, tier distribution, per-domain run
totals and task-state bytes.

Measured on the dev machine (60 ticks, 8-lane mix per task): 250 systems
serial mean ≈ 20 µs/tick; 5000 systems serial mean ≈ 536 µs/tick —
linear scaling dominated by task work, with p99 spikes on `wake_all`
storm ticks (expected: the full galaxy runs that tick). Parallel job
overhead exceeds task cost at synthetic-task sizes; it pays off when
per-task work is real.

## Remaining limitations

- Per-tick `depends_on` is ordering-only; there is no cross-tick
  dependency gating or dataflow.
- `max_wall_time` is checked between tasks/waves — a single oversized
  task can exceed it (never preempted mid-body).
- Parallel mode submits waves sequentially; waves wait fully before the
  next submits (simple boundary, leaves some overlap on the table).
- No per-task cost learning — cadence is owner/tier policy, not adaptive.
- Core/game phases do not yet run through the executor — the engine
  layer is proven by tests/benchmarks; campaign adoption is separate
  work (ROADMAP item 2's remaining boundary).
