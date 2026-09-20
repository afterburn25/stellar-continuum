<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Ice optics, belt alignment and motion

Frozen small bodies now use view-dependent dielectric shading: reflective clear
patches, rough frost, refraction and thickness-dependent absorption. The shared
asteroid shape library includes long fractured forms as well as rounded bodies.
The solar-system view exposes the real campaign pause state beside its zoom
readout, with a Resume/Pause control connected to the existing simulation clock.

## ENGINE CAPABILITIES ADDED / EXTENDED

- `Material3D::dielectric` opts into reusable `Dielectric3D` materials. Default
  materials preserve the existing diffuse rendering used by planets and ships.
  Parameters include index of refraction, roughness, transmission, thickness,
  RGB absorption, reflection-light strength and surface relief.
- Engine shaders implement Schlick Fresnel reflection, GGX sun highlights with
  Smith masking, Snell refraction (including total internal reflection), and
  Beer-Lambert absorption along an approximate optical path. Reflection and
  refraction sample an immutable world-oriented equirectangular environment.
  Orthographic rays remain parallel; perspective rays use camera-space position.
- An optional RGBA map controls roughness, transmission, thickness and height.
  Derivative surface relief changes illumination and glints without changing
  geometry. A deterministic five-tap cone filter softens rough reflections.
  Longitude wraps while latitude clamps at the environment poles.
- Optical resources share the immutable texture cache and are counted in both
  per-scene and combined-frame admission checks before GPU allocations. The
  offscreen target still costs eight bytes per pixel. There are no new frame
  buffers, per-body targets, physics state or save fields.
- Application generates seamless frost/clear-patch maps for the five frozen
  material categories. Mixed rock/ice transmits less than the frozen bodies.
  A shared dark space environment and star-facing sun highlights light the ice.
  These are visual material approximations, not laboratory measurements of each
  frozen chemical. No glowing dust sheet or isolated-photo card is restored.
- Six of the 24 asteroid forms are elongated, on differing axes, with narrow
  widths around one-quarter to two-fifths of their length. The existing size
  distribution still supplies small, medium, large and rare huge bodies.

## Orbital alignment and motion

The earlier small-body chart projection added a fraction of orbital height to
screen Y, while its circular band guides did not. It also measured radius in XY.
The renderer now measures the full XYZ radius and flattens it consistently onto
the chart. Height remains in the solid scene's depth buffer. Inclined ice body
centers therefore remain within the same radial bands used by their guides.
Sol's saved outer ice belt remains at 32–50 AU from the Sun; it is not moved to a
planet's surface or rerolled. Enlarged asteroid silhouettes can cross a guide,
because the schematic intentionally exaggerates their sizes.

The new motion control reads the campaign's pause state and uses its existing
resume/pause commands. It does not introduce an independent animation clock or
animate physical orbital motion while the campaign is paused. The developer
replay now resumes a focused icy body, advances actual fixed ticks, verifies the
same mesh changes both position and full-axis rotation, then pauses and checks
that its orientation and saved time stay fixed. Earlier close-up replays only
captured paused scenes. Planet and star artwork keep their existing behavior.

## Ownership, compatibility and performance

Engine owns optical validation, shaders, samplers and resource accounting.
Application owns the ice appearance maps, shape mix, chart projection and UI.
Core's version-one field seeds, orbit/spin records, composition, resource yields
and depletion remain untouched. Rendering and inspection stay read-only.

The application caches at most 48 meshes, 32 albedos, 20 optical maps and one
environment (about 28 MiB of CPU image data). Frozen pixels add two material
samples and up to ten environment taps; legacy materials skip those calculations.
The field retains its 768 resolved-body cap and projected-size geometry LOD.
All GPU work remains on the renderer owner thread. Generated maps are immutable,
deterministic, and reused across frames. No SDK or shader compiler ships to players.

## Verification

See `IceOpticsValidation.txt` in the package for final results. Automated checks
cover optical parameter rejection, resource bounds, unchanged diffuse rendering,
real GPU Fresnel response, refraction direction, absorption, frost masking,
roughness response and cached uploads. Application checks cover elongated volume,
frost variation, non-glassy rock, inclined orbital bounds at multiple epochs,
spin, picking, survey restrictions, zoom and persistence regressions.
Packaged 720p and 1080p developer replays include close-up screenshots and actual
Resume/Pause motion checks, followed by developer field spawn and disk reload.

## ENGINE LIMITATIONS REMAINING

- Reflection/refraction use an environment map and a single surface interface
  with approximate thickness. Nearby asteroids and planets are not traced into
  one another; there are no multiple internal bounces, caustics, spectral
  dispersion or volumetric subsurface scattering.
- The environment is a shared procedural space map, not a live capture of the
  current star system. Roughness uses a bounded cone filter, not a full prefiltered
  radiance convolution. Existing renderer color/exposure conventions remain.
- The previous geometry LOD, 768-body cap, conservative picking and schematic
  planet/asteroid drawing-order limits remain. Developer fixed ticks can show
  stepped motion, and distant outer-belt orbits advance slowly at normal speed.

This report supersedes the diffuse-only ice limitation in `SmallBody3D.md`.
The separately named package preserves prior deliveries and ordinary saves.
