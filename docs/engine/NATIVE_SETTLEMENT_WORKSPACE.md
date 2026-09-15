# Native ship-delivered settlement

The C++ system view accepts manual settlement targets from an owned, active,
populated colony or resource-outpost vessel. Select the ship, then right-click
an observer-visible planet. Other selected ships receive a concise notice.
The exact body is assessed by the existing campaign colonization subsystem;
the destination need not occur among its eight suggested opportunities.

The confirmation shows personnel, target, sovereign authorization, treasury,
confirmed route distance with metric units first, and the existing 30-day
colony or 20-day outpost establishment period. Travel is additional. Core
currently exposes no complete travel-time estimate here, so the UI does not
invent one. Preparing confirmation visibly pauses the clock for review.
Cancel/Escape closes the preview without issuing a mission or spending funds;
resuming simulation remains the player's choice.

Core owns expedition authorization terms. Initial colony/outpost missions cost
120/90 internal budget units, formatted in the player's currency. Redirecting
an already authorized mission costs zero additional authorization. Preview and
issuance use the same Core helper; the retained opportunity planner's separate
affordability rules remain unchanged.

Confirmation rechecks campaign generation, owner, vessel, mission revision,
passengers, design, currency, authorization state and exact target assessment
before calling the existing paid command. Changed knowledge, funding, reach or
target terms cannot reuse an obsolete quote. An affordable unrelated balance
change does not invalidate it. Arrival, settlement progress and vessel
consumption remain simulation-owned.

Normal status refresh reads only the selected owned vessel's live mission
state. It does not generate or sort settlement suggestions. Generic status
text exposes no hidden destination names. Client input clears map drag state
when opening the modal; captured releases cannot restart an earlier pan.
Mission reload resolves both in-transit destinations and current systems after
arrival. UI snapshots are copied before dispatching events that may refresh
the underlying view.

## Validation scope

The exact-target controller has strict Debug/Release checks for targets outside
the suggestion cap, both mission kinds, initial and retained authorizations,
generation/owner/knowledge/funding changes, and detached preview data. The
maintained colonization oracle retains 124 source cases and four native
boundaries. These source checks use C# only as the preserved behavior oracle;
the production implementation and runtime are C++.

The settlement exporter creates its own fresh 500-system Player17 base, then
authors two isolated copies with fully surveyed knowledge and one populated,
funded, high-speed vessel each. Five native launches cover the base, real
720p order/cancel/confirm for both kinds, and their paused 1080p reloads.
The proof checks canonical charge, destination, owner, passengers, revision,
positive time, incomplete establishment and whole-payload reload equality
apart from SavedAtUtc. Negative tests reject missing interaction, invalid
renderer/capture, identity, knowledge, charge or saved-state evidence.

The authored vessels bound the proof duration; they do not establish natural
research/unlock progression or normal travel pacing. Zero-cost retargeting is
covered by native controller tests, not claimed as a graphical smoke action.
Exact completed integration/export counts belong in HANDOFF.md and PR #325.
Final graphics, sound, full gameplay parity and sustained 60 FPS remain open.
