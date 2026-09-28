# Native economy workspace

The native C++ client exposes the player's Sovereign Treasury through the compact
left navigation rail. This selectively adapts Devin's `810d2a6d` economy work
without changing authoritative Core rules, the C# reference, or Player17.

## Facts and choices

The workspace shows reserves, net/income/operating costs per simulation day,
stored materials and material production. Currency uses the actual player's
sovereign denomination; material rates are material units, never money.
Income is split into colony economy and surface trade. Operating costs show
administration, population services, habitat support, fleets, orbital and surface
maintenance, and research programs. Remaining reserved research funding is
displayed separately from daily research operating expense. Construction and
ship authorizations are capital costs and are not added to recurring expense.

Canonical Core projections supply all amounts, storage capacity, treasury
health, and industry allocation weights. A deficit, depleted treasury, arrears,
or genuine shortage remains visible. Materials prioritize infrastructure,
shipbuilding, or both equally when competing demand exists; spare capacity goes
to other work. Priority changes are free and reversible, reach Core immediately,
and persist in the existing player's `IndustryPriority` field.

## Authority and lifetime

The controller is simulation-owner-thread bound. The actual player observer,
home system, economy and construction record must each resolve uniquely. Missing
or duplicated data is unavailable, not an empty successful economy. Invalid
numeric projections fail visibly; technical details belong in Support logs.
Failure is latched until explicit Retry or a new observer/campaign. Older
campaign generations cannot replace the current view.

Priority commands require the displayed campaign generation, current revision,
actual live player, complete economy binding and unchanged current priority.
An accepted command consumes its revision; refresh issues a new actionable
revision even when the requested priority was already selected. No foreign
economy is mutated. Command feedback uses the existing status, event and audio
paths.

Automatic projection runs only while the workspace is visible, simulation time
has changed, and at least a second has elapsed. Paused frames reuse the view.
Explicit Refresh and opening the panel project once. A recent industry allocation
is presentation-only, cleared on campaign/observer or priority changes.

## Input and presentation

Header, Close, Refresh/Retry and allocation controls remain reachable while the
body scrolls. Text measurement and clipping preserve long amounts and explanatory
rows at 720p and larger viewports. Wheel/drag input cannot move the chart beneath
the workspace. A command requires press and release on the same control;
cancellation, campaign, observer or revision changes invalidate a pending press.
Economy, Supply, Research, Shipyard, Construction and Diplomacy are exclusive.

## Maintained verification

`native_economy` covers canonical projection, identity rejection, currency vs.
materials, invalid numeric data, failure/retry, stale generations and commands.
`native_economy_workspace` covers measured layout, clipping, scroll and gesture
ownership. `native_ui_layout` includes the new rail target and menu blocking.

The export runner includes `native_economy_runtime.py`. Its isolated real Vulkan
runs use the normal app input route at 1280x720 and 1920x1080, under a different
Unicode working directory and restricted PATH. They exercise the rail, complete
cost rows, scroll/drag, explicit refresh, workspace switching, three allocation
choices and normal saving. The client compares complete canonical Player17
before/after, permitting only the player's chosen industry priority to change.
The second run reloads it and independently compares the saved payload except
for its timestamp. Captures retain the actual packaged artwork.

This is a maintained native workspace and export gate, not a new economic
simulation or proof of final visual/performance parity.
