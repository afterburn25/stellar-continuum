# Native fleet map and order adapter

This maintained adapter is a client-owned C++ projection and command adapter. It reads
the campaign owned by `CampaignFrame`; it never retains a campaign, fleet, or
service pointer between calls. A caller supplies its monotonically changing
campaign generation. Building a view for a new generation clears selection,
and selection, previews, and orders reject stale generations. Route previews
also carry the selected fleet's mission-order revision, so an intervening
order invalidates the old preview.

## Source behavior retained

`src/Game/Presentation/Main.FleetNavigation.cs` is the preserved presentation
reference. The map selects active fleets owned by the player. Screen-space hit
testing stays in the renderer because it owns camera transforms and role-marker
offsets. The adapter's `select_next_hit` accepts the renderer's hit IDs, filters
them to active owned fleets, sorts by ID, and cycles after the current selection,
matching `TrySelectFleetAt`. An outliner can use `select` for an exact fleet ID.

The view exposes active owned fleet identity, position, role, transit state,
route, speed, range, fuel, revision, own combat power, and Core's own-combat
status. These entries are player-owned; the renderer's established policy is
to draw them green. The adapter carries no color styling.

Foreign live `FleetState` is never projected. The only foreign entries are
historic combat-intelligence observations already recorded for the player.
They contain the recorded fleet ID, observed power, day, and evidence, but no
name or current position. An unobserved enemy therefore produces no contact.

## Authoritative routes and commands

Every preview uses the canonical operational-reach surface. Scout and science
fleets call `ExplorationSimulation::assess_operational_reach`; the other roles
call the shared `assess_operational_reach` service. The result supplies the
actual lane route, total distance, range/fuel blocker text, and authority flag.
The `estimated_transit_days` field is explicitly an estimate. It follows the
preserved presentation calculation at `Main.FleetNavigation.cs:209-211` using
the fleet's canonical strategic speed and `civilization_operating_funding`.
The adapter invents no cost, drive, or fuel calculation. The order call
re-enters the existing command surface, which validates the current campaign
again:

- Scout/science: `ExplorationSimulation::issue_travel_order`, whose canonical
  planner uses `require_survey_work = false` for plain map travel.
- Military: `GalaxySimulationStepCoordinator::issue_military_deployment_order`.
- Logistics: `GalaxySimulationStepCoordinator::issue_freight_transit_order`.
- Colony: `ColonizationSimulation::issue_transit_order`.

There is no military-deployment precommit API. Physical route blockers appear
in preview, while any additional military command-state denial is returned by
the canonical issue call without mutation.

## Focused evidence

`native_fleet_controller_tests.cpp` loads the actual-source-authored
`valid-current17` Player17 fixture and checks owned-only projection, exact own
status and power, hidden unobserved enemies, recorded enemy power, foreign
selection rejection without mutation, sorted repeated-click cycling, covered
scout plain-travel acceptance versus survey-only rejection without mutation,
route range denial without mutation, a linked authoritative colony route,
stale generation rejection, canonical route assignment, and transit progress
only after `CampaignFrame::advance`. A separate native fresh seed-500 campaign
must remain fleet-empty, preventing the client from inventing starting vessels.

Gate105B compiled under MSVC C++ latest with `/W4 /WX /permissive-`, UTF-8
and precise floating point in strict Debug (`/MTd /RTC1`) and Release
(`/MT /O2 /DNDEBUG`). Both configurations passed the focused actual-source
Player17 and native fresh-500 harness against the staged Gate106 exploration
command.

Frozen SHA-256 evidence:

- public header: `dd45bb129b392b96a800b43e042751136d0c9499753ba0f8d6c65db7ec165032`
- Gate105B source: `a7e461cea0da5c85eb1686ce51edc54b84c3121a0088aedf989ee14f1fd6d0dc`
- Gate105B tests: `fab29fbe2a87e1110a6bd43913cf3c06b7bf28151a4cc7ed1e3032cb1ce37a8a`
- actual-source Player17 fixture: `138cdda12594a77352294fe265f0632fcf9d3cae9deedbae869d51fe30ed8fcf`

## Maintained map workspace and export proof

Gate107 supplies green owned markers, a clickable outliner, fixed detail/preview/action regions and explicit route confirmation. UI contexts capture their own input. Preview state clears when the campaign generation, selection or mission revision changes. The renderer never places historic foreign contacts without authorized coordinates. System names are redacted in canonical messages as well as labels; longest complete matches preserve known names and shared prefixes. Stable co-located offsets are built in O(n log n) when the view refreshes, then projected in O(n) per frame/input pass.

Gate109 adds a maintained fleet export validator and nine negative/positive Python checks. The actual 20-system Player17 source fixture is test input only. Two graphical launches use normal outliner selection, right-click preview and Confirm, then advance, save, reload paused and save again. The first save must contain a newly ordered owned fleet, positive transit progress and later simulation time. The second must recapture the full payload unchanged except SavedAtUtc. Both launches must exit successfully and report save=ok. A rendered later frame can be beyond the saved frame, including in a subsequent travel phase; payload equality compares saved state directly.

The combined engine0.1.46 run passed 118 CTest and 57 Python checks, plus all six actual map/research/fleet launches. Ordered and loaded 1280x720 captures were inspected. The current point-map and fleet markers remain functional migration visuals, not detailed ship artwork or the finished game.
