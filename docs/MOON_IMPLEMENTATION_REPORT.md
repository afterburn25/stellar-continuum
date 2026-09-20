<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Sol moons, stable spin and tracking — 2026-09-20

Sol now includes all 18 supplied major moons, alongside Earth's existing Moon:
28 bodies total. Each named image is used only by its canonical moon. Fresh
campaigns and legacy Sol saves share the same Core catalog; migration is
idempotent and preserves existing planets, resources and appearance records.

| Parent | Supplied moons |
| --- | --- |
| Jupiter | Io, Europa, Ganymede, Callisto |
| Saturn | Mimas, Enceladus, Tethys, Dione, Rhea, Titan, Iapetus |
| Uranus | Miranda, Ariel, Umbriel, Titania, Oberon |
| Neptune | Triton |
| Pluto | Charon |

## Images and presentation

Original files remain in the user's Moons directory. The C++ spherical material
importer produces 2048 × 1024 albedo/normal/property maps under
`assets/visual/moons/sol-<name>`. The 54 runtime maps have exact hashes in
`export/native-moon-assets.json`; the source audit is
`data/planets/moon-asset-audit-v1.json`. No generic cloud layer or invented
emissive texture is added. Titan retains its supplied orange haze; Iapetus's
legitimate dark terrain is protected with a reduced light-gradient correction.

All 18 conversions were reviewed at four longitudes and as GPU close-ups.
Europa's cracks, Io's sulfur colors, Triton's pale terrain and Iapetus's
dichotomy remain identifiable. The supplied images show one hemisphere: the
unseen hemisphere is an approximate wrap, not additional measured geography.

System and planet screens share canonical materials and orientation. Close
views request 2048-wide maps; lower LODs remain bounded by a 96 MiB CPU material
cache and existing GPU budgets. Resolved moons retain a 2.8-pixel minimum disc;
unresolved families remain hidden until separation is readable. Each family
uses its own orbit paths and depth-tested moon meshes. Selecting a planet
immediately starts tracking; zoom preserves it, while manual pan releases it.

## Physical ownership

Core `planetary_satellites` defines the 19 canonical moon identities, physical
radii/masses, mean elements and parent relationships. Values use
[JPL mean satellite elements](https://ssd.jpl.nasa.gov/sats/elem/) and
[JPL satellite physical parameters](https://ssd.jpl.nasa.gov/sats/phys_par/).
Phases are a reproducible campaign epoch, not an observing-date ephemeris.
Procedural moon orbits derive periods from their parent mass and orbital radius.

Engine `framed_orbit_position` supplies generic plane transforms and
`rotation_frame` supplies orthonormal quaternion frames. Core owns all celestial
semantics. Native consumers use the same analytic state for paths, positions,
illumination and locked faces. Uranian moon orbits follow the tilted equator;
Triton is retrograde. Pluto and Charon move about a mass-weighted barycenter.
The chart exaggerates sizes and distances for legibility; its depth is bounded.

Moon faces remain locked toward their actual parent, including spectral-only
legacy Sol snapshots. Ordinary planets rotate once in roughly 8–14 real minutes
around a fixed individual axis. Cosmetic spin does not accelerate with game
speed. Physical orbits retain simulation time: 1× advances one hour per real
second. Faster time advances those same analytic orbits, and camera tracking
keeps the selected body anchored. No second simulation clock was introduced.

Parent/moon alignments provide one analytic eclipse blocker to the existing
surface and atmosphere shader. Real source direction gives changing phases.
Alignment cases are tested; the renderer does not simulate a finite stellar
disc or penumbra, overlapping shadow casters, eclipse reddening or multiple
simultaneous ring/moon shadows. External eclipses currently take precedence over
the surface's ring shadow.

## Verification and remaining work

`native_moons` checks canonical identities, all 18 live imports, legacy migration,
DTO round-trip, orbital closure through 10 million days, correct paths, fixed
spin axes, tidal facing, retrograde motion, Uranian tilt, binary barycenter,
eclipse alignments and production workspace tracking across hourly/large time
steps. It emits 18 individual GPU close-ups, three overview sheets and six
parent-family workspace captures. Save, planetary-screen, system-entry and GPU
regression suites also run for this change.

Evidence: `build-native/preview/moon-test-captures/validation.json` and sibling
PNGs. Mean two-body motion does not include mutual perturbations, precession,
libration or resonant tidal evolution. Iapetus uses an approximate intermediate
orbital frame. This report does not claim every requested advanced moon feature
or the separate asset cooker/release acceptance checklist is complete.
