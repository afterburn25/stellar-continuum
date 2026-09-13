# Planetary management and Command Center slots

The planetary window replaces the player-facing free surface building workflow.
Open **Manage planet** from a focused owned planet or the colonies list. The window
uses live campaign data and sends ordinary simulation commands; it is not an editor
mockup. Existing surface scenery source remains available for later visual work.

## Player rules

- New colonization and resource-outpost arrivals have an unbuilt Command Center.
  Complete its construction before any planetary building can be authorized.
- The Command Center has its own site and does not consume a building slot.
- Colony levels 1, 2 and 3 unlock 16, 32 and 64 slots. Outposts unlock eight slots
  and retain the existing full-colony conversion requirement before expansion.
- A construction order immediately reserves one slot. Completion, upgrading,
  disabling and repairing retain it; cancellation or demolition releases it.
- Command Center construction and upgrades spend credits and materials up front,
  then take funded simulation time. Capacity increases only after completion.
  Ordinary buildings use the existing credit authorization and shared material
  construction budget. Paused time grants no progress; rejected orders spend nothing.
- Level 2 retains the Industrial Automation Program requirement; level 3 retains
  Orbital Manufacturing and minimum planetary-size requirements. Building upgrades
  retain their own research requirements.
- Existing established starting worlds retain their completed Command Centers.
  This includes the level-2 player homeworld. The new build-first rule applies to
  newly founded settlements; it does not dismantle existing cities.

Initial Command Center balance: 25 normalized credit units and 120 construction
materials, before the existing environmental construction multiplier; four days
at full funding on a baseline world. Existing upgrade costs remain unchanged.
Currency is displayed in each civilization's denomination, not raw budget units.

## Window contents

The top shows population, signed power balance, signed local credit flow and local
industrial production. The overview includes Command Center state/cost/research,
system, species, stability, infrastructure, radius, mass, gravity, temperature,
pressure, atmosphere, solvent, radiation, discoveries, specialization and environment
cost/wear multipliers. Scrollable slots remain usable at 1280×720 and 1920×1080.

Economy lists taxation, trade, required local operating costs with a breakdown,
operational research-lab capacity, queued material requirements, power demand and
supply, storage/charge/discharge, food/water/housing capacity and balances, reserves,
labor availability and demand, employment, cargo capacity, outpost deposit/extraction,
operating funding, empire stores, empire cash flow/material production and arrears.
Food and water remain population-support capacities rather than invented tonnage.
Research-lab capacity is not presented as daily research points. Industrial production
is not mislabeled as net materials after empire construction spending.

Alerts identify insufficient command capacity, power, workers, life support,
local credits, operating funding and stalled material supply. Existing reserve-aware
shortage feedback distinguishes a buffered deficit from population decline.
Building management supports timed upgrades, repairs, on/off, priority and confirmed
cancellation/demolition. Inspecting the window does not mutate the world.

## Persistence and native conversion

Galaxy save v18 and campaign wrapper v19 record `SurfaceBuildingState.SlotIndex`
and allow `SurfaceHubLevel = 0`. Loaders retain historical v16/v17 catalogs and
research/diplomacy state. Older free-placed buildings map deterministically to free
slots without moving coordinates or changing progress. Reads do not persist this
mapping; successful placement/removal stores it before modifying the building list.
Duplicate, negative or locked saved slots and invalid Command Center capacity fail
validation. Existing atomic save/backup behavior remains in place.

The old coordinate-based simulation entry point is retained for compatibility and
terrain validation; it also enforces Command Center capacity and assigns a slot.
Player free-placement commands explicitly reject and direct users to the window.
The window chooses slots and the simulation chooses compatible internal coordinates.

The native engine migration must port this gameplay change separately: level-zero
founding, timed foundation/upgrade rules, slot ownership/reservation, old-save mapping,
v18/v19 loading, and the read-only planetary accounting view. The current native
migration's earlier free-placement parity is not evidence of this new behavior.
No C++ migration checkout was changed as part of this implementation.

## Verification

Run `dotnet run --project tests/Game.CoreRuntime.Validation -- --planetary` for
Command Center/slot checks, save continuity and planetary catalog compatibility.
The full core suite also covers economy accounting, research, colonization, Player
and Developer saves, and construction allocation. The focused real-input UI capture
is `STELLAR_CAPTURE_FOCUS=planetary-window` with `tools/ScreenshotCapture.tscn`.
It requires an isolated user directory containing `PlanetaryVerification` and uses
controlled fixtures for unbuilt centers and deficits. It tests actual buttons,
confirmation, signed negative balances, slot locking, construction, save and return.
The main surface acceptance journey now uses slots; historic screenshot filenames
are retained for the release archive. The immersive journey now opens planetary
management from the focused world rather than expecting a free-placement descent.

This feature branch and its local game preview are separate from the integrated
release and the native editor package. A clean-machine certification remains open.
