<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Solid small bodies and solar-system magnification

This update replaces resolved asteroid cards with actual opaque triangle meshes.
Small, medium, large and rare huge bodies share the saved field's orbit, material,
variant and full quaternion spin. Ice fields use discrete fragments without the
stretched luminous dust or cluster pictures. Planets and small bodies can both be
zoomed to 55x; the persistent lower-right readout follows the actual chart scale.
The earlier system-view planet limit was 5x.

## Using the update

Open `Developer Game.cmd`, enter Sol, then choose **BELTS & DEBRIS**.
**Next large** starts with the largest qualifying body, then cycles through large
bodies in descending size. **Focus body** centers and magnifies the selected
object. The inspector identifies Small, Medium, Large or Huge. **Next field**
switches between the rocky and icy populations. Mouse-wheel or toolbar controls
zoom the chart; **FIT SYSTEM** restores the overview. Pausing freezes the saved
simulation clock, including all asteroid rotation and orbital motion.

The zoom number is the chart's camera multiplier: at 1x one presentation unit
maps to one screen pixel. This is a schematic chart, not a physical distance scale.
Magnification therefore has the same meaning while focusing a planet or an asteroid.

## ENGINE CAPABILITIES ADDED / EXTENDED

- `native_solid_mesh.hpp`: reusable `directional_solid_mesh` creates a closed
  deformed sphere from a caller-supplied directional surface. Its normals follow
  the actual surface derivatives, including elongated forms and the UV poles.
  Engine owns geometry validation and immutable mesh resources, not asteroid rules.
- `Material3D::light_direction`: optional normalized camera-space incident light
  per object in a shared scene. Existing consumers inherit their scene light.
  Small bodies point their lighting toward the system's star without changing
  when another object enters the viewport. The existing GPU lighting uniform,
  depth buffer and opaque triangle pipeline are reused; no new shader format.
- Application `NativeSmallBodyGeometry`: bounded cache of 24 irregular, cratered
  forms at two levels of detail, plus 32 seamless opaque material maps. Rock,
  metal, carbon and frozen material palettes retain the authoritative composition.
  Frost and dark inclusions replace the old isolated-photo background on ice.
- Application `NativeSmallBodyRenderer`: one shared 3D scene, viewport culling,
  projected-size LOD and at most 768 resolved meshes. Bodies below 2.2 pixels use
  faint unresolved markers. Above 28 pixels meshes use 96x48 tessellation;
  smaller solids use 24x12. Largest visible bodies receive priority at the cap.
  The CPU resource cache is bounded to approximately 7 MiB of meshes and 16 MiB
  of albedo maps; GPU resource copies and scene targets have separate Engine caps.
- The saved scale quantile maps monotonically to a presentation radius of
  1.3–78 units: about 70% small, 22% medium, 6.5% large and 1.5% huge. This is
  roughly a 60:1 visual size range. Core's version-one generator, RNG consumption,
  physical records, resource quantities and depletion ledger are unchanged.
- All representative body indices are visited at overview zoom, so index-prefix
  thinning cannot hide rare huge bodies. Rocky dust is faint and fades out by
  chart scale 0.18. Ice dust and cluster images are not submitted at any zoom.
- System workspace raises the general zoom cap from 5x to 55x, preserves the
  cursor anchor and expanded outer-system panning bounds, and displays the live
  magnification. Stellar artwork retains its existing screen-size cap.

## Compatibility and assets

No save migration or reroll is required. Rendering, selection and camera changes
do not mutate authoritative campaign state. The 36 supplied images remain
byte-identical in the package. Some rocky overview artwork is still used; resolved
bodies use seamless procedural surface maps because perspective photographs of
isolated bodies are not spherical texture maps. New Midjourney images are not
required for this update.

The new package has a separate name from the earlier Small-Bodies and Solar-Artwork
deliveries. Earlier installations and the ordinary smoke-save anchors are preserved.

## Verification

Automated coverage checks solid volume, derivative normals, full-axis rotation,
pause stability, immutable resource reuse, seamless material edges, the size tail,
absence of icy image haze, picking/culling, survey restrictions and zoom anchoring.
The GPU regression checks both inherited and per-object illumination, triangle
occlusion, clipping, stable resource uploads and scene budget validation.
The package validation file records the final test totals and graphical replays.

The replay inspects all nine Sol planets and their rotating globes, captures rocky
and icy close-ups and huge bodies, invokes the four developer spawn commands, then
saves and reloads the actual developer file to compare field records and clock.
The report is accompanied by reviewed 1280x720 and 1920x1080 captures, including
Earth beyond the previous zoom cap and the visible magnification indicator.

## ENGINE LIMITATIONS REMAINING

- Bodies use diffuse lighting with ambient fill; ice does not yet have refraction,
  subsurface scattering or physically based specular reflection.
- Geometry is selected from 24 deterministic shape variants. Detail changes at a
  projected-size threshold; no continuous mesh morphing or GPU instanced draws.
  Beyond 768 resolved visible bodies, the largest receive priority.
- The system remains a schematic orbital chart, with 3D depth among asteroid
  meshes. Planet sprites and chart overlays retain their existing drawing order.
  Size classes describe presentation, not newly invented kilometer measurements.
- Picking uses conservative projected bounds. No surface-level asteroid targeting,
  asteroid collision simulation, n-body gravity or automatic mining fleet loop
  is introduced here. Existing authoritative resource and environment hooks remain.

This report supersedes the billboard rendering and limited system-zoom discussion
in the earlier `SmallBodyFields.md` report. Other gameplay scope remains unchanged.
