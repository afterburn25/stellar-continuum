# Planetary Development + Terraforming

Engine-level planetary environment model, species-relative
habitability evaluation, and staged terraforming. Modules:
`engine/include/stellar/engine/planetary.hpp` (environment +
habitability), `engine/include/stellar/engine/terraforming.hpp`
(staged projects); `engine/src/planetary.cpp`,
`engine/src/terraforming.cpp`. Tests: `planetary`, `terraforming`
(ctest).

Status: IMPLEMENTED at engine level. These are ADAPTER surfaces —
authoritative planet state remains Core `PlanetaryBody`/planetary
catalog; adapters project it into `PlanetEnvironment` and write the
mutated result back.

## Habitability (milestone 7)

`PlanetEnvironment` is a plain-data physical description: temperature
(K), atmosphere (atm), gravity (g), water fraction, sorted free-form
environment tags (`"dust_world"`, `"toxic_atmosphere"`, ...).

`HabitabilityProfile` is species-relative: hard ranges per physical
parameter plus `tolerance` — a soft margin (fraction of range span)
beyond each edge where suitability ramps linearly to zero. `water_min`
is a floor scored as achieved-fraction-of-minimum. `required_tags`/
`forbidden_tags` are absolute: violation zeroes suitability.

`evaluate_habitability(env, profile)` is a pure function returning
`suitability` (0..1 product of per-parameter scores), `habitable`, and
sorted `unmet` reason strings — exactly the signal Population's
`environment_suitability` and Colony's `required_tags` consume.

## Terraforming (milestone 8)

Staged physical mutation, not a universal progress bar:

- `TerraformProject` = sequential `TerraformStage`s; each stage applies
  linear deltas (temperature/atmosphere/water/gravity) across its
  duration and discrete tag changes at completion.
- One `Terraforming` instance per planet owns the adapted environment.
  `start`/`cancel`/`advance(days)`; cancel keeps already-applied deltas
  (physical work done is not undone); a project may restart at stage 0.
- `TerraformAdvance` reports completed stages (multiple per step
  possible) and project completion; `stage_progress`/`project_progress`
  give 0..1 for UI.
- Habitability is never computed inside terraforming — callers
  re-evaluate per species. A world +30K warmer is progress for humans
  and ruin for cryophiles; the framework keeps that species-relative.

## Determinism

Linear interpolation over elapsed days, discrete tag application at
stage boundaries, sorted tags, no RNG — bit-equal repeat runs asserted.
Step size does not affect final state (asserted: 1×200d ≡ staged
advances).

## Persistence

`PlanetEnvironment`, project catalog and program state (active id,
stage index, stage elapsed) are plain data — serialize directly.

## Remaining limitations

- Deltas are linear per stage — no diminishing returns, feedback loops
  (albedo→temperature coupling), or atmosphere composition breakdown.
- No cost/upkeep/resource draw inside the framework — stage gating by
  resources/energy is the caller's (settlement inventory or Core
  economy).
- Gravity/water deltas are physically naive (real terraforming barely
  moves gravity); the fields exist for exotic tech but games should
  normally leave them at zero.
- No multi-project parallelism — one active project per planet.
- Environment tags are free-form strings; no tag registry/validation
  yet (the economy catalog's strict-validation pattern fits if a tag
  taxonomy lands).
