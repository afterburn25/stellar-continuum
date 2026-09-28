# Species Fleet Crew Integration

This note defines the current early-release Species boundary for operating crews aboard interstellar vessels.

## Why this exists

Fleet gameplay already models species-specific colony passengers, but ordinary scout, science and military crews previously had no biological identity at all. That made existing Species morphology, biomass, metabolism and dormancy facts impossible for future life-support/logistics/habitation systems to consume.

This milestone adds a **reconstructible crew physiology view**, not a new combat or ship-performance bonus system.

## Current reconstruction rule

`FleetState` currently persists fleet role but not ship design ID or crew manifest.

The early-release design registry currently has exactly one active design for each fleet role. `ShipDesignRegistry.GetCurrentDesignForRole(...)` therefore reconstructs the design only while that one-to-one rule remains true.

`CurrentFleetCrewSpeciesView` currently resolves operating crew species from the owning civilization's founding `SpeciesId`.

This is explicitly transitional. Persistent fleet design/crew identity becomes required when any of the following become authoritative:

- multiple ship designs for the same fleet role;
- captured or transferred vessels whose design identity matters;
- foreign assigned crews;
- mixed-species operating crews;
- persistent crew casualties/replacements;
- player-selected crew composition.

At that point the game should persist bounded aggregate crew identity rather than continuing to infer it from owner + role.

## Crew complement

`ShipDesignDefinition.CrewComplementIndividuals` is aggregate static design data. The current proving complements are deliberately modest first-generation design values and are not final lore/balance commitments.

No object is created per individual crew member.

## Crew vs passengers

Operating crew and transported population are separate concepts.

A colony ship can therefore have:

- crew species: derived from the current owning civilization bridge;
- passenger species: `FleetState.EmbarkedPopulationSpeciesId`;
- passenger amount: `FleetState.EmbarkedPopulationMillions`.

The two species may differ. Species code must not overwrite one with the other.

## Physical outputs

`FleetCrewSpeciesSnapshot` exposes read-only physical facts:

- crew species;
- design and aggregate crew complement;
- passenger species when population is embarked;
- adult crew biomass;
- baseline, typical-day and peak-activity metabolic demand;
- natural dormancy mode, dormant demand, maximum duration and recovery time;
- body plan, normal work orientation, body dimensions and buoyant-workspace requirement.

These values can later be consumed by Logistics, habitat/life-support design, medicine and crew-space ergonomics.

## Explicit non-effects

This view does **not** directly modify:

- strategic speed;
- sensor range;
- weapon damage;
- shield/armor/hull durability;
- research output;
- Industry or Credits;
- diplomacy or morale.

If crew biology matters to those outcomes later, the owning system must model a concrete causal mechanism such as inadequate life support, hostile operating environment, unsuitable controls/workspace, medical stress, fatigue or supply shortage.

## Save-format boundary

This milestone is intentionally reconstructible and does not require a new save-format version.

The current bridge must stop being reconstructible once owner + role is insufficient to identify crew/design state. That future transition should use an explicit migration rather than silently changing crew identity on load.

## Validation

Automated checks prove:

- every current role maps to exactly one current design;
- every current design has a positive bounded aggregate crew complement;
- crew species follows the current owner-species bridge;
- crew biomass and metabolic demand are derived from Species physiology;
- colony-ship crew and passenger species remain independent;
- changing crew species changes physical crew burden but does not mutate fleet strategic speed, sensor range or Combat state.
