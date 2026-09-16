# Native surface building presentation

The native colony surface uses the prepared building geometry described in
`NATIVE_SURFACE_BUILDING_PREPARATION.md`. This is a fixed oblique sprite layer
over the existing surface map, not a freely navigable 3D surface.

## Ownership and frame boundary

`NativeSurfaceBuildingPresentation` consumes only the observer-owned
`NativeColonyView`, viewport and detached placement quote. The host calls its
update once after input and simulation refresh. Geometry and rasterization run
on the existing Engine image queue. The renderer receives immutable completed
images through a value-captured provider; it never queries a campaign or starts
a preparation job.

The view carries homeworld identity from Core's home-system/body resolution.
Other colonies in that system do not receive capital ornamentation. If a later
environment no longer allows the original home-body resolver to identify a
world, the decorative capital variant is omitted without blocking colony UI.
These are shared prototype building families; distinct alien architecture is
still pending.

Requests contain visible sites, hub and an optional complete placement ghost.
The ghost requires matching campaign, observer, system, body, colony, view
revision and available type. Its yaw is the Core quote's normalized rotation.
Previewing, drawing and cache updates never authorize construction or charge
the treasury. Building completion, condition, staffing, power and enablement
continue to come from Core.

## Drawing and lifecycle

Projected ground anchors align each image to the canonical site coordinates.
Geometry already includes rotation, so the image is not rotated again. Roads
and fallback aprons draw first, then a single depth-sorted stream of ready structures
and individual fallback meshes, then selection and construction indicators. Ready images include their own projected
foundation, so the old flat apron is omitted to avoid a false raised-bowl effect.
Cosmetic road endpoints extend beneath foundations; cached routes and canonical
obstruction checks are unchanged. Cylinder and ellipsoid triangle winding is
outward and regression-tested.
Only a valid, visible ready image replaces its fallback. Picking, footprint
radii, road obstruction and route caching retain their existing rules.

Conservative admission bounds retain tall buildings whose ground is outside
the viewport; final drawing clips actual content to the terrain rectangle.
The projection uses stable 256/512 raster tiers rather than making a new image
for every wheel increment. The current player surface camera's maximum scale
uses the 256 tier; 512 remains covered for larger supported projection scales.
The preparation/cache bounds remain 130 requests, 48 entries, four pending
jobs, two admissions per update and 24 MiB of cache plus pending reservations.
Camera movement can cancel no-longer-requested jobs; stale completion cannot
bind to a new colony. Closing the surface releases bindings and clears work.
Failed jobs retain fallback meshes and show a reopen-to-retry notice instead
of retrying every frame. Reopening is the explicit retry action.

## Maintained evidence

Layer and workspace tests cover anchor placement, clipping, mixed-depth
fallback, exact yaw, stale result rejection, preview identity, footprint
selection and unchanged rendering when no provider is supplied. Presentation
tests cover request coalescing, viewport churn, scope replacement, capital
identity and latched failure.

`native_surface_runtime.py` checks the actual packaged Vulkan client at 720p
and 1080p: canonical placement/cancel/refund, unfinished progress, paused
Player17 reload, completed image counters, and matched captures with the image
layer disabled. RGB changes must be visible inside terrain and absent outside
the clipping region. The optional 120-frame profile includes balanced mouse
wheel and drag input; capture readback is excluded from frame timing.

Build/runtime results and remaining limitations are recorded in the current
migration handoff. No sealed release or sustained 60 FPS claim follows from
this presentation integration alone.

The validator also checks a clearly labeled test-only populated Player17
gallery derived from the unchanged fresh campaign after normal placement and
reload checks. Nine completed sites span generators, science labs, habitat
complexes and fabricators with enabled/disabled, priority and repair-condition
inputs. One unfinished generator anchors the existing reload input path.
All ten sites and the hub must be ready and drawn at 720p/1080p, with exact
paused payload preservation and clipped RGB comparison. The test removes only
its temporary backup so fallback recovery cannot supply false evidence.

This is not a dense gameplay-built city. Powered/staffed flags are Core-derived
and not directly diagnosed. At fit-to-all scale the building silhouettes remain
small, especially at 720p; visual finish still requires work.
