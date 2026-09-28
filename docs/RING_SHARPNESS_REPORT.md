<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Ring detail preservation — 2026-09-20

The old importer reduced ordinary ring renders to a 1024 × 1 radial strip.
Its 65th-percentile combination of 128 angular rays mixed slightly different
band positions and filled narrow gaps. Increasing output resolution alone could
not recover the discarded detail. Actual Bright Ice and Dense Banded captures
confirmed this loss against their supplied originals.

## Correction

`prepare_ring_material` now samples the original image directly into a
1024 × 2048 radial/angular material for all 162 approved rings. The fitted
ellipse and robust statistics locate the ring bounds; they no longer collapse
the visible texture. Broad angular illumination is compensated with a bounded
gain, while authored color, narrow bands and irregular particulate occupancy
remain. Matching first/last angular rows prevent a texture seam. Originals and
approved artwork identities are unchanged; no invented sharpening/noise is added.

The shared live planet assembly uses 512 annulus segments at close range and
opts into `Material3D::anisotropic_texture`. The Engine GPU adapter owns one
additional reusable 8x anisotropic sampler. Mipmaps stay enabled, avoiding the
shimmer that forced sharp sampling would cause at distance. Other materials
retain their existing filtering.

Saved ring thickness now drives a closed annular slab with correctly wound top,
bottom and edge surfaces. Back-face culling avoids doubling alpha through two
parallel faces. Physical thickness stays small instead of inflating a ring into
a thick decorative band. The geometry and real-renderer tests pass.

## Validation

- `engine_ring_material`: an intentionally nonconcentric narrow gap survives
  in all 1024 sampled directions; angular detail varies by 21.93 alpha levels
  per row instead of being averaged away. Blank rejection, seam and allocation
  checks pass.
- `native_scene3d_gpu`: a resolvable radial pattern's mean contrast rises from
  0 to 74.11 with anisotropy. Far minification converges to the expected average,
  with no duplicate texture upload. Depth, transparency, mutual shadows and
  rectangular mip-tail regressions also pass.
- `native_giant_visual`: actual shared-renderer captures for ten ring families,
  close/shallow views, nine giant subclasses and Pluto; live developer controls
  and full campaign save round-trip pass.
- `native_planet_materials`, `native_system_workspace`: pass.
- All 162 regenerated face-on previews reviewed. These previews expose source
  irregularities as well as detail; they are not evidence of per-particle physics.

Evidence: `work/ring-sharpness/before`, `work/ring-sharpness/after-samples`,
`work/ring-sharpness/review`, and `build-native/preview/giant-test-captures`.

## Cost and limitations

A close ring costs 8 MiB of CPU base pixels plus approximately 10.67 MiB of
GPU mip levels. Lower LODs remain proportionally smaller. System View admits
at most two close and six medium-or-better planets, leaving space for other
views/effects within existing 96 MiB material / 192 MiB GPU resource budgets.
Eight-way overview captures use appropriate medium LOD instead of loading eight
close maps simultaneously. No save, generation, clock or orbit changes.

Detail still depends on the original source and on-screen pixel size. Strong
baked source occlusion and nonplanar artistic source geometry cannot be perfectly
recovered from one image. Rings remain textured annuli with analytic shadows;
this change adds no individual-particle simulation or physical volume scattering.
