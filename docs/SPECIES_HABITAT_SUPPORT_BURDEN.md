# Species Habitat-Support Burden

## Purpose

Species environmental mechanics now identify exact occupied worlds and can reduce population turnover on naturally viable but stressful planets. The next prerequisite for supported hostile-world colonies is a clear statement of **what biological/environmental burden exists** before any system claims it can satisfy that burden.

This milestone provides that raw burden. It does not create support capacity, supply routes, upkeep, costs, or a new economy.

## Colony burden contract

`ColonyHabitatSupportBurden` exposes the physical Species/population quantities that are authoritative regardless of logistics implementation:

- Species ID;
- population millions;
- typical-day metabolic demand in the existing Species reference-demand units;
- adult biomass in million kg.

When and only when `ColonyState.PlanetaryBodyId` is exact, it also exposes `ColonyEnvironmentalSupportRequirements`:

- exact body ID;
- colonization viability;
- natural habitability;
- unprotected operational capacity;
- limiting environmental factor;
- number of required mitigation categories;
- gravity mitigation;
- thermal control;
- pressure control;
- sealed habitat;
- artificial biosphere;
- radiation shielding.

For the current scalar single-Species colony population, the burden view also exposes how much population is subject to each required category. A required category maps to the colony's full population; a non-required category maps to zero.

## Legacy fairness

A legacy colony with null `PlanetaryBodyId` still has known Species metabolism and biomass, but environmental requirements are returned as unknown (`Environment = null`).

The legacy compatibility-world resolver is not used to manufacture support obligations. This matches the environmental-demographics rule that old saves are not retroactively penalized by a guessed occupied body.

## Habitat-supported fallback

An exact colony on a `HabitatSupportedFallback` world exposes its real environmental mitigation requirements and is explicitly marked as using the current prototype habitat-support assumption.

This does **not** mean those requirements are currently satisfied. The burden contract only states the physical need that a later capacity/reliability owner must meet.

## Ownership boundary

Species owns:

- metabolism;
- biomass;
- environmental suitability;
- mitigation categories required by biology + environment.

Species does not own:

- food, air, water, coolant, pressure-gas, shielding mass, or other cargo-unit conversion;
- route throughput;
- stockpiles;
- credits/upkeep;
- construction capacity;
- support reliability;
- failure probability;
- whether a colony's current infrastructure actually satisfies the burden.

Those consequences belong to Logistics, Construction, Economy, and future health/population systems.

## Validation

The Species checks require:

- newly generated founding colonies expose exact environmental support requirements;
- metabolic demand exactly matches the existing Species metabolic evaluator;
- biomass exactly matches population × authored adult mass;
- each mitigation category exposes either the full scalar colony population or zero, matching the category flag;
- equal-population Species with different physiology produce different biomass/metabolic burdens even when environmental occupancy is unknown;
- null-body legacy colonies expose no guessed environmental requirement;
- exact habitat-supported fallback colonies preserve their authoritative viability and non-zero mitigation requirement.

## Next integration step

A separate civilization/system aggregation view can sum these raw burdens for Logistics and AI consumers without changing the units or claiming capacity. Only after a real support-capacity owner exists should `HabitatSupportedFallback` demographic behavior stop being neutral.
