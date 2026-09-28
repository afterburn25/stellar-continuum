# Pioneer Foundation Balance Handoff

Status: **next balance work; no implementation in this branch.** This is a design for Core review,
not an approved retune. It preserves the existing homeworld and district economy, existing vessels,
embarked populations, queued reservations, and saves.

## Decision and present seam

The new-player target is 10,000 colony founders (`0.01M`) and 250 outpost personnel
(`0.00025M`). The current designs instead reserve 250M for `colony_ship` and 8M for
`resource_outpost_ship` (`src/Game/Simulation/Shipbuilding/ShipDesignRegistry.cs`). Their
metadata also lists crews of 320 and 180 people. Today only `EmbarkedPopulationMillions` is a
conserved passenger population. Crew is design/species-view metadata, not an independently
stored or transferred cohort. The first implementation must therefore describe the 10,000/250 as
the modeled founders/personnel and must not claim that 320/180 crew are conserved into a colony
until a deliberate crew-manifest model exists.

Money is one normalized simulation budget unit, rendered locally by
`SovereignCurrencyCatalog`; for Terrans, one unit is UED $10M. There are no Interstellar Credits.
The current colony ship authorization is 180 budget units = **$1.8B UED**, and its separate
settlement authorization is 120 = **$1.2B UED**: $3.0B UED total plus 1,500 materials. The
outpost is 130 = $1.3B UED plus 90 = $0.9B UED: $2.2B UED plus 950 materials. Those are current
costs, not a claim that either ship should receive a number-only price cut.

| Item | Current modeled payload | Target modeled payload | Current crew metadata | Current committed money |
| --- | ---: | ---: | ---: | ---: |
| Colony ship | 250M | 0.01M (10,000) | 320 | $3.0B UED + 1,500 materials |
| Sealed outpost | 8M | 0.00025M (250) | 180 | $2.2B UED + 950 materials |

Shipbuilding currently removes the reserved passenger population and the ship authorization at
order acceptance, stores population/species/source on `ShipyardState` and queued orders, turns it
into fleet passenger population on completion, and restores the population plus authorization on
cancellation. Spent materials stay spent. `ColonizationSimulation` transfers the fleet passengers
into the new colony after the 30-day colony or 20-day outpost establishment timer, then consumes
the vessel. This chain is the conservation seam to retain.

The support seam makes the payload change unsafe by itself. `ColonySustenanceCapacity` gives every
colony `500M × infrastructure` sealed food, water, and housing support before body biology or
surface modules. A natural colony starts at infrastructure .35 (175M baseline); an outpost starts
at .15 (75M). Natural worlds can also receive `12,000M × area × infrastructure × habitability`.
Existing district modules are similarly large: habitat 1,000M/15,000 workers, agriculture and
water 2,000M each/35,000 and 25,000 workers, and a power generator needs 20,000 workers. At a
45% participation rate, 10,000 founders expose 4,500 workers and 250 personnel expose about 113.
They cannot honestly operate these districts.

## Smallest complete foundation path

Add an optional, versioned `PioneerFoundingManifest` to new pioneer ship designs and a
`PioneerSettlementProvisioning` state to the founded settlement. It holds finite, physically
transferred habitat, power, food, water, basic-service and job capacities. The manifest moves
with the paid build: order → completed fleet → settlement. Cancellation before completion restores
founders and authorization by the existing mechanism; consumed materials, including supplied
hardware, remain consumed. A settlement receives no separate grant.

Only a settlement carrying the provisioning state uses pioneer capacity. Its support is the
manifest's delivered, powered and staffed service capacity plus finite reserves; it gets neither
500M sealed baseline nor natural-biosphere output until an explicit commissioning milestone adds
staffed local capacity. Existing colonies and old saves have no provisioning state and keep their
present calculations unchanged.

The first tuning pass should encode small modules alongside the manifest, rather than scaling down
all `SurfaceBuildingCatalog` outputs. Working targets to calibrate in simulation tests are:

| Delivered service | Colony founders | Outpost personnel | Requirement |
| --- | ---: | ---: | --- |
| Habitat, food and water capacity | 0.012M (12,000) | 0.00030M (300) | Modest headroom, no billion-person inference |
| Initial reserve | 0.30M food-days; 0.07M water-days | 0.0075M; 0.00175M | Existing 30/7-day policy for payload only |
| Staffed basic-service jobs | about 0.003M (3,000) | about 0.00010M (100) | Fits the 4,500/~113 available workers |
| Power | manifest-specific generation/demand | manifest-specific generation/demand | Essential services require funded, powered operation |

These are balance targets, not values to copy into an unrelated district definition. The exact
power units and daily upkeep must be calibrated against `SurfaceConstruction.GetOutput` and
`CivilizationOperatingCapacity`, with a nonzero operating cost that can pause services. If funding,
power, or staffing is insufficient, food/water/habitat support declines through the existing
reserve/shortage feedback path; recovery must be by restoring funding or completing a pioneer
service, never by an automatic output multiplier. A viable first colony begins with finite
provisions, staffed basic services, and enough power; it can then face a visible, recoverable
shortage if those services are not maintained.

The current fixed `payload + 500M` source eligibility rule cannot be silently applied to pioneers.
Keep it for legacy designs. Give manifest-bearing designs an explicit source-retention policy,
validated at authorization, that leaves a small defined source floor and does not alter any legacy
order. Core must approve that floor; it is an economic policy, not a UI decision.

## Affected ownership and seams

- **Shipbuilding owner:** `ShipDesignDefinition.cs`, `ShipDesignRegistry.cs`,
  `ShipbuildingSimulation.cs`, `ShipyardState.cs`, and `FleetState.cs`. Add manifest persistence
  to active and queued orders and to completed fleets; preserve the existing population/species/
  authorization refund chain.
- **Colonization/economy owner:** `ColonizationSimulation.cs`, `ColonyState.cs`,
  `ColonySustenanceCapacity.cs`, `ColonySustenanceReserves`, `SurfaceConstruction.cs`, and
  `CivilizationOperatingCapacity`. Transfer and operate only pioneer provisioning; retain legacy
  capacity and district math untouched.
- **Persistence owner:** `CampaignSaveService.cs` DTOs, validation and migrations. Missing
  pioneer fields must mean legacy behavior. Do not reinterpret old `EmbarkedPopulationMillions`,
  source reservations, colony populations, or surface buildings.
- **Presentation owner:** existing shipyard/colonization/surface read models. Show founders,
  supplied reserve, essential-service funding/power, and shortage recovery using the same
  authoritative snapshots; no new exchange currency.

Prefer new, manifest-bearing pioneer design IDs or an explicit fresh-campaign design revision over
changing the meaning of `colony_ship` and `resource_outpost_ship` in place. That decision belongs
with persistence compatibility because design IDs appear in queued orders and fleets.

## Required proof

1. Shipyard authorization reserves exactly 0.01M/0.00025M founders, species and source; legacy
   designs retain their existing payload and 500M threshold. Active/queued save-load and
   cancellation preserve source population and authorization refunds exactly.
2. Completion and settlement transfer the exact founders and manifest. The 320/180 crew numbers
   remain metadata unless a separate conserved crew state is introduced.
3. New pioneer colonies/outposts have only manifest support and finite 30/7-day reserves; old
   colonies, fleets, queued orders, and saves keep their prior values and capacity behavior.
4. Power/funding/staffing loss produces a real reserve decline and shortage; restoring a funded,
   staffed pioneer service recovers it. No district-scale building may be staffed by a 10,000/250
   settlement, and revenue remains labour-backed.
5. A full paid first-colony path reaches a viable startup without grants, then displays a
   meaningful, recoverable shortfall. Verify money formatting as UED budget units, not credits.

## Atomic implementation order

1. Add optional manifest/provisioning contracts and persistence defaults with conservation and
   old-save tests. No balance behavior changes yet.
2. Add new pioneer designs plus the manifest-specific source-retention rule; exercise active,
   queued, completion, cancellation and reload conservation.
3. Transfer provisioning at settlement and apply its funded power, jobs, finite reserves and
   shortage/recovery logic. Keep legacy colonies on their current paths.
4. Expose authoritative founder/supply/funding status in shipyard, colony and surface cards.
5. Run the ordinary first-colony journey and tune the manifest only after proof that startup is
   viable without hidden support.

This handoff deliberately does not change research, homeworld scale, district outputs, existing
saves, or currency rules.
