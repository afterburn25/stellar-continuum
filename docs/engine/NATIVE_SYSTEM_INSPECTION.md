# Native galaxy system inspection

The native C++ galaxy map opens a system card on a single star click. The card
shows survey status, progress, distance from home and authorized survey findings.
It uses the existing map artwork and navigation rail. Its scrollable content stays
inside the panel; closing clears the selected star. Other workspaces and menus
hide the card without losing the selection.

## Review boundary

This selectively adapts Devin `d2a41caa`, rather than importing its main-loop
patch. That implementation selected the first colony in a system and exposed live
foreign population, infrastructure and stability after contact plus survey. It
also recomputed economy/logistics during inspection and swallowed failures.

`build_system_inspection` produces a value snapshot after validating the current
player and target. Unknown stars hide their name/class/traits. Detected and partial
contacts show the already-observed name, with details held until full survey.
Home distance uses the public star-chart coordinates, including depth when present;
this is an intentional exception to unknown-system redaction, consistent with the
requested navigational reference distance. Missing home position reports Unknown.

Full survey reveals stellar classification and survey traits. All player-owned
settlements are listed with their own body, population, infrastructure and
stability. A referenced body must belong to the selected system. Foreign settlement
existence, count, identity and live values are never disclosed: contact and survey
do not provide an observed-colony record. The generic foreign-intelligence notice
does not depend on whether foreign settlements actually exist.

The client refreshes the snapshot during update, including paused updates, and
clears it on campaign replacement. Drawing consumes the snapshot and does not
query or mutate Core. No economy/logistics recomputation or fallback projection is
performed here. No Core rules, Player17 schema or C# source are changed.

## Input and layout

The card begins to the right of the compact navigation rail and above bottom
status text. Title, survey status, progress and close control remain pinned;
findings and settlement rows scroll in a clipped content area. Content changes,
target/observer changes and viewport resizing cannot leave an invalid scroll
offset. Pointer ownership prevents map panning, zooming or fleet orders through
the card. Keyboard shortcuts remain routed by the owning campaign.

## Maintained verification

`native_inspection` exercises disclosure boundaries and card layout/input without
SDL or a GPU. Tests mutate hidden foreign state, revoke observer authority and
remove targets; these must not leave private information in a snapshot.

The graphical export path runs `validate_native_inspection_export`. It starts the
relocated native executable from an unrelated Unicode working directory with a
restricted PATH, using `--smoke <capture.bmp> --inspection-check` and an isolated
save. A fresh 720p run and 1080p reload click actual chart markers through the
game's input router, capture known/unknown cards and the scrolled end of settlement details, check bounded wheel/drag input,
open and close Research, dismiss the card, and compare the complete paused save
apart from SavedAtUtc. The in-process proof also compares the campaign, selection,
camera and clock before/after browsing. This is event replay, not OS mouse injection.

Evidence and validation results for the current checkpoint are recorded at the
top of `../CPP_MIGRATION_HANDOFF.md`. The working validation package remains
unsealed; these checks do not establish finished visual quality or sustained FPS.
