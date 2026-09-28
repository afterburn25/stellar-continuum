# Species Fleet Biological Load

This note defines the current Species-owned biological-load contract for interstellar fleets.

## Purpose

Operating crew physiology is now reconstructible for current fleets, and colony ships already carry species-identified passengers. Logistics/life-support systems need a single read-only view that combines those populations without conflating them or inventing economic units.

`FleetBiologicalLoadView` provides that bridge.

## Inputs

The view consumes authoritative existing state:

- current fleet owner and role;
- reconstructible current design crew complement;
- owner-derived current crew Species;
- embarked passenger population amount and Species;
- authored Species physiology, metabolism and dormancy;
- existing cross-species environmental/xenobiology compatibility.

It does not create or persist new fleet state.

## Common physical units

Crew and passenger metabolic demand are expressed in **reference-individual demand units**. Existing Species metabolic envelopes are population-scaled in millions; the fleet bridge converts them to the same individual-reference scale before summing.

Biomass is expressed in kilograms.

These units are intentionally physical/normalized inputs, not:

- food units;
- oxygen units;
- cargo tons;
- fuel;
- Credits;
- route capacity;
- ship range;
- endurance days.

Logistics/life support owns those conversions when the corresponding resource model exists.

## Crew and passengers stay separate

The snapshot preserves:

- crew Species and aggregate crew complement;
- passenger Species and population amount;
- crew metabolic load;
- passenger metabolic load;
- crew biomass;
- passenger biomass.

The totals are additive, but identity is not averaged away.

A colony ship may therefore carry a passenger Species different from its operating crew Species.

## Operating state

Crew and passengers may be evaluated independently as:

- typical day;
- resting;
- peak activity;
- natural dormancy.

Natural dormancy is available only when the Species biology explicitly supports it. The view rejects attempts to claim dormancy savings for a Species with no natural dormancy mode. Technological/medical stasis must be modeled separately.

## Mixed-species accommodation

When crew and passengers are different Species, the view reuses the existing first-contact/xenobiology contracts to expose whether the vessel requires:

- separate environmental accommodation;
- dedicated passenger nutrition;
- cross-species quarantine assessment;
- xenomedical interface adaptation.

These are requirements, not automatic penalties or diplomatic attitudes.

## Explicit non-effects

`FleetBiologicalLoadView` does not modify:

- fleet speed;
- sensors;
- Combat state;
- passenger amount or identity;
- ship orders;
- economy state;
- Logistics routes or stockpiles.

## Logistics integration boundary

Current Logistics is colony-centric and has no authoritative fleet supply-flow owner yet. Therefore this milestone **does not** inject fleet biological load into `PrototypeEconomyLogisticsView`.

A later fleet-supply owner can consume this contract to calculate real life-support resource demand, storage, endurance and route requirements without duplicating Species biology.

## Validation

Automated checks prove:

- crew-only vessels sum exactly to their crew load;
- same-Species passengers add biomass/metabolism without inventing separate habitat requirements;
- Terran crew with cryogenic-hydrocarbon passengers exposes separate environmental and nutrition requirements;
- combined biomass and metabolism conserve crew + passenger values;
- natural dormancy reduces demand only for Species that actually support it;
- the view is read-only with respect to passenger state.
