# Native surface building preparation

This checkpoint extracts Devin `58aaf475` building geometry and adds bounded
background preparation through the existing Engine `ImagePreparationQueue`.
It is maintained native C++ preparation code with tests. It is not yet connected
to the live colony workspace; the existing terrain, building meshes, placement,
selection and roads remain active. No new surface screenshot or performance
claim is attributed to this preparation-only checkpoint.

## State and authority

`SurfaceBuildingState` is an immutable presentation input. Normalization retains
exact canonical yaw and explicit construction completion, and maps progress to
three visual stages and condition to three visual bands. Routine progress ticks
within a stage reuse one cache key. Only explicit completion removes scaffolding,
including when progress is almost 100 percent. Power, enabled state, staffing,
hub level, capital and outpost appearance are explicit inputs; irrelevant flags
are removed from the normalized key. Invalid direct keys are rejected.

Geometry rotates before projection and retains the ground-center anchor. The
14 supported building identities use Core's catalog footprint, not a second
visual size table. Review caught a copied battery radius of 12 instead of the
canonical 14. Hub metadata uses Core's 24-unit exclusion radius independently
of the decorative hub geometry. Core, Player17 and road rules are unchanged.

Generator, lab, industry, trade, habitat, battery, cargo, advanced details and
hub geometry are adapted from the existing conversion. Water reclamation and
agriculture retain its industrial/habitat silhouettes. This is a bounded raster
of geometry through an oblique orthographic camera, not navigable 3D or final
species-specific colony art. Capital must eventually come from authoritative
observer-owned identity; the current colony view does not expose that field.

## Worker and cache contracts

The worker-friendly raster entry point takes only normalized state and a raster
specification. Both geometry and pixels are prepared off the owner draw path.
Typed failures contain a useful message. Limits are 8,192 triangles, 512 by 512
pixels, and eight million triangle bounding-box pixel tests. The level-three
hub exceeds the earlier proposed 4,096 triangles. Camera/model/projected values
are validated before allocation and integer conversion. The image/depth buffers
are allocated only after the work budget is checked.

`NativeSurfaceBuildingAssets::update` accepts the complete visible request set
once per owner frame and returns results in the same order. Duplicate normalized
requests share one immutable result. Its limits are:

- 130 visible requests, including a hub and preview;
- 48 cache entries and 24 MiB of cached image bytes plus pending reservations;
- four pending jobs and two new admissions per update;
- the shared Engine queue's existing independent admission and output limits.

When all needed images are pinned, excess requests defer instead of cycling
through eviction. Otherwise the oldest unused successful entry is evicted.
Scope changes clear old results; requests that leave the view cancel pending
work before collection. Already running canceled work finishes privately and
cannot be published. The queue may retain that canceled job's reservation until
it finishes. Returned image references held by a caller are outside the cache's
ownership accounting and must not be retained indefinitely.

The factory writes a result cell that is read only after `Ticket.take()` has
synchronized completion. Collection verifies state identity, dimensions, pixel
budget, finite in-image bounds and ground anchor before publishing Ready.
Invalid metadata and worker/submission failures become terminal Failed views.
Failed keys are not evicted or retried automatically, even when temporarily out
of view. Explicit retry or a scope change permits another attempt. Invalid raw
requests fail cheap validation without starting a job. The failure statistic
counts preparation/submission failures, not repeated invalid-input views.

The live host must keep existing meshes for Pending, Deferred and Failed views
and display useful failure feedback with an explicit retry action. This wiring
is still pending; the cache itself does not access a campaign or draw anything.

## Validation

Final MSVC `/W4 /WX` build and three CTests passed:
`native_surface_building_geometry`, `native_surface_building_assets`, and
`native_image_preparation`. Tests cover exact yaw, ground anchors, all catalog
footprints, late construction, operational states, invalid keys/geometry,
budgets, normalized-key coalescing, actual off-owner preparation, scope/request
cancellation, shared-queue backpressure, metadata rejection, failure latching,
explicit retry, LRU, pinned-memory deferral and owner-thread enforcement.

The matrix covers 14 families at two rotations and both 256/512 resolutions,
plus all three hub levels in unfinished/complete states at both resolutions:
68 variants. All fit the default image without clipping. Maximum measured work
was 1,932,632 pixel tests, below the existing eight-million cap. This is work
accounting, not a sustained frame-rate result.

Reproduce with the repository's MSVC environment and the normal preview build:

```text
cmake --preset windows-native-preview
cmake --build build-native/preview --target stellar_native_surface_building_geometry_tests stellar_native_surface_building_assets_tests stellar_native_image_preparation_tests --parallel 4
ctest --test-dir build-native/preview -V -R "^(native_surface_building_geometry|native_surface_building_assets|native_image_preparation)$"
```

Logs: `work/native-surface-building-stage-build.log` and
`work/native-surface-building-stage-test.log`. The extraction worker's earlier
isolated build is additional evidence, not a replacement for the final tests.

## Next integration gate

Connect requested state to the observer-owned colony view, prepare only visible
keys, and place images using projected ground anchors and camera scale. Keep
canonical footprint selection, preview rotation, placement costs and road
obstruction checks. Draw prepared structures in depth order above ground/roads
and below selection/status; pending meshes must remain coherent with that order.
The host must not infer capital from a display name or apply human architecture
as purported alien art. Validate actual mixed-building 720p/1080p captures,
rotation/preview, completed/unfinished states, paused save equality and cold/churn
owner-frame timing before claiming live surface integration.
