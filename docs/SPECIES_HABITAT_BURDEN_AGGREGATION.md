# Species Habitat-Burden Aggregation

## Purpose

The per-colony habitat-support burden contract exposes authoritative Species metabolism, biomass, and exact-body environmental mitigation requirements without inventing support capacity or economic units.

This milestone adds deterministic read-only aggregation so Logistics, strategic planning, diagnostics, and future support-capacity systems can consume the same raw burden at civilization and star-system scale.

It still does not claim any burden is satisfied.

## Preserved units

`HabitatSupportBurdenTotals` keeps the exact units of the colony contract:

- population: millions of individuals;
- typical-day metabolic demand: Species reference-demand millions;
- adult biomass: million kg;
- population requiring gravity mitigation: millions;
- population requiring thermal control: millions;
- population requiring pressure control: millions;
- population requiring sealed habitat: millions;
- population requiring artificial biosphere: millions;
- population requiring radiation shielding: millions.

No weighted support score, cargo conversion, credits/upkeep, or infrastructure-capacity equivalent is introduced.

## System aggregation

`SystemHabitatSupportBurden` groups positive-population colonies owned by one civilization in one star system and exposes:

- populated colony count;
- exact-body colony count;
- legacy unknown-environment colony count;
- habitat-supported fallback colony count;
- distinct scalar colony Species count;
- raw burden totals.

Exact-body plus legacy-unknown counts must exactly partition the populated colony count. Habitat-supported fallback is a subset of exact-body colonies.

System records are deterministically ordered by `SystemId`.

## Civilization aggregation

`CivilizationHabitatSupportBurden` exposes the same colony classifications and raw totals across all populated colonies owned by one civilization, plus the ordered system records.

The record validates that:

- per-system populated/exact/unknown/fallback counts re-sum exactly to civilization counts;
- every system entry belongs to the same civilization;
- system IDs are unique and sorted;
- every raw quantitative total re-sums exactly from per-system totals.

Distinct Species count is calculated at civilization scope rather than summed from systems because the same Species may occupy more than one system.

## Unknown is not zero need

Legacy null-body colonies remain explicitly classified as `LegacyUnknownEnvironmentColonyCount`.

Their metabolism and biomass contribute normally, because Species/population are known. Environmental mitigation populations remain unavailable because exact occupied environment is unknown.

The aggregate therefore does not silently interpret unknown historical occupancy as a naturally comfortable world or as zero support need.

## Zero-population colonies

Zero-population colonies do not contribute active biological burden and are omitted from populated colony/system counts.

Negative or non-finite population is rejected rather than silently skipped.

## Mixed Species

The aggregate derives Species identity from each scalar colony population and retains `DistinctSpeciesCount` at system and civilization scope.

It does not replace colony population Species with the civilization founding Species. This preserves future captured/transferred/multi-origin colony cases until the scalar colony bridge is replaced by bounded multi-Species cohorts.

## Read-only behavior

`CurrentCivilizationHabitatSupportBurdenView` creates no persistent state and mutates no colony, planetary body, civilization, economy, construction, Logistics, or Species data.

The current view is an owner/raw simulation contract, not an observer-filtered intelligence view. Any future UI or foreign-AI exposure must pass through an appropriate fair-information projection rather than exposing exact hidden colony burdens directly.

## Ownership boundary

Species aggregation owns only additive physical burden accounting.

It does not own:

- habitat support capacity;
- support reliability or failure probability;
- life-support inventories;
- cargo conversion;
- interstellar routes;
- stockpiles;
- credits/upkeep;
- construction projects;
- demographic consequences beyond the separately defined environmental-turnover contract.

## Validation

Species checks prove:

- civilization totals exactly conserve per-colony burden;
- system totals exactly conserve the colonies in that system;
- civilization totals exactly re-sum from system totals;
- exact-body, legacy-unknown, and fallback counts remain distinct;
- mixed colony Species remain visible at both scopes;
- system output ordering is deterministic;
- zero-population colonies do not create active burden;
- aggregation is read-only for colony identity, body occupancy, Species, population, infrastructure, and stability.

## Next safe step

A later Logistics/Construction milestone may define explicit support capacity or installed environmental-control capability and compare it against this burden. That capacity should come from represented facilities, technology, resources, and reliability—not from an arbitrary Species-side conversion or generic colony `Infrastructure` multiplier.
