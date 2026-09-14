# Native colony workspace

Engine 0.1.51 connects the owned colony controller to the C++ client. Select a
known owned body in its system, then Open Colony. Population, stability,
infrastructure, hub limits, support, workforce, reserves, power, surface
production, deposits and construction sites come from the detached canonical
colony view. Surface production is explicitly separate from empire treasury
and total income; rates use the player's sovereign currency.

At 720p and larger, colony details and the construction-site column scroll
independently with clipping and content-derived panel heights. Population uses
K/M/B units. Current food/water reserve days use stored population-days, not a
projected future balance. Back or Escape restores the same system/body. Pause
and speed retain the workspace. Body selection takes inspector focus without
discarding the fleet selected for later manual orders.

The controller rejects foreign or unknown colonies, detects changed generation,
and refreshes changed site identity and geometry. No campaign mutation occurs
from opening or closing this view. A missing/removed owned colony clears it.

Maintained pure input/layout checks cover clipped content, independent scrolling,
long site lists, changed lists, observer and generation gates, and actual
fleet-to-body-to-fleet inspector transitions. Two real Vulkan export launches
open fresh Earth at 1280x720 and reload its paused Player17 save at 1920x1080.
They exercise selection, Open Colony, Back, reopening, pause and speed, and
compare full save payloads excluding only SavedAtUtc. Negative export tests
reject malformed/truncated captures, missing known-system membership, identity
or reserve mismatch, missed input and changed paused state.

This is the telemetry workspace. Shared surface placement/removal assessments
and guarded native mutation controllers are implemented separately, but their
graphical placement actions remain open. Ship-delivered settlement is now
exposed separately in the system view; see NATIVE_SETTLEMENT_WORKSPACE.md.
It does not claim final 3D surface visuals, completed artwork, sustained 60 FPS,
or full gameplay parity. See NATIVE_COLONY_CONTROLLERS.md and
NATIVE_SURFACE_ASSESSMENTS.md; exact packaged evidence belongs in HANDOFF.md
and migration PR #325.
