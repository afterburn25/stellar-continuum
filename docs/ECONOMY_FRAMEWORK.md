# Economy Framework

Reusable data-driven economy layer for strategic simulation.
Modules: `engine/include/stellar/engine/economy_catalog.hpp`,
`engine/src/economy_catalog.cpp`, layered over the runtime
`resource_economy.hpp` (`ResourceNetwork`/`Inventory`/`Recipe`).
Tests: `economy_catalog` (ctest).

Status: IMPLEMENTED at engine level (catalog, validation, graph,
diagnostics, runtime bridge). Game-side catalogs and Core adoption are
separate work.

## Layers

| Layer | Type | Role |
| --- | --- | --- |
| Definitions | `ResourceSpec`, `RecipeSpec` | Content rows: what exists, how it transforms |
| Catalog | `EconomyCatalog` | Validated registry of specs |
| Graph | `EconomyGraph` | Derived dependency structure, immutable per catalog generation |
| Diagnostics | `analyze_economy` | Demand/bottleneck/reserve analysis over observed state |
| Runtime | `ResourceNetwork` (existing) | Inventories, producers, transfers, shortages |

`to_runtime_recipe(RecipeSpec)` bridges catalog recipes into the runtime
executor, so content validated by the catalog is what actually runs.

## ResourceSpec

`id`, `name_key` (localization), `category` (Raw / Refined / Component /
Consumable / Energy / Labor / Information / Abstract), `physical`,
`mass_per_unit`, `volume_per_unit`, `storage` (Bulk / Liquid / Gas /
Cryo / Contained / Live / None), `decay_per_day` (perishability),
`transportable`, `base_value`, `substitution_group`, `tags`, `unit`.

## RecipeSpec

`inputs` / `outputs` (consumed/produced per run), `catalysts` (required
present, not consumed), `byproducts` (always-produced secondaries),
`labor_persons` + `labor_skill`, `energy_units` + `energy_resource`,
`facility_tags`, `duration_days`, `efficiency` (output multiplier),
`allow_substitution` (inputs may swap within substitution groups).

## Validation

`EconomyCatalog::validate()` collects every problem — it does not stop
at the first error. `define()`/`add_recipe()` never throw on content;
malformed or duplicate ids are recorded as issues. Checks:

- malformed / duplicate ids (record, `id`, reason)
- physical resource without positive `mass_per_unit`
- nonphysical resource carrying mass/volume or flagged transportable
- negative decay / base_value / labor; nonpositive amounts, duration,
  efficiency
- unknown resource references in inputs/outputs/catalysts/byproducts
  and `energy_resource`
- zero-output recipes
- energy requirement with no energy-class resource in the catalog
- `allow_substitution` on inputs lacking a `substitution_group`
- dependency cycles (every member reported)
- unreachable production chains — chain categories (Refined /
  Component / Consumable) with no viable recipe path, where viability
  means every input and catalyst is available

Issue order is deterministic (sorted by severity/record/field/reason)
and independent of insertion order.

## Availability model

Raw resources are sources — extraction is a game mechanism, not a
recipe. Energy, Labor, Information and Abstract resources come from
facilities and population, not recipes. Refined, Component and
Consumable resources must be reachable through the recipe graph or the
catalog reports an unreachable-chain warning. This is a fixpoint
computation, so a cycle with no external input correctly reports every
member as both cyclic and unreachable.

## Diagnostics

`analyze_economy(catalog, demand, observed)` produces one
`EconomyDiagnostic` row per demanded or observed resource, sorted by id:

- `demand_per_day` / `supply_per_day` / `unmet_per_day`
- `reserve_days` — stock ÷ consumption rate (infinity if stocked but
  unconsumed)
- `utilization` — produced ÷ installed capacity
- `import_dependent` — demanded but no recipe produces it
- `bottleneck` — unmet demand

`EconomyGraph` additionally answers `producers_of`, `consumers_of`,
`downstream_resources` (transitive: what depends on this — downstream
affected industries), `upstream_resources` (what it requires),
`unproducible_resources`, and exposes the raw `edges()` for editor
graph rendering.

## Determinism

The catalog is a pure data structure: no clocks, no randomness, no
hidden order. All query and validation results are sorted and stable
across insertion order and platforms.

## Example

```
res.ore (Raw) --smelt--> res.steel (Refined) --roll_plate-->
res.hull_plate (Component) --assemble_hull--> res.ship_hull

res.hydrogen (Raw) + res.energy (Energy) --crack_hydrogen-->
res.propellant (Refined)
```

## Remaining limitations

- Substitution is declared but not yet resolved by the runtime
  (`to_runtime_recipe` keeps primary inputs only).
- Catalysts/facility_tags/labor are validated metadata; the runtime
  ResourceNetwork does not gate on them yet (production queues,
  priority allocation and maintenance consumption belong to the colony
  framework milestone that consumes this catalog).
- Demand diagnostics consume caller-supplied observations; there is no
  automatic rollup yet.
