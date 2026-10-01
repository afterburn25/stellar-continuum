# Colony Framework

Reusable settlement substrate: districts host structures; structures
produce jobs, housing and resources and draw utilities. Modules:
`engine/include/stellar/engine/colony.hpp`, `engine/src/colony.cpp`.
Tests: `colony` (ctest).

Status: IMPLEMENTED at engine level. Core adoption pending — the
authoritative game settlement remains `stellar::core::Colony` /
`surface_economy`; this framework is the deterministic building-block
layer those rules can be re-expressed on.

## Model

Content is data, not code:

- `DistrictSpec` — a developed parcel with bounded `structure_slots`,
  build cost/time, its own utility demand and upkeep, and `required_tags`
  site/tech tags.
- `StructureSpec` — a facility: optional required district (or
  standalone), build cost/time, utility demand **and supply** per day
  (power plants feed the pool), upkeep, per-day inputs/outputs while
  operating, `jobs`, `housing`, condition decay/repair rates and
  `required_tags` tags.

Runtime instances (`District`, `Structure`) carry construction
progress, complete/enabled flags, per-structure `condition` and the
last advance's `operating` ratio. Instance ids are caller-supplied so
save identity maps directly onto framework objects; construction cost
payment is the caller's responsibility (authoritative inventory).

## advance(elapsed_days, ColonyInputs)

One deterministic pass (ascending instance-id order, expected-value
math, no RNG):

1. **Construction** — remaining build days integrate down; completion
   counts are reported in `ColonyDelta`.
2. **Utilities** — aggregate supply/demand pools over complete+enabled
   districts and structures. Per-utility satisfaction =
   `min(1, supply/demand)`, shared uniformly by all consumers — no
   ordering dependence.
3. **Workforce** — `jobs_total` scales uniformly by
   `workers_available`; the colony does not own population (pair with
   `Population`).
4. **Per-structure** — `operating` = district gate × utility ratio ×
   worker ratio × input fill. Upkeep and inputs draw from the optional
   `Inventory*` stockpile (shortfalls reported, not thrown); outputs
   deposit into it or are reported gross when no stockpile is given.
   `condition` decays at the spec rate when under-maintained and repairs
   when maintenance funding + upkeep are met. Disabled structures stall
   and decay at half rate; hosted structures stall while their district
   is incomplete or disabled.

`ColonyDelta` reports jobs filled/total, housing capacity, produced
outputs, upkeep/input shortfalls, the utility balance table and
construction completions.

## Determinism

Fixed ascending-id processing, shared utility pools (no consumption
ordering), expected-value arithmetic and caller-supplied ids. Identical
inputs produce bit-identical deltas (asserted in tests). Elapsed-days
advance composes with `SimulationExecutor` dormant tiers — a dormant
settlement integrates months in one call.

## Persistence

Specs, districts and structures are plain data; serialize
`districts()`/`structures()` plus the spec tables. No hidden engine
state.

## Remaining limitations

- Utility supply counts whenever a structure is complete+enabled —
  `operating` does not feed back into supply (avoids the supply →
  operating → input circularity); owners disable structures to cut
  supply. Per-utility priority shedding is M5 network work.
- A structure completing mid-step produces for the whole step
  (expected-value model, not per-day integration).
- No structure adjacency, coverage areas or placement geometry yet —
  districts are slot containers, not spatial objects.
- No level/upgrade chain on structures (Core `pending_upgrade` remains
  game-level); specs can model tiers as distinct ids.
- Workforce is one undifferentiated pool — skill-tiered staffing waits
  on Population occupation/education integration.
