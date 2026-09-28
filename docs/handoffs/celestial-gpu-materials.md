# Cinematic celestial GPU materials

Date: 2026-09-08. Branch: work/ui-player-experience. Baseline: 90a634e.

## Integration API

- `CelestialBodyMaterials.GetPlanetMaterial(SystemSpatialBodyMarker)` returns a
  cached per-body ShaderMaterial. Pair it with `WhiteTexture` on a TextureRect.
- `UpdateLighting(material, towardStar)` changes only a GPU uniform.
- `ReleasePlanetMaterial(bodyId)` removes cache ownership without invalidating
  references held by scene nodes.
- `FocusedPlanetView.SetBody(marker)`, `SetDiscRect(rect)`, and
  `SetLightDirection(direction)` provide the complete camera-positioned view.
  The rectangle is the solid planetary disc. Saturn rings extend to about2.16
  times its radius, with distinct back/planet/front draw order.
- `GetStarMaterial(color)` optionally supplies a controlled photosphere/corona.
  Its TextureRect should be `StarExtentMultiplier` (2.20) times the desired
  photosphere diameter.

The helper owns no camera, selection, simulation, or input. All helper controls
ignore mouse input so the owning canvas can route camera/selection gestures.

## Appearance and information boundaries

Planet shaders sample original cached NASA Texture2D resources directly. A focused
planet can grow to500px without enlarging a baked256px intermediate. Original
globe framing and 2:1 longitude/latitude map sampling share one projection table
in SolBodyMaterials. Uranus samples only the documented natural-color left disc.
No CPU pixel reads occur in the focused rendering or animation path.

A canonical texture requires both a recognized SurfaceKey and confirmed detailed
environment; unknown worlds remain neutral shaded discs. Confirmed non-Sol
classes may receive explicitly decorative material variation. Atmosphere and
ocean-reflection effects depend on confirmed visual class. These are cinematic
presentation effects, not new gameplay or scientific facts.

Original source limitations still apply: Venus has little intrinsic image detail;
globe photographs contain one observed hemisphere and pre-existing illumination.
The shader adds presentational lighting without inventing unseen surface maps.
See docs/SOL_VISUAL_SOURCES.md for all original image hashes and provenance.

SolBodyMaterials retains its CPU source/sample compatibility methods for the
existing bounded orbital thumbnail path. Canvas integration may migrate that
path separately; this slice does not edit the canvas or projection.

## Validation

Full actual production C# source compiled against real GodotSharp.dll with exit0;
only the four existing nullable warnings in colonization/exploration remain.
The shader files require actual Godot renderer validation on the combined
candidate. No visual-pass claim or Windows executable launch was made locally.
No source image bytes, camera/canvas files, simulation rules, or saves changed.
