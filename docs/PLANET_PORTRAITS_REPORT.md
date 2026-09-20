<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Canonical planet portraits — September 19, 2026

Small portraits now include the saved axial tilt, initial rotation, polar
flattening and rings of the planet they represent. Ring gaps remain transparent,
the planet hides the rear ring, and the front ring blends over the surface.
Both components cast shadows on each other. These portraits are used by the
planetary screen, system sidebar and colony command HUD through the existing
canonical material cache. The 47-folder import and all classification rules are
unchanged.

## ENGINE CAPABILITIES ADDED / EXTENDED

`SphericalThumbnailOptions` and the corresponding
`spherical_material_thumbnail(images, options)` overload provide a generic,
worker-safe orthographic ellipsoid/annulus preview. It consumes immutable RGBA
maps, an orientation, polar radius, ring radii and a light direction. Engine has
no planet taxonomy or campaign dependency. The original import-preview overload
is retained so accepted art exports do not change.

The CPU renderer intersects each view ray with the ellipsoid and ring plane,
orders their visible surfaces, and evaluates directional diffuse illumination
and mutual analytic shadows. Spherical longitude wraps at its seam; ring alpha
uses radial U and azimuth V, matching `Mesh3D::uv_sphere` and `annulus_mesh`.
Premultiplied interpolation/compositing is converted to straight-alpha output to
avoid dark edge fringes. Four subpixels per pixel resolve silhouettes; eight
bounded radial samples filter fine ring bands. Framing fits the projected
ellipsoid and tilted ring with a margin rather than cropping the ring.

Application supplies the existing canonical surface, cloud, emission and ring
maps. Portraits and live meshes share the same pose, quantized polar-radius and
ring-radius helpers. Clouds and emission retain their configured strengths.
Portraits use the saved reference pose at day zero and neutral preview light;
live planets continue to rotate with campaign time and actual star illumination.

No save fields, RNG draws, source assets, simulation rules or gameplay timing
change. Rendering stays on the existing single material worker. Each 96x96 RGBA
portrait occupies 36,864 bytes and remains in the existing 80-entry/96 MiB CPU
cache; submitted UI images retain the existing GPU image budget. Cached icons
are reused without regeneration each frame. Generic output is bounded to
16–1024 pixels; invalid dimensions, transforms and layer projections are rejected.

## Validation

Focused tests check flattening and rotation against expected silhouettes,
front/rear ring ordering, alpha blending outside the planet, transparent gaps,
uncropped framing, shadows on both objects, determinism, edge-on rings and invalid
inputs. Material integration checks the canonical pose, shared ring alpha and
radii at every planet LOD, all three supplied ringed-giant families, and reuse in
the bounded cache. Native replay checks that all eight inspected planet classes
actually submit their cached 96-pixel portraits, with save/reload unchanged.
Final test and packaged replay results are recorded in the build's
`Documentation/planet-validation.json`.

All 15 selected C++ regression suites and eight planet export checks passed.
Packaged gameplay replays passed at 1280x720 and 1920x1080, including imported
planet materials, cached portraits, ring shadows, asteroid motion/ice optics and
save/reload. Both original anchor saves remained unchanged. Ringed-planet screens
at both resolutions and the three ringed-family reference portraits were visually
inspected. The final replay's intentionally injected diagnostic fault tests pause
handling and does not indicate a gameplay failure.

## ENGINE LIMITATIONS REMAINING

These are compact reference portraits, not live camera feeds: time, current star
color, detailed normal/specular response and atmospheric rim shells are not
rendered into them. Clouds are composited on the surface at their reference pose.
Ring profiles and ellipsoid shadows share the existing approximation; there is
no particle scattering, finite-star penumbra or arbitrary terrain shadowing.

Multiple-star lighting still requires authoritative companion positions and
physical properties: current Core systems store companion classes, but only one
physical star record and no companion orbit. Existing planet limitations,
including synthesized hidden terrain, discrete LOD and environment-only ice
refraction, remain as documented in the planet and ring-shadow reports.
