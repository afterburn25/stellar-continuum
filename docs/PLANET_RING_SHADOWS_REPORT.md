<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Planet and ring shadows — September 19, 2026

Both canonical planet views now support mutual shadows. Ring gaps and partial
transparency transmit sunlight onto the planet, including its separately rotating
clouds and atmospheric rim. The planet's tilted, flattened silhouette casts a
shadow across its rings. These follow the existing star direction and viewer pose.

## ENGINE CAPABILITIES ADDED / EXTENDED

Engine `AnalyticShadow3D` describes one world-space ellipsoid or transparent annulus.
`prepare_shadow3d` converts receiver geometry and camera-space light into the
blocker's normalized frame. World positions are subtracted as doubles first,
preserving close geometry at astronomical coordinates. The shader traces toward
the light and attenuates direct diffuse and specular illumination. Ambient,
emission and environment radiance remain independent.

Radial alpha uses eight fixed samples over each pixel footprint to suppress
fine-band moire on curved receivers. Thin rings opt into diffuse illumination
from either side; solid planet materials retain their normal day/night response.

Application `native_planet_materials.hpp` supplies ring/planet geometry from the
same canonical appearance that drives both views. The planet blocker matches
mesh flattening; the ring blocker uses the rendered radii and exact alpha resource.
Cloud rotation cannot move the ring plane. No Core/save schema, RNG, appearance
selection, source artwork or simulation timing changes.

No extra draw calls, shadow textures or shadow render targets are created for
planets. The existing ring alpha is bound again; generic caller-supplied maps
participate in scene and combined-frame resource checks. GPU uniforms are
192/208 bytes (vertex/fragment), with seven sampler bindings. Existing image,
geometry, target, instance and view budgets continue to apply.

## Validation

Automated coverage checks blocker pose and double precision, invalid descriptors,
ellipsoid silhouettes, rotated shadows, blockers behind the surface, ring holes,
gaps, partial opacity, scale invariance, parallel rays, emission preservation,
mutual visible geometry, immutable uploads and combined-frame admission. Planet
integration checks all three LODs, viewer tilt, oblateness, cloud-relative rotation
and emissive layers. Native replay verifies the actual imported ringed globe.
All 14 selected C++ regression suites and eight export tests passed. Packaged
1280x720 and 1920x1080 native replays passed, including canonical imported
materials, mutual ring shadows, save/reload and small-body motion/ice optics.
Both original anchor saves remained unchanged. Ringed-world screenshots from
both views were inspected. The build validation file records the tested executable
hash and replay summaries. The intentionally injected diagnostic fault at the end
of each replay verifies pause handling and is not a gameplay failure.

## ENGINE LIMITATIONS REMAINING

One analytic blocker and one effective directional star per material. Rings are
thin planes with authored radial alpha; this does not simulate their individual
particles, scattering, or finite-star penumbrae. Ellipsoids approximate the planet
silhouette; arbitrary terrain/mesh shadowing and interplanetary eclipses are not
implemented. Pixel edge smoothing is antialiasing, not physical soft shadowing.
Two-sided ring diffuse is a particulate-sheet approximation, not a scattering model.
The normalized receiver extent is bounded to one million units.

Other documented planet limits still apply: synthesized hidden terrain, estimated
material masks, approximate atmospheric shells and climate, discrete LOD, and
environment-based ice optics without nearby-object ray tracing or caustics.
