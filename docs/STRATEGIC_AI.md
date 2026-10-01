# Strategic AI

Deterministic utility-based decision machinery for civilizations and
factions. Module: `engine/include/stellar/engine/strategic_ai.hpp`,
`engine/src/strategic_ai.cpp`. Tests: `strategic_ai` (ctest).

Status: IMPLEMENTED at engine level — the decision machinery, not the
policy. No Core/game civilization consumes it yet; scorers and commit
effects are caller-supplied against authoritative state.

## Model

`StrategicMind` owns a set of `UtilityAction`s partitioned by domain
(`"economy"`, `"expansion"`, `"military"`, ...). Each action carries:

- `score()` — caller's utility function over observable state (must be
  a pure function; a nondeterministic scorer breaks determinism),
- `commit()` — the effect, invoked once for the winner only,
- `cooldown_days`, `weight`, `enabled`.

`decide(domain, now_day, min_utility, hysteresis)`:

1. Iterates the domain's enabled, non-cooldown actions in ascending id.
2. Computes `utility × weight`; the domain's incumbent gets ×
   `hysteresis` (default 1.1) — decisions don't oscillate on marginal
   score noise.
3. Argmax wins; equal utilities resolve to the smallest action id.
4. Winner commits only if `utility ≥ min_utility`; otherwise nothing
   happens (a real option — inaction is not a bug).
5. The decision is journaled: day, domain, action, utility, candidate
   count, whether it displaced the incumbent.

## Hierarchical cadence

The framework deliberately does NOT own planning cadence. Register each
domain's `decide()` as a `SimulationExecutor` task at the tier matching
its timescale — empire strategy at `Background`, diplomacy at `Normal`,
fleet tactics at `Nearby`. One machinery, many cadences; dormant
factions simply integrate `elapsed_ticks` into `now_day`.

## Decision journal

Bounded ring buffer (`journal()`, capacity at construction) recording
every committed decision — the developer-facing "why did the AI do
that" surface. Not persisted; rebuild by replaying ticks if needed.

## Determinism

Ascending-id iteration, strictly-greater argmax (first wins ties),
fixed hysteresis multiplier, caller-clock cooldowns, no RNG. Bit-equal
repeat runs asserted in tests. Note: determinism holds given
deterministic scorers — the contract is documented, not enforceable.

## Persistence

Action registrations are code (like executor callbacks) — re-register
after load. Persisted state is only: per-domain incumbent id, per-action
last-commit day, and the caller's own game state the scorers read.

## Remaining limitations

- Flat utility scoring — no goal decomposition, planning graphs or
  opponent modeling; those are policy layers built on this machinery.
- One incumbent per domain — no simultaneous action budgets ("pick top
  3 under cost X") yet.
- Cooldowns are per-action; no per-domain decision budgets (the
  executor's task budgets already bound decision frequency).
- No observer/fog-of-war filtering inside the framework — scorers must
  read only what the faction legitimately knows (privacy is the
  caller's contract).
- Journal is memory-only and per-mind; no cross-faction analysis tools
  yet.
