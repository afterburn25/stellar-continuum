# Earned native settlement

This continues the seed-115501 Terran campaign from its first full science survey
at day 6587.15625. It does not author a vessel, edit a save, grant money or
technology, reveal unexplored bodies, or alter Core settlement rules.

## Exploration and paid preparation

`stellar_native_earned_settlement_tests` is a maintained CMake console target.
Its optional `--earned <absolute Player17>` mode selects the shortest reachable
observer-known route, breaking ties by system ID. Every route node must already
be known. A scout completes reconnaissance and the science ship completes the
detailed survey before any planetary suitability is inspected. The traversal
is bounded to 24 new systems, 5000 additional days and 300 wall-clock seconds.
An unmet requirement is a terminal diagnostic and nonzero exit, not a successful
test or permission to change the rules. Default CTest checks are explicitly
labelled boundary checks; they do not claim to execute this earned campaign.

The actual run visited systems 2, 3 and 8. Only after the full survey of Ross 154
did it select Xanthe (body 8004), a naturally viable world. Canonical paid
shipbuilding produced colony vessel 2 with 250 million settlers. The exact-body
settlement quote charged 120 simulation budget units in addition to the ship's
separate purchase cost. Arrival started a fresh establishment timer; 30 workdays
later Core appended colony 9 and consumed the vessel. The retained inactive
vessel has zero passengers. Its mission revision increments once on consumption
because both the C# reference and native Core clear its route.

The headless continuation reached these checkpoints:

| Checkpoint | Simulation day |
| --- | ---: |
| Eligible surveyed site | 6627.1875 |
| Paid populated vessel | 6725.765625 |
| Partial establishment | 6732.4375 |
| Founded colony | 6762.421875 |

These are accelerated validation steps using Player policy and exact Normal
1/64-day frames. They do not establish real-time pacing or frame-rate targets.

## Mouse controls and completion

The existing settlement smoke can also start from the earned populated-vessel
checkpoint. It selects the owned vessel, right-clicks the surveyed planet,
reviews and cancels a quote without charge, then confirms a new quote through
normal input. Confirm and Cancel require a matching press and release. Leaving
the control, focus loss, resize, a replacement quote or opening the menu cancels
the pending press. The visible modal consumes its pointer events.

`--settlement-completion-smoke <bmp>` loads that authorized expedition, advances
normal Core frames through travel and establishment, and verifies one new colony,
vessel consumption, unchanged orders until the final route clear, and the normal
ColonyFounded feedback event. It captures establishment after at least five
workdays, opens the new colony through its planet action, and explicitly saves.
`--settlement-founded-smoke <bmp>` reloads the result while paused and verifies
full Player17 equality except the save timestamp. Both modes require `--load`.
The completion replay is bounded to 1024 days and 300 wall-clock seconds.

The target planet inspector now shows the selected owned expedition's current
status and establishment workdays. It no longer advises preparing another vessel
while that exact world is being settled. Unrelated bodies and unsurveyed planets
do not receive mission details. Unchanged status does not rebuild the inspector.

## Final graphical continuation evidence

The actual 720p graphical order reviewed and cancelled the funded quote for 120
simulation budget units ($1.2B UED)
without charge, then confirmed the exact Xanthe mission. Latest graphical run:
day 6726.7884387 to 6762.4134387 in 2280 Normal 1/64-day steps, with the
ordinary 5/30 establishment timer, colony entry and normal ColonyFounded
feedback inspected. The paused
1080p founded reload matched the full Player17 payload except `SavedAtUtc`.
Production modal press/release/cancel and inspector status/readiness fixes are
part of this checkpoint. The rebuilt MSVC run and all 10 focused CTests pass.
Python final validation reports 577 checks (560 pass, 17 optional skips). The
maintained earned wrapper, navigation, system, preparation and authored ordering
checks pass, as do four CLI cases.
The four malformed/no-overwrite CLI cases pass; evidence is in
`work/native-earned-settlement-cli.log`. Prior CI run d090
(`35093767829`) passed.

The maintained `native_earned_settlement_runtime.py` validator is wired into
`stellar.py` after earned survey and preparation checks. Its CMake checker comes
from the build directory; it is not a packaged game dependency. Evidence is in
`work/native-earned-settlement-export-final.log` and `-export.json`, with final
build, CTest and Python logs under the same prefix. Four graphical images and
seven Player17 checkpoints are retained. The wrapper binds arrival's cleared
travel destination and the consumed vessel's final route-clear revision, and
checks the immediate expedition charge separately from later economy ticks.

The next earned source is
`work/native-audio-validation/package-settlement-founded-1920x1080.player17.json`:
player 0, system 8, body 8004, colony 9, 250M settlers, no surface modules and
2.0 initial power under existing Core rules. Continue through normal paid surface
placement, timed construction, operating output and save/reload.

## Boundaries and remaining work

Core, the C# reference, Player17 and approved art remain authoritative and unchanged.
The checks preserve source saves and retain separate output checkpoints. A local
relocated package is unsealed validation tooling, not a release download. The
system scene still needs visual finish, and a newly established colony needs its
first operational surface modules. This milestone proves establishment and
recovery; it does not claim a finished colony economy, campaign pacing, graphics,
or sustained 60 FPS.
