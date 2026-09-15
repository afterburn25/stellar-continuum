# Native shipyard workspace and commands

The maintained controller keeps Gate108B's canonical shipbuilding assessment and sovereign
currency projection. The full view revision still changes when the displayed
shipyard state changes, including treasury, industry, population, queue, or
build progress.

A Start command is bound to campaign generation and the last projected view
revision, then reprojects current authoritative state. It accepts ordinary
balance or unrelated progress changes only when the selected design's canonical
authorization terms are unchanged: player species and currency identity,
industry/credit/population costs, active-versus-queued placement, minimum
source population, and selected population source ID/species. Current balance
and population are excluded from that cached quote. The current canonical assessment supplies a useful denial,
and `start_ship_build` performs the final shared prepare/commit revalidation.
Currency, price, queue placement, or reservation-source changes require a fresh
view.

Cancel remains strict against the complete last-projected signature because the
workspace pauses and refreshes before arming its exact refund confirmation.
Successful commands consume the cached projection. No persistent Core pointer
is retained; the cache is an owned DTO and is discarded on campaign replacement.

Focused evidence includes affordable balance drift followed by an accepted
canonical Start, current insufficient-funds denial without mutation, currency
change rejection without mutation, assessed population provenance, exact queue
and refund behavior, CampaignFrame-only progress, owner-thread enforcement, and
generation invalidation. The focused harness passed with strict MSVC C++ latest
Debug and Release flags (`/W4 /WX`, UTF-8, precise floating point).

## Mouse workspace

The full-screen workspace separates known designs, design details, and active/queued orders. Costs, readiness, feedback and the action occupy fixed regions at720p, with clipped lists and wrapped text. It shows source sovereign currencies, exact population required before reservation, minimum full-rate build time, and canonical queue placement. There is one canonical civilization shipyard queue; the UI does not invent separate physical station queues.

Start/Queue always calls the controller and canonical command. The first Cancel click routes through the ordinary global pause control, refreshes the current cancellation quote, and arms confirmation. A changed revision clears that confirmation. Refund and reserved-population accounting remain Core-owned. Selection, stale notices and pending confirmation clear on campaign replacement. Research, shipyard, menu and map input contexts remain exclusive.

Gate111 strict Debug and Release checks cover layout, mouse actions, campaign replacement and locked states. Checks are always active under NDEBUG; the negative Release probe exits nonzero with the failed expression and line. Real720p Vulkan captures verify fresh500 locked behavior and a test-authored unlocked Player17 state. The latter proves running Start, automatic pause before refund confirmation, and saved canonical order state. Source-data mutation belongs only to the test scenario; normal games receive no injected unlocks or ships. It is not evidence of complete natural early-game progression.

Engine0.1.57 renders reviewed design artwork through `native_ship_art_assets`: design rows draw cover-cropped thumbnails and the details region draws a bounded portrait resolved by design_id with role fallback, decoded once per source under the shared 6-entry/4MiB cache budget.
