# Native outpost freight collection

An owned resource outpost's existing colony screen exposes **Collect materials**.
The player opens it from the surveyed planet inspector, using the same access path
as **Open surface**. No second missions board or settlement planner is introduced.
This selectively integrates the collection behavior from Devin `d9d23f57`.

Collect opens a paused review. It identifies the selected bulk freighter, departure
colony, outpost, cargo capacity, pickup stock and current extraction rate. It
explains that there is no upfront dispatch fee, ship upkeep continues, and materials
only enter shared stores after delivery and unloading. Core controls travel,
transfer, funding and return routing; clicking Dispatch does not transfer cargo.

The controller chooses the lowest-ID eligible owned bulk freighter already at a
developed colony whose copied-state Core preflight succeeds. Unreachable or
rejected lower-ID candidates do not block a reachable ship. Confirmation binds that exact ship; it never silently substitutes
a newly available vessel. Read-only preflight runs the existing coordinator order
on a copied campaign. A controller-held, single-use quote binds campaign generation,
observer, the owned surveyed outpost/body, departure colony, displayed facts, and
ship decision state. Confirmation revalidates those bindings and reruns Core
preflight before dispatch. Stale/replayed/foreign/ambiguous/busy or invalid requests
return a useful reason without an automatic retry loop.

Review and Dispatch require matched mouse press/release on the same action.
Cancel and Dispatch occupy distinct positions. Escape or lost pointer focus clears
the review; changing colony, observer, campaign or navigation also cancels it.
Review text wraps and scrolls while actions remain pinned at 720p, 1080p and 4K.
Main-menu and navigation behavior remain available without leaking clicks to the
underlying map. The dispatched notice uses the existing feedback/audio path.

The maintained exporter exercises the actual colony input path using isolated
Player17 fixtures. It authors a resource deposit and outpost on an unoccupied,
fully surveyed home-system body and an idle bulk freighter at the developed home.
This fixture is validation-only; generation and live campaigns are unchanged.
Review/cancel preserve the complete canonical campaign. Dispatch permits only
the quoted fleet's route/freight fields to change, with zero cargo transferred,
unchanged treasury and unchanged outpost stock. A second process proves full
paused reload equality. Exact per-capture BMP metadata validates review, dispatch
and reload screenshots. The smoke reuses the existing colony flags.

This work does not port the separate colony-sites mission board, change freight
rules, add ship models or implement a new cargo economy. The same-system graphical
fixture proves dispatch and persistence; interstellar routing and timed transfer
remain covered by the established Core freight parity suite.

## Verification (2026-09-16)

- MSVC native build and 8 focused CTests pass, including Core freight parity and
  controller/UI/colony/settlement/surface regressions. Controller tests include
  unreachable lower-ID selection, replay, stale generations, changed fuel and
  mission revisions, duplicate fleet/economy/player identities, unpaused confirm,
  revision exhaustion, multiple valid developed home colonies and invalid numbers.
- Python export discovery: 485 tests, 468 passed and 17 optional executable checks
  skipped. Actual executable validation below is separate from those skips.
- Five relocated Vulkan launches (fresh fixture base, dispatch and reload at each
  resolution) pass. Six review/dispatch/reload captures have exact BMP metadata.
  The 720p review and 1080p dispatch were inspected for clipping and action layout.
- Existing colony, surface construction/management/relief and colony/outpost
  settlement validators pass on the same native executable.
- Evidence: `work/native-freight-{build,final-build,ctest,python,runtime,regressions}.log`
  and `work/native-freight-{runtime,colony,surface,settlement}.json`.

Fixtures set zero-passenger species to null, matching canonical Player17, and
create the validation freighter explicitly because new campaigns have no ships.
The exported validation payload gate was not relaxed to hide normalization.
This local package is unsealed and is not a release download.
