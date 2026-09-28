# Surface sprite integration review

Reviewed 2026-09-15: Devin `58aaf4751a69e9809e951d8084e3dd59e2f9232c`,
documented by `bfc51a23050a4347839d154c99a9ce401497f124`. This is a source
review against Codex surface checkpoint `f13192ea` and the retained C#
`src/Game/Presentation/SurfaceBuildingVisuals.cs`. The proposed sprites have
not been rendered or benchmarked by this review. They are not imported into
PR #332.

The reusable geometry is valuable: recognizable generator, laboratory,
industrial hall, trade hub, habitat, storage and upgraded hub shapes follow
the reference building families. Extracting those builders would let us retain
the working native placement, roads, clipping and diagnostic contracts.

## Corrections needed before integration

| Finding at reviewed commit | Consequence | Required integration behavior |
| --- | --- | --- |
| `native_surface_scene.cpp:254` image API and `native_surface_workspace.cpp:664,695` omit site/preview rotation. | Rotating a placed or previewed building does not rotate its sprite. | Pass the canonical rotation through geometry, preview, drawing and hit testing. |
| Workspace `:341,655` uses the same `34 * pixels_per_unit` footprint for every site. | Display/selection no longer follows the different catalog footprints. | Retain the current canonical footprint and transform contract; derive sprite bounds separately from the selectable ground footprint. |
| `phase_for_progress` at scene `:250` maps any unfinished progress above two thirds to phase 3. | Scaffolding disappears while construction is still incomplete. | Only authoritative `complete` selects the finished state. Preserve unfinished scaffolding through the last construction tick. |
| Image state carries powered/priority only; workspace `:639` always passes `capital=false` for hubs. | Shutdown, poor condition and capital identity cannot select their reference visual states. | Carry explicit required presentation fields from the authoritative view. Reference `UpdateState` retains scaffolding until completion, uses enabled/powered beacon state, and reports repair/workforce conditions. Do not infer these from sprite phase. |
| Cache misses rasterize synchronously in scene `:277,294`, called by workspace rendering. Its 48-entry map evicts `begin()` at `:265,293`. | Cold preparation and lexicographic eviction can create owner-thread work and repeated raster/upload churn. This is a static risk, not a measured slowdown. | Prepare through the existing bounded Engine job path; retain immutable results, coalesce requests and reject stale completions. Use a deliberate cache policy and measure cold/churn owner-frame cost. |
| Workspace `:636` connects every completed site directly to the hub. | Replaces the current connected, footprint-checked service routes with lines through buildings. | Keep the existing world-space road graph and its obstruction checks. Geometry integration must not replace road behavior. |
| `native_scene_raster.hpp:167` accepts unbounded size/geometry and nonfinite camera values; projected values are converted to integers at `:193` onward. | Invalid inputs can request excessive memory or cause undefined floating-to-integer conversions. `phase_for_progress` has a similar conversion before clamping. Current workspace calls use a fixed size, but the reusable API has no such guarantee. | Validate finite values and bounds before allocation/conversion; cap dimensions, triangles and work. Test invalid inputs and terminal failures. |
| The public renderer fixes every sprite at 128 pixels. | Enlarging these sprites cannot establish detailed close-up or navigable 3D parity. | Review actual 720p/1080p close-up captures and choose a bounded level-of-detail policy. Keep the presentation claim limited to rasterized sprites. |

The proposed tests establish pixel differences, nonempty images and a bounded
cache count. They do not establish rotation/footprint agreement, construction
completion timing, road preservation, cache reuse under real state churn or
owner-frame latency.

## Integration checkpoint to request

Expose the pure geometry builders independently from the workspace/cache.
Preserve the current Core rules, paid placement/cancellation, observer gating,
Player17 coordinates/rotation and road graph. Add the omitted state and input
guards, then stage prepared imagery through the existing Engine resource path.
Acceptance needs actual mixed-building captures at 720p and 1080p, unfinished
and complete states, rotated previews/sites, paused save equality and measured
cold/churn behavior. A count-only cache test or a static gallery is insufficient
to claim a finished 3D colony.
