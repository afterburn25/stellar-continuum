# Population Framework

Reusable cohort/aggregate demographics for civilization-scale
simulation. Modules: `engine/include/stellar/engine/population.hpp`,
`engine/src/population.cpp`. Tests: `population` (ctest).

Status: IMPLEMENTED at engine level. Colony/Core adoption pending.

## Model

Citizens are never individual entities at this layer. A
`PopulationCohort` is a tagged aggregate — `CohortKey{profile, culture,
occupation, education, wealth}` — carrying a headcount plus qualities:
`health`, `happiness`, `morale`, `housing_coverage`,
`employment_rate`, `environment_suitability`, `political_tendency`,
and an 8-bucket age distribution.

`DemographicProfile` rows are data-driven per species/culture
archetype: fertility/mortality/lifespan, per-capita food and goods
consumption, workforce participation, migration tendency, education
progression rate, environment needs.

## advance(elapsed_days, SettlementConditions)

One deterministic pass over cohorts (sorted-key order, expected-value
math, no RNG):

- **Mortality** — base rate plus starvation (food_ratio), environmental
  unsuitability, overcrowding, insecurity and unhoused terms;
  healthcare relieves up to 30%.
- **Fertility** — base rate scaled by food, housing and environment;
  births land in age bucket 0.
- **Aging** — buckets drain toward older ages at
  `lifespan/8` per bucket; deaths draw uniformly, births feed bucket 0.
- **Happiness/health/morale** — drift toward condition-driven targets;
  food shortage is a hard cap on happiness, not just one term.
- **Employment** — settlement jobs are distributed equal-share across
  cohort workforces (size × participation × working-age fraction);
  `employment_rate` drifts toward the achieved ratio.
- **Education** — `education_progress_per_year` fraction advances one
  level; the moving slice becomes (or merges into) the higher-education
  cohort key.
- **Emigration pressure** — computed from unhappiness, unemployment,
  overcrowding and profile `migration_tendency`; reported in
  `PopulationDelta.emigration_pressure`. Actual removal is explicit:
  `take_emigrants(key, n)` returns a state-carrying slice,
  `take_immigrants` merges it into a destination `Population`.
  Destination selection is a game-level concern.

`PopulationDelta` reports births, deaths (with starvation and
environmental attribution), educated members, the unemployed snapshot
and aggregate emigration pressure.

## Scale

Cost is proportional to **cohort count**, not headcount — 17.5M people
in ~100–240 cohorts advances a month in tens of microseconds. Query
helpers (`total`, `workforce`, `employed`, `unemployed`,
`average_happiness`, `food_demand_per_day`, `goods_demand_per_day`)
aggregate on demand.

## Determinism

Sorted cohort-key iteration, expected-value arithmetic, no randomness
and no wall-clock dependence. Identical inputs produce bit-identical
evolution (asserted in tests). Designed to sit behind
`SimulationExecutor` tiers: a dormant colony integrates months of
`elapsed_ticks`-derived days in one `advance` call.

## Persistence

Cohorts and profiles are plain data — serialize `cohorts()` and profile
definitions directly; the engine layer owns no hidden state.

## Remaining limitations

- Age-dependent fertility/mortality curves are not yet per-bucket
  (deaths draw uniformly; `DemographicProfile` could grow curves).
- Employment allocation is equal-share — no skill/wage competition for
  scarce jobs yet.
- Cohort splitting/merging policies (culture drift, occupation
  retraining, wealth mobility) beyond education are future work.
- `environment_needs` tags are declared metadata; the colony framework
  resolves them into `environment_suitability` per cohort.
