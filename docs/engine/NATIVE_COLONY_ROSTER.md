# Native owned-colony roster

The Colonies and outposts navigation icon opens a read-only list of the player's
settlements. Rows group colony name and kind, planet and system, population, and
a View action. Mouse-wheel scrolling keeps every row reachable. View reuses the
existing surveyed-system and owned-colony screens; surface building management
and outpost freight collection remain available there. Browsing never commands a
ship, moves the strategic camera or advances a paused campaign.

## Authority and refresh

The detached projection requires one actual player identity and unique canonical
colony, body and system IDs. Only that player's colonies enter the list. World
and system names require known identity plus full survey; unavailable references
remain disabled with an explanation. Invalid population values are unconfirmed.
The live campaign is not changed during projection.

Open revalidates generation, observer, colony/body/system identity and current
ownership before using the existing system and colony controllers. Selection is
not a settlement, survey or movement order. Confirmation does not bypass the
existing observer admission rules. A changed row list, viewport or focus cancels
a pending mouse press. Actions require matching press and release inside their
visible regions; clipped row portions cannot activate through the header.

The roster projects on open or explicit Refresh. While visible, it refreshes at
most once per second after simulation time changes. Paused views reuse their
projection. Same-campaign population refreshes retain the scroll position;
identity changes clear it and discard the previous notice. Failures latch until Refresh or reopening; campaign and observer
changes discard the old view. It shares navigation exclusivity and input capture
with the existing workspaces. No separate mission planner is introduced.

## Integration scope

This selectively addresses the owned-colony discovery part of Devin e49f4df0.
Its separate mission board and fleet/site suggestion browser remain unimported.
Existing manual ship selection, exact settlement previews and construction rules
remain authoritative. No Core, C#, Player17 schema or artwork replacements.

The maintained colony smoke exercises navigation, bounded scrolling, Research
exclusivity, Refresh, rejected unpaired release and opening the original colony.
It compares the complete canonical Player17 payload, fleet selection and strategic
camera before/after browsing. Separate roster screenshots have per-file dimension
proof. Reload must retain the complete paused save apart from the timestamp.

## Validation checkpoint (2026-09-16)

The final MSVC build and 14 affected CTests pass. Python export discovery passes
475 tests with 17 optional executable checks skipped (492 total). Real Vulkan
colony replay and a second-process paused reload pass at 1280x720 and 1920x1080,
including separate roster captures with exact per-file dimensions. Both roster
captures were visually inspected. This caught and corrected the old system
inspector covering the roster; the maintained replay now rejects that overlap.
The complete paused Player17 payload, fleet selection and strategic camera stay
unchanged through browsing, selection and return to the colony screen.

Existing freight dispatch/reload, surface construction/management/relief,
colony/outpost settlement and navigation runtime suites pass on the same final
executable. Unit coverage additionally exercises 40-row scroll bounds, live
population refresh without scroll resets, non-vacuous stale-click rejection,
identity changes, clipped clicks and hover feedback through 4K layout.

Evidence is in work/native-roster-{build-final,ctest,python,runtime}.log and
work/native-roster-{runtime,freight,surface,settlement,navigation}.json. The local
package remains UNSEALED with a precommit embedded version. These checks do not
claim finished visual parity, sustained 60 FPS or clean-machine certification.
