<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Native C++ 3D rendering extension

Stellar Engine now has a reusable GPU 3D path alongside its existing 2D renderer.
The planetary screen is its first gameplay consumer. This extends the native
C++23 engine; it does not introduce a second simulation or a scripting wrapper.

## ENGINE CAPABILITIES ADDED / EXTENDED

### Public interface and ownership

`engine/include/stellar/engine/native_scene3d.hpp` exposes immutable `Mesh3D`
and `Scene3D` resources, indexed position/normal/UV vertices, mesh instances,
materials, quaternion transforms, perspective and orthographic cameras,
camera-relative matrix preparation, and conservative frustum culling.
`Scene3DView` can appear anywhere in either ordered world or UI command lists.
`Window::scene3d_statistics()` reports uploads, submitted/culled instances,
retained resource bytes and viewport allocation bytes.

Mesh construction validates indices, attributes, triangle completeness and
budgets once. Scene creation validates camera, transform, material and combined
resource limits. Immutable data can be built on workers; GPU allocation,
submission and destruction remain on the window thread. Shared ownership
prevents stale pointer reuse. Cache eviction releases GPU handles through SDL's
deferred resource lifecycle. Failures throw actionable errors to the existing
application error boundary rather than silently dropping objects.

Absolute positions use doubles. Camera-relative subtraction happens before
conversion to GPU floats, preserving small local offsets at large coordinates.
Tests check a quarter-unit offset at an absolute position of one trillion.
This is render precision support, not a replacement for Core's coordinate or
distance rules. Graphics never modify saved game state or simulation timing.

### GPU implementation and 2D coexistence

`engine/src/native_scene3d_gpu.cpp` uses the existing pinned SDL 3.4.16 Vulkan
device: persistent vertex/index buffers, GPU texture uploads, a D32 depth target,
depth-tested opaque geometry, sorted transparent geometry, back-face culling,
optional double-sided materials, and per-fragment directional lighting.
Opaque instances draw before transparent instances. Transparent instances are
stable-sorted back to front by their centers and do not write depth.

Each viewport renders into its own RGBA target and composites in the exact
original command-list position. Premultiplied alpha preserves transparent edges
without dark fringes. All 3D views prepare before 2D commands are queued; this
matters because the pinned SDL GPU renderer retains its command buffer until
present. Merely flushing its draw queue would not submit that buffer. Separate
targets also prevent one viewport from overwriting another before composition.
The integration follows the pinned [SDL renderer implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/render/gpu/SDL_render_gpu.c)
and [SDL GPU API contract](https://github.com/libsdl-org/SDL/blob/release-3.4.16/include/SDL3/SDL_gpu.h).

Existing galaxy backgrounds, regional starfield, nebulae, routes, markers, text,
screenshots and UI keep their 2D interfaces and ordering. The supplied regional
starfield remains fixed behind map content; the overview retains its original
galaxy artwork. 3D GPU resources initialize lazily when a 3D view is first used.

### First integrated consumer and performance

`app/native_client/native_planet_globe.hpp` now shares one 128-by-64 sphere mesh.
Dragging and zooming update small transform/camera values. Surface, city-light
and cloud maps share geometry and use GPU materials. The globe keeps its
orthographic presentation, existing province selection, day/night controls and
observer visibility rules. Region projection is checked against the exact
rendering matrices at all supported UI sizes.

The pre-change path rebuilt and shaded up to three 8,385-vertex meshes every
frame. In the same 1,000-frame three-layer globe preparation benchmark on this
host:

| CPU work | Before | After |
| --- | ---: | ---: |
| Globe draw-list preparation, mean | 0.691710 ms | 0.021616 ms |

That is approximately **32 times faster (96.9% less CPU time)** for this operation.
The measurement excludes initial texture preparation, GPU rendering and present;
it is not a claim of a 32-fold whole-game frame-rate increase. The Vulkan test
also checks that repeated frames do not re-upload unchanged geometry or images.
Logs: `work/scene3d-globe-before.log`, `work/scene3d-globe-after.log`.

### Build and shader reliability

`engine/shaders/scene3d.vert` and `.frag` are the maintained shader sources.
`tools/compile_scene3d_shaders.py --compiler <glslang executable>` regenerates
the embedded SPIR-V header and digest manifest. Native CMake configuration
rejects stale source/binary pairs. LF checkout rules preserve those hashes.

Shaders target Vulkan 1.0 and were compiled with Khronos glslang 16.6.0. The
development compiler came from the official [Khronos repository](https://github.com/KhronosGroup/glslang),
with archive SHA-256 `1cd2fa4eb91d594263507e8644782320d7d2d936bc2d7d62dbbac4d07d88c76d`
verified against its published asset digest. The manifest records the compiler
binary digest/version and source/output digests. Ordinary builds are offline
with respect to these shaders; neither glslang nor a Vulkan SDK ships with the
game. The SDL dependency/version and runtime export layout are unchanged.

### Validation coverage

- `engine_scene3d`: mesh validation and outward winding; normalized normals;
  perspective/orthographic projection; quaternion and camera rotations;
  near/far depth mapping; wide-frustum culling; large-position precision;
  invalid cameras, materials, geometry and instance budgets.
- `native_scene3d_gpu`: actual Vulkan pixel captures of intersecting geometry
  submitted in both orders; near/far clipping; back-face/double-sided behavior;
  lighting; UV orientation; premultiplied transparency; 2D foreground UI;
  independent world/overlay 3D viewports; offscreen culling; scaled world target;
  stable uploads, cache pressure, target resize/release and budget rejection.
- `native_planetary_screen`: real 3D submission with immutable geometry reuse,
  region projection alignment, selection/drag/zoom, observer restrictions,
  modal routing, and layouts from 720p through 4K.

The complete native build and **212/212 CTest regressions passed**, including
10,000-system generation, existing saves and backgrounds. Evidence is in
`work/scene3d-final-build.log` and `work/scene3d-full-regression.log`.

The actual game also passed `validate_native_planetary_export`: construction
review and cancellation stay read-only, accepted construction reserves the real
slot, save succeeds, and paused reload preserves normalized authoritative state
and restores the slot at 720p, 1080p and 1440p. Evidence:
`work/scene3d-planetary-validation.json`, `work/scene3d-planetary-runtime.log`.
Reviewed captures include:

- [1080p globe](../build-native/preview-planetary-1920x1080-planetary-0.bmp)
- [720p construction dialog over the globe](../build-native/preview-planetary-1280x720-planetary-2.bmp)
- [1440p reloaded globe](../build-native/preview-planetary-2560x1440-planetary-0.bmp)

Final hardening adds rejection of subnormal camera/scale values, scene resource
count boundaries and combined multi-view resource overflow before any upload.
The final complete rebuild and all five focused regressions passed; results are
recorded in `work/scene3d-budget-build.log` and `work/scene3d-budget-tests.log`.

The final executable also loaded an isolated copy of the existing 10,000-system
campaign, browsed overview/regional/system views, and completed a save while
paused. Runtime diagnostics confirm unchanged simulation day, the original
overview deep field/galaxy layers, and the supplied starfield in the regional
view only. The captures were reviewed for full coverage and ordering:

- [Galaxy overview](../work/scene3d-captures/map-10000.png)
- [Regional star map](../work/scene3d-captures/map-10000-regional.png)
- [System view](../work/scene3d-captures/map-10000-system.png)

Evidence: `work/scene3d-map-runtime.log`. This cold browsing run includes artwork
preparation and screenshots, so it is a visual/save regression rather than a
steady frame-rate benchmark.

## ENGINE LIMITATIONS REMAINING

- This is a working mesh renderer with an integrated globe, not conversion of
  every star-map, terrain, ship or battle view to 3D. It does not add 3D physics,
  collision, scene-file import, skeletal animation, shadow maps, PBR materials,
  arbitrary affine/nonuniform transforms or a hierarchical scene graph.
- Transparent objects use center-based sorting. Intersecting transparent
  surfaces are not order-independent. One camera-space directional light is
  supported; mipmaps and dedicated 3D MSAA are not implemented. The existing
  world-quality resolve still applies when a 3D viewport is in the world layer.
- Limits are explicit: 262,144 vertices / 786,432 indices per mesh, 4,096
  instances per scene, 8 viewports per frame, and 128 unique meshes/textures
  each across the frame. Geometry cache budget is 64 MiB, 3D image cache budget
  192 MiB (CPU owners plus estimated GPU pixels), and combined color/depth
  targets 128 MiB. Driver allocations, staging and in-flight commands are
  additional; these are not measured total-process or GPU-memory ceilings.
  Existing 2D image caching has its own budget. Exceeding a limit raises an
  error; it never silently truncates a scene.
  Camera near distance and orthographic height must be at least `1e-6`; uniform
  scale must be at least `1e-8` to keep GPU projection and normal calculations
  numerically defined. Render units should be selected for the local scene.
- The existing 10,000-system campaign ceiling, quadratic lane backbone and
  whole-document save-memory costs remain. Rendering improvements do not
  certify a larger campaign size or long late-game simulation performance.

Future native terrain, ship inspection and tactical views can reuse the same
3D resources, camera, material, depth and ordered-composition interfaces.
