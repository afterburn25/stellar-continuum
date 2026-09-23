# Warfare Framework

Strategic-scale military model: fleets as cohort aggregates, explicit
engagements, interdiction zones. Modules:
`engine/include/stellar/engine/warfare.hpp`,
`engine/src/warfare.cpp`. Tests: `warfare` (ctest).

Status: IMPLEMENTED at engine level. Core fleet/battle systems remain
authoritative; this is the deterministic large-scale substrate for
thousands of concurrent fleets where per-ship simulation is
impractical.

## Model

- **`ShipClass`** — data-driven template: role, attack (hull
  damage/day), defense (flat per-ship absorption), hull, speed,
  supply, interdiction radius contribution.
- **`ShipCohort`** — class × count × condition × experience aggregate.
  Ten thousand destroyers are one cohort. `add_ships` merges with
  weighted condition/experience.
- **`FleetState`** — id + owner + strategic position + order
  (Hold/Move/Interdict/Retreat) + engaged flag. Fleet speed is the
  slowest cohort scaled by condition.

`report(fleet)` aggregates ships/attack/hull/speed/interdiction/supply.

## Interdiction, not presence

An `Interdict` order projects a radius (`Σ count × condition × class
interdiction`) that gates warp for fleets of OTHER owners — same-owner
movement is never gated by its own zone. `Hold`-order fleets project
nothing: presence alone never blocks movement. `interdicted(x, y,
mover_owner)` is a pure query.

## Engagement

`resolve(a, b, days)` — deterministic Lanchester-style attrition:

- Each side's aggregate attack (count × class attack × condition ×
  experience bonus up to +50%) applies over elapsed days.
- Damage distributes across cohorts proportional to hull share, net of
  per-ship defense absorption.
- Losses thin cohorts and degrade their condition proportionally.
- Both sides' damage uses pre-resolution strength (symmetric).
- Destroyed fleets disengage; retreat is caller-driven (set the Retreat
  order and stop resolving).

No RNG, no initiative rolls — expected-value resolution designed for
executor-driven cadence (hot while engaged, dormant otherwise).

## Determinism & scale

Sorted fleet/cohort iteration, expected-value math, no RNG — bit-equal
engagements asserted. 2000 fleets × 30 moves + 200 engagements in ~6ms.

## Persistence

Classes/fleets/cohorts/orders are plain data with caller ids; serialize
`fleets()` + `cohorts(fleet)` + the class catalog.

## Remaining limitations

- 2D strategic plane; no per-leg routes (pair with LogisticsNetwork
  paths for lane-bound movement).
- No range/weapon arcs — attack is a single aggregate dps; no
  rock-paper-scissors between roles (role is metadata).
- Retreat/morale is caller policy — the model reports destruction only.
- No supply consumption inside the model (supply_per_day is reported
  for the caller's economy to settle).
- No reinforcement mid-engagement semantics — `add_ships` works but
  callers should treat engaged fleets carefully.
