<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Planet texture filtering — September 19, 2026

Fine planet textures and radial ring bands now use a complete chain of filtered
GPU texture levels. As a feature becomes smaller than a screen pixel, trilinear
sampling blends the appropriate levels instead of repeatedly sampling the full
resolution image. Zooming closer restores available source detail. This shared
renderer change also applies to asteroids, ice and other native 3D materials.

## ENGINE CAPABILITIES ADDED / EXTENDED

Engine's `texture_mip_layout3d` computes the exact RGBA8 chain and resident byte
charge for an immutable image, including odd dimensions and 1D ring profiles.
Scene admission, combined-frame admission and GPU cache eviction consume the
same layout. A null image retains the existing 1x1 white fallback.

The SDL/Vulkan adapter allocates the full chain, uploads the base pixels, ends
the copy pass, and generates the remaining levels on the existing GPU command
buffer before submitting it. This happens once per cached image upload, on the
window owner thread. Trilinear samplers expose the complete chain. Render targets
and depth attachments remain single-level; no extra view or recurring pass is
introduced. Shared base, shadow and material resources retain identity-based
deduplication.

Each level is generated with an explicit linear blit whose dimensions are
clamped to at least one. This avoids the zero-height/width regions in the pinned
[SDL 3.4.16 Vulkan mip generator](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/gpu/vulkan/SDL_gpu_vulkan.c#L9227).
The regression caught uninitialized samples on a 1024x1 ring map; horizontal and
vertical 1D maps and odd rectangular tails are now covered.

Cloud sampling now lets the sampler wrap longitude without a discontinuity in
the pixel derivatives. Equirectangular environment lookups use shortest wrapped
longitude derivatives at the atan seam. Alpha rejection happens after derivative
evaluation, including the ice optics response.

The existing 192 MiB/128-entry texture budget includes retained CPU base pixels
and every GPU level. Square power-of-two chains add about one third to GPU image
bytes (about one sixth to the combined CPU/GPU charge); thin 1D chains approach
twice their base GPU bytes. The cache may therefore retain fewer large images.
Over-budget individual scenes and combined views are rejected before uploads.
The existing bounded base-image staging allocation remains transient.

No imported assets, physical state, classification rules, seeded generation,
save fields, campaign time or application material selections change. Both
System View and Planetary Screen inherit the filtering through Scene3D.

## Validation

GPU regression captures check convergence of a minified checkerboard to its area
average, stability under subpixel translation, thin alpha bands and retained wide
ring gaps, close zoom, cache reuse and shared surface/shadow accounting. Additional
checks cover cloud wrap and refracted environment seams. Allocation tests include
odd rectangles, vertical/horizontal 1D profiles, exact budget rejection and
cross-role deduplication. Existing lighting, ice optics, mutual ring shadows,
depth ordering and cache tests remain enabled.

All 15 selected C++ regression suites and eight planet export tests passed.
The packaged game completed developer replays at 1280x720 and 1920x1080: eight
planet classes in both views, canonical portraits, ring shadows, asteroid orbit
and tumbling, pause/resume, ice optics and save/reload. Both original anchor saves
retained their hashes. The final diagnostic-pause event is an intentional fault
injection in the replay. Ringed-planet captures at both resolutions and the 1080p
ice close-up were visually inspected; the old ring interference pattern is
reduced and the main transparent gap remains visible. The package records results
and shader hashes in `Documentation/planet-validation.json`.

## ENGINE LIMITATIONS REMAINING

Filtering is isotropic trilinear RGBA8 UNORM sampling. It does not add anisotropic
filtering, temporal antialiasing, texture compression, linear-light color import
or alpha-weighted color mip generation. Channels are averaged independently so
packed height/property alpha retains its meaning; translucent assets should keep
appropriate color in their transparent texels. Normal maps are averaged encoded
channels and normalized during shading, without roughness variance compensation.

Geometry silhouettes and very oblique surfaces can still alias. Existing discrete
material/mesh LOD transitions remain. Ring geometry is a thin annulus, without
individual particles or scattering. Multiple-star illumination still needs Core
companion positions and physical properties. Ice reflection/refraction samples
an environment map, without local-object ray tracing or caustics. Unseen planet
hemispheres remain inferred from the supplied single-view artwork.
