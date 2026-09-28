# Species Environmental Demographic Pressure

## Purpose

Species life history already changes intrinsic population-turnover pace. Founding colonies now also have authoritative naturally compatible physical homeworld bodies. This milestone connects those two facts conservatively: the exact natural environment may reduce population turnover on a naturally viable but stressful world.

This is not a generic racial bonus/penalty system. The effect emerges from authored Species tolerances evaluated against authoritative planetary gravity, temperature, pressure, atmosphere, solvent, immersion, and radiation.

## Species-owned turnover contract

`ColonyPopulationTurnoverPressure` composes:

- intrinsic Species life-history growth pace;
- whether the colony has an exact authoritative occupied body;
- evaluated colonization viability;
- natural habitability;
- environmental limiting factor;
- environmental-support requirement;
- natural-environment turnover factor;
- final effective growth pace.

The final relationship is:

`EffectiveGrowthPaceFactor = IntrinsicGrowthPaceFactor × NaturalEnvironmentTurnoverFactor`

Economy remains authoritative for the final population mutation and the Terran-normalized base growth constant.

## Natural-world pressure

Environmental pressure applies only when all of the following are true:

1. `ColonyState.PlanetaryBodyId` is exact/non-null;
2. the exact body resolves correctly in the colony system;
3. authoritative Species evaluation classifies the world as `NaturallyViable`.

For those colonies:

`NaturalEnvironmentTurnoverFactor = sqrt(NaturalHabitability)`

Natural habitability is already a canonical worst-axis physical suitability in `[0,1]`.

The square-root response preserves a comfortable world at `1.0` while compressing the penalty on merely viable worlds. At the current natural-settlement threshold of `0.20`, the factor is approximately `0.447` rather than `0.20`. No per-Species percentage table is introduced.

This milestone does not create mortality or negative population growth. Environmental stress only reduces positive baseline turnover.

## Legacy null-body fairness

If `PlanetaryBodyId` is null, environmental viability is reported as unknown and the environmental turnover factor is exactly `1.0`.

The existing compatibility-world resolver is useful for legacy presentation/migration, but it is not authoritative historical occupancy. Old saves therefore do not receive a retroactive demographic penalty based on a guessed body.

## Habitat-supported fallback fairness

An exact body classified as `HabitatSupportedFallback` also receives an environmental turnover factor of exactly `1.0` for now.

Those colonies already exist because an unspecified prototype support system makes an otherwise non-natural world usable. Applying raw natural stress while support capacity, reliability, maintenance, and resource costs are not modeled would double-count or invent the support system.

A future habitat-support owner can replace this neutral treatment with a consequence derived from actual support delivery and failure state.

## Economy boundary

`EconomySimulation` now consumes `IColonyPopulationTurnoverPressureView` and uses `EffectiveGrowthPaceFactor` in the population update:

`population *= exp(baseRate × stability × effectiveSpeciesPace × simulationDays)`

Credits, Industry, and Science formulas are unchanged and are still calculated from the tick's starting population/infrastructure/stability before population growth is applied.

Changing Species or environmental turnover therefore does not create a direct same-tick productivity bonus or penalty.

## Validation

Species mechanics checks cover:

- exact naturally viable but stressed worlds use `sqrt(NaturalHabitability)`;
- effective pace exactly conserves intrinsic × environmental pressure;
- habitat-supported fallback worlds remain environmentally neutral;
- legacy null-body colonies remain environmentally neutral and expose no fake body/viability;
- no environmental pressure is applied without authoritative exact occupancy.

The Economy regression independently proves that the final population update uses the Species-owned effective pace while same-starting-state Credits, Industry, and Science remain unchanged by Species/environmental turnover.

## Deliberately deferred

This milestone does not model:

- environmental mortality;
- negative population growth;
- food, air, water, thermal, pressure, radiation, or immersion resource flows;
- habitat support capacity or reliability;
- medical/health state;
- adaptation progression as an active colony state;
- productivity modifiers;
- happiness, politics, or migration.

Those systems may consume the same physical Species/environment facts later without changing the ownership boundary established here.
