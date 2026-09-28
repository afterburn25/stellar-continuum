# Species Demographic Integration

This note defines the current Species → population/economy boundary for intrinsic demographic pace.

## Purpose

Early-release colony growth previously used one universal exponential growth constant for every species. That made authored differences in reproductive maturity, generation length and reproductive cadence mechanically inert.

The current milestone makes those biological facts matter **without introducing generic racial productivity bonuses**.

## Ownership boundary

Species owns a read-only, dimensionless biological pace input.

Economy/Population remains authoritative for:

- the base population-growth constant;
- colony stability effects;
- the actual population mutation;
- future mortality, healthcare, housing, resources, policy, migration and social constraints.

Species does **not** directly modify Credits, Industry or Science output in this milestone.

## Intrinsic demographic pace

`SpeciesDemographicPressureEvaluator` derives three normalized factors from authored `SpeciesLifeHistory` data, using `terran_baseline` as the reference value of `1.0`:

- generation pace: Terran generation length / species generation length;
- reproductive-event throughput: `(offspring per event / minimum inter-event years)` relative to Terran;
- maturity pace: Terran reproductive maturity / species reproductive maturity.

`IntrinsicGrowthPaceFactor` is the geometric mean of those three dimensions, bounded by a broad numerical/gameplay safety rail (`0.20`–`1.80`).

The clamp is not a named trait bonus. Current proving species naturally fall inside it; it primarily protects future authored catalog entries from pathological values.

## Economy integration

`EconomySimulation` keeps the existing Terran-normalized base rate:

`BaselineDailyPopulationGrowthRate = 0.000055`

The current scalar colony growth step is:

`population *= exp(baseRate × stability × intrinsicSpeciesPace × simulationDays)`

The species factor is supplied through `IColonyDemographicPressureView`, allowing a future authoritative bounded multi-species cohort owner to replace the current single-species scalar adapter without moving demographic biology into Economy.

## Explicit non-effects

Changing only `PopulationSpeciesId` while holding starting population, infrastructure and stability equal does **not** directly change same-tick:

- Credits production;
- Industry production;
- Science production.

Any later economic consequence can arise indirectly because populations diverge over time, or through explicit logistics/habitat/health systems, but there is no hidden species productivity multiplier.

## Environmental stress is deliberately deferred

`ColonySpeciesEnvironmentView` already exposes natural habitability, operational capacity and mitigation requirements on the actual occupied world.

Those values are **not yet multiplied into population growth**. Current seeded/homeworld generation still contains legacy compatibility assumptions, and habitat-support capability/cost is not yet authoritative end-to-end. Applying natural habitability to growth today could punish a species because of world-generation scaffolding rather than a real gameplay decision.

Environmental demographic effects should be connected only after the colony/habitat/logistics owners can distinguish:

- natural environment;
- installed habitat support;
- support capacity and reliability;
- operating/resource cost;
- health/mortality consequences.

At that point Species should continue to expose physical stress; the population owner should decide births/deaths/growth consequences.

## Current proving behavior

The four current proving species derive different turnover pace from their authored life histories. Terran is normalized to `1.0`; long-generation cryogenic biology turns over substantially more slowly. Pelagic and compact high-gravity biology fall between those extremes based on their own maturity, generation and reproductive-event timing.

These are mechanical consequences of authored biology, not final lore balance commitments.

## Validation

Automated checks prove:

- Terran normalization is exactly `1.0`;
- all current species return finite deterministic bounded values;
- the cryogenic result is derived from its authored life-history numbers;
- Economy applies the exact Species pace to population growth;
- identical economic starting conditions produce identical same-tick Credits/Industry/Science even when species differ;
- long-generation cryogenic population grows more slowly than Terran under otherwise identical scalar conditions.
