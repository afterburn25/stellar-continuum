# Native surface construction assessments

Core surface placement and removal now have owned nonmutating assessments.
They expose the existing exact authorization/material terms, coordinates,
rotation, building identity and removal consequence. Commits re-assess the
current world before mutation. Existing direct commands share the same
preparation/apply path; geometry, capacity, pricing, denial order and timing
remain authoritative Core behavior.

Incomplete cancellation refunds half the current authorization cost, with no
refund of spent industry. Completed demolition gives no refund and still
requires a construction economy, as before. Numeric refund preparation moves
before erase because it depends only on body environment; the direct command
retains erase, credit and currency-format order, including the original
malformed-world exception boundary. The unchanged 95-case source oracle
checks the preserved commands.

`NativeSurfaceConstructionController` wraps these assessments for a known,
player-owned colony. A detached quote is bound to generation, player,
system/body/colony, displayed colony revision, and exact Core terms. Confirm
requires the caller's current generation and re-resolves ownership and knowledge
before using Core commit. A quote from before reload cannot target the new world
even when IDs coincide. Confirmation consumes its token on success or rejection.

Only the latest preview token is retained. Moving a placement preview or
preparing a denied placement invalidates the prior token without accumulating
memory. Cancelling a preview changes only client state. Current funding,
geometry, capacity, IDs, completion state and consequences are checked again;
no UI calculation can authorize a stale price or refund.

Always-on tests cover quote immutability, direct-command equivalence, funding
loss, overlap/ID/completion changes, cancellation versus demolition, source
refund behavior, ownership/knowledge, current-generation confirmation,
single-token replacement, paid construction through CampaignFrame and paused
Player17 recovery. Strict Debug and Release checks supplement the maintained
source oracle; exact combined checkpoint results are recorded in HANDOFF.md.

This is controller groundwork. Mouse-driven surface placement, terrain/3D
presentation, upgrade/repair confirmation and construction audio remain open.
There is no automatic placement or new building eligibility rule here.
