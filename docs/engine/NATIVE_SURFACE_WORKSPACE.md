# Native operational surface workspace

Engine 0.1.53 exposes construction on an owned solid world through Open Surface
in the colony panel. The view renders the canonical 1,024 by 1,024 surface
coordinate area, reserved hub footprint, existing sites and actual construction
progress. This is a functional top-down view; detailed 3D terrain, roads and
building artwork remain separate migration work.

The 0.1.58 Codex candidate adds the existing approved ground albedo to this
top-down workspace. The app decodes one immutable source through its shared
image-preparation queue, with a 16 MiB maximum; the workspace receives only that
image and never accesses files, a worker, or Core to draw it. A missing/invalid
source retains its terminal exception until explicit queue reinitialization,
so repeated polling cannot create a retry loop.

Ground tiles use the same world coordinates as placement and drag/zoom. Their
base span is 512 units; a power-of-two level bounds the visible draw list to 64
images at extreme zoom-out. Every image is clipped to the terrain field. The
generic source is desaturated because the current surface view does not provide
environment classification; it does not assert that alien worlds have Earth's
climate or biology. Grids, hub, sites, placement previews and costs remain above
the artwork. Detailed buildings, roads and true 3D terrain are still outstanding.

Build and export paths verify the exact image and provenance note in
`export/native-surface-art-assets.json`. The source note has a pinned Git line
ending so a fresh Windows checkout preserves its reviewed hash. The focused
`native_surface_art` CTest checks actual asynchronous decode/reuse, terminal
errors, owner-thread access and clipped camera-aligned tiles from 720p to 4K.
See `NATIVE_SURFACE_ART_SOURCES.md` for the unchanged image's provenance.

The palette uses only the colony controller's available buildings and their
descriptions, sovereign authorization, industry, workforce, power and footprint
data. Left-drag pans and wheel zoom stays anchored at the pointer. An exact
Core assessment determines whether the ghost can be placed. Pointer motion is
coalesced to at most one preview request per frame; client geometry never
authorizes a building.

Placement and removal require an explicit paused review. Cancel or Escape
discards a detached quote without spending money. An unfinished site's removal
shows cancellation/refund wording; a completed site's removal shows demolition
and its canonical zero refund. Research, available materials and authoritative
construction progression remain in charge. The interface invents no completion
time or instant construction. The current candidate adds reviewed canonical
upgrade, repair, operation and priority controls; see NATIVE_SURFACE_MANAGEMENT.md
for authorization, UI and updated validation contracts.

Generation and revision changes invalidate stale quotes. Integration review
fixed a queued pointer preview overwriting a confirmation revision, cleared
gestures on transitions, aligned square-site hit targets with their painted
bounds, and clipped palette text as well as card backgrounds. The colony panel
does not render underneath the surface workspace. A thin underlying system
border remains visible at the outer margin and is a presentation follow-up.

Maintained UI tests cover 720p through 4K layout, pan/zoom invariance, input
capture, cancellation, quote invalidation and site removal. Completed-site
demolition is covered by controller tests; it has not been claimed as a
graphically exercised flow.

The export validator adds 40 negative/positive Python checks and three actual
Vulkan launches: standard fresh 500-system Earth, 720p mouse placement and
cancellation, then an isolated paused 1080p reload/resave. It injects no extra
currency, research, materials or capabilities. Power-generator authorization
is 25 internal budget units and cancellation returns 12.5; public display uses
the preserved sovereign-currency conversion. The final site retains exact
identity, coordinates, rotation, progress, treasury and simulation day. Entire
save payloads must match on paused reload except the new save timestamp.

The native renderer also accepts immutable images in its ordered interface
layer. Real GPU pixel readback tests panel/image/control order, clipping, tint,
shared texture reuse and invalid source rectangles. This provides the renderer
support for the staged race-selection portraits without prematurely activating
that new-game workflow.

Combined Engine 0.1.53 validation passed 138 CTest, 206 Python checks and 26
actual Vulkan launches. Surface captures at 720p and 1080p were inspected.
Exact clean-commit package evidence is recorded in HANDOFF.md, draft migration
PR #325 and coordination issue #324. These checks do not certify sustained
60 FPS, clean-machine compatibility or final graphical/gameplay parity.
