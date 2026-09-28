# Older-campaign performance and 2D orbit map repair

The September 10 Windows build could become unplayable in an older campaign while a fresh game remained smooth. The affected 100-system campaign was 123 years old with seven civilizations and fourteen colonies. Its Windows log recorded 60 FPS paused and approximately 1 FPS running, across galaxy, system and surface views.

## Cause and repair

Idle AI survey ships evaluate destinations again when no supported work is available. Each destination assessment rebuilt the entire deterministic interstellar lane graph, including its cubic minimum-distance backbone search, and repeated shortest-path searches. As fleets exhausted nearby work or could not fuel a destination, the same expensive calculation repeated every simulation frame. This was not a corrupt save or a planet shader problem.

`InterstellarLaneNetwork` now shares derived lane geometry by campaign catalog using weak ownership, and keeps at most 64 shortest-path trees per catalog keyed by origin and leg range. Immutable stellar record identities are checked before reuse, including catalogs backed by mutable lists. Replacing/removing stars invalidates geometry and routes. Permission-filtered requests are evaluated afresh. Fuel, refueling settlements, ownership, survey information and order acceptance are never cached. Simulation timing, candidate ordering, routes and economic rules are unchanged.

Native support logs now include mean and peak simulation work time alongside FPS. The maintained screenshot tool has a `performance` focus that retains the supplied campaign and measures paused/running region, galaxy overview, orbital overview, planet focus, surface and return to orbit. Run it with isolated application-data directories and a copied save. It checks that time advances, captures frame pacing and writes `performance.json`; it does not satisfy the full gameplay screenshot gate.

## Evidence

On the affected save, a console reproduction of the actual campaign/core composition measured a repeated core step at 1,813 ms before and 3.076 ms after. Exploration dropped from 1,750 ms to 2.271 ms; colonization from 60.525 ms to 0.669 ms. Cold first-step work dropped from 3,789 ms to 81.995 ms. Measurements are local on an i9-12900K/RTX 3080 Ti; they are not universal hardware guarantees.

Simulation validation passed 70/70, including route parity against the previous destination-search algorithm, tied/zero-length routes, range limits, changed permission sets, geometry replacement/removal and cache eviction. Core Runtime passed 72/72. Existing travel/fuel, colony, research and save checks remain in place. The copied user save and diagnostic work files are private local fixtures, not repository content.

The repaired native Windows game retained that save and passed seven 1080p frame-pacing samples, exiting 0 with empty stderr. Running region, galaxy, orbital overview, planet focus, surface and return-to-orbits averaged 58.47–59.87 FPS, with 95th-percentile frame times below 16.81 ms. Every running sample advanced approximately five game days; the paused sample advanced zero. Individual worst frames reached 103.5 ms, so this is not a zero-hitch claim. Evidence is in the private `work/performance-aged-1080` folder. These measurements use the working candidate before the final wording/documentation commit.

## Requested system-map restoration

The system overview uses the earlier 2D orbital map again: textured bodies, visible orbit paths, local sky, mouse drag/zoom and aligned selection/orders. Detailed 3D planet focus and surface descent remain available. The native camera focus passed 28 named input checks, including station picking, planet picking after pan/resize, wheel entry/exit, focus recovery and privacy boundaries. Periodic snapshot refresh no longer reissues planet focus, which previously reset the wheel zoom target. Full gameplay validation and final package results are recorded in the repair PR.
