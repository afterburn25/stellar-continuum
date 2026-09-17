# Native home-system supply network

The compact navigation rail opens a read-only Supply Network workspace. Four
separate totals show available surplus, required imports, delivered supply and
shortfall. Every owned home-system location is listed with its facility kind,
status and three daily figures. These are abstract supply units, not tonnes or
currency; the labels do not imply a new economic rule. Tiny positive amounts
show <0.01 instead of misleading zeroes. This is a home-system network, not an
empire-wide trade or route editor.

## Authority and failure handling

This selectively adapts the read-only model from Devin ea9164e9. The upstream
permanent initializing fallback, separate repeated projections and eight-row
limit are not retained. No Core formula, Player17 field or C# source changes.

The observer must be the campaign's actual player, flagged as player, and have
a valid home system, economy and construction record. Missing prerequisites
produce Unavailable with a recovery message, never healthy zero totals. One
Core home_system_logistics call computes the network per admitted refresh;
its internal economy_logistics call is not duplicated. Totals are copied from
Core. Nodes outside the observer/home scope are excluded. An unexpected result
identity fails before exposing names or totals.

Failures clear stale data, display Retry and record the exception diagnostic
in the existing support log. A failed observer/generation is latched until an
explicit Retry or identity/generation change. Reopening the panel does not start
a failing retry loop. This does not suppress a failed query as a successful load.
The pause-menu diagnostics export remains available.

Opening/Refresh projects once. Automatic refresh is limited to once a second
while visible and only after simulation time changes. Paused redraws consume the
cached view. Opening again refreshes changes made from other workspaces while
paused. Campaign replacement closes the panel and clears the controller.

## Interaction and layout

The navy/cyan panel uses the game's drawable-pixel layout and measured text.
The header, Refresh/Retry/Close, summary totals and column labels remain pinned;
all location rows use bounded, clipped scrolling. Input capture owns drags through
release outside the panel and cancels on focus/pointer loss. Escape closes the
workspace. Pause/speed and compact navigation remain usable. Other workspaces
and battle activation dismiss Supply without changing the world or camera.

## Maintained validation

native_logistics tests observer authority, home prerequisites, canonical totals,
foreign-state mutations, more than eight nodes, projector exceptions, retry and
campaign identity replacement. native_logistics_workspace tests 720p/1080p
layout, measured UTF-8 rows, last-row reachability, clipping, pointer capture,
commands and suppression of stale data on failure. Unchanged table geometry is
cached; invalidation tests cover node names, viewport and the text measurer.

The export pipeline runs native_supply_runtime.validate_native_supply_export.
It invokes --smoke with --logistics-check and an isolated save from an unrelated
Unicode working directory with restricted PATH. Fresh 720p and loaded 1080p
Vulkan runs replay navigation, compare canonical totals, count projections,
prove paused caching, capture the table and scroll end, exercise drag isolation,
Refresh, Research switching and Close. The complete Player17 payload must remain
identical except SavedAtUtc. Proof records reject missing/duplicate keys and
non-boolean success claims. This is native input replay, not OS mouse injection.

See the current checkpoint in ../CPP_MIGRATION_HANDOFF.md for run evidence.
The working validation folder is unsealed; it is not a release download.
