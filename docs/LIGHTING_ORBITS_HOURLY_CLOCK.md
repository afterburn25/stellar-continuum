# Planet lighting, stellar orbits and hourly native time

September 19, 2026. This extends the orbital/artwork correction; it does not replace
the supplied planet library or re-enable automatic planet rotation or cloud layers.

## ENGINE CAPABILITIES ADDED / EXTENDED

### Readable lighting on the original globes

Engine `Material3D::linear_light` opts a material into sRGB decoding before light
evaluation and encoding afterward. Previously, multiplying encoded surface colours
made illuminated terrain too dark. The native canonical planet material now uses
this path, a larger visible-face light component and a bounded exposure response
to stellar flux. Ice, snow, oceans and vegetation retain their source colours;
authored lava emission remains a separate luminous surface layer. Planetary and
system inspection derive light from the planet's actual stellar host. CPU portraits
use the corresponding linear-light thumbnail option.

The change is shader arithmetic within the existing pass and immutable material
interface. Other materials retain their previous response unless they opt in.
Surface asset files and saved source IDs are unchanged. Fixed authored poses,
manual globe inspection, absent added clouds and existing texture budgets remain.

### Saved multiple-star architecture

Core owns `StellarOrbitArchitecture` and its optional, versioned `StellarOrbits`
save extension. It contains companion physical properties, relative analytic
orbits, planet-to-host bindings and a belt host. `stellar_positions`,
`stellar_planet_position`, `planetary_stellar_orbit`, `stellar_host_physics` and
`stellar_host_accepts_orbit` provide shared positions, periods and eligibility.
The binary pair moves about its common centre of mass; a triple adds an outer
orbit between the inner pair and the third component. Kepler periods depend on
separation and enclosed mass. Lighter components travel larger barycentric paths.

Fresh systems can place planets around A, B, C or the inner A+B pair. Existing
campaigns retain their planetary hosts, established distances and artwork while
receiving deterministic companion orbits. Generation, restore and developer
insertion share stability and clearance rules. Frozen-world repair and belt
placement use the host's mass and luminosity. A final clearance pass expands
companion orbits where repaired worlds or belts require more room; repeating it
does not drift the result. The save reader rejects malformed bindings, fractional
IDs, invalid periods, missing components and bindings to another system.

Application consumes these Core orbits for all surveyed stellar components,
planet paths and host-relative belts. The chart compresses separation for display
while retaining each pair's mass ratio. Fit System now supports the smaller zoom
needed by wide triples, and the zoom readout retains precision below 0.01x.
Orbit evaluation is analytic for the visible system; there is no galaxy-wide
per-frame N-body integration. Load-time grouping avoids repeated global body scans.

Stability bounds use conservative margins around the S/P-type fits in
[Holman and Wiegert (1999)](https://arxiv.org/abs/astro-ph/9809315), with a
conservative Hill bound outside the calibrated mass-ratio range.

### Solid belt bodies and separate clocks

The rocky belt renderer no longer overlays photographic strips containing static,
painted large rocks at intermediate zoom. Resolved objects use the canonical
three-dimensional meshes, varied sizes and elongated shapes. The existing
768-solid budget and tiny unresolved particles remain. Both rock and ice cosmetic
tumble use capped presentation time, independent of strategic speed, and freeze
when paused. Orbital revolution uses simulation days and the host mass. The belt
inspector displays period and orbital position, making slow outer-system motion
inspectable even at the hourly cadence.

### One hour per second and a 24-hour clock

`StrategicClock::set_days_per_second` supplies a reusable base cadence. Native
sessions select 1/24 day per real second at 1x. Other Core consumers retain their
legacy default unless explicitly configured. Effective speed reporting accounts
for that base cadence. Developer fixed stepping retains its 250 ms wall tick,
advancing 15 game minutes per tick at 1x, and higher speeds scale that progression.
Strategic accounting remains in game days; tactical timing is unchanged.

Engine calendar formatting and the Core wrapper provide HH:MM. The HUD shows it
beside the campaign date and states the 1x rate. Date and time use the same small
rounding tolerance so repeated fractional ticks agree at midnight. The clock's
saved campaign time and existing fixed-step backlog remain authoritative.

## Validation

- Native executable built successfully; 16 targeted regression suites passed:
  stellar orbits, fixed developer simulation, system workspace/view, planet
  appearance/materials, small-body fields/renderer, campaign frame/strategic clock,
  calendar, campaign/galaxy persistence and JSON, spherical materials and GPU.
- GPU readback checks distinguish correct linear illumination from legacy encoded
  multiplication. Planet checks cover static poses and absent cloud overlays.
- Orbital tests cover centre of mass, Kepler periods, S/P stability, A/B/C hosts,
  deterministic migration, repeated clearance, malformed saves and triple fit.
- The native developer replay checks eight imported classes in both views,
  binary/triple visibility, rocky/icy motion and pause, cached portraits, rings,
  saved appearance IDs and save reload. Captures were inspected for habitable,
  frozen and molten worlds, rocky bodies and the triple chart.
- A read-only audit of the user's saved 1,000-system galaxy retained 7,173 bodies
  and all 6,867 imported surface assignments. It found 1,473 clear fields and 117
  unsettled habitable worlds, and validated 187 binaries and 44 triples. Repeated
  repair was stable and the source save's SHA-256 was unchanged.
- The final command-line launch correction selects the developer reader for
  `--dev-game --load`. An additional native load/save/capture replay uses a separate
  copy of the actual campaign; the packaged proof records both executable hashes.

The package includes validation logs and representative native captures.

## ENGINE LIMITATIONS REMAINING

- Stellar motion uses restricted hierarchical Kepler orbits, not full N-body
  perturbations, tides, stellar evolution or galactic tidal disruption. Existing
  moon-local motion is not upgraded by this stellar-orbit extension.
- Wide companions are placed conservatively to limit their extra heating. Close
  binary climate/lighting uses an aggregate host approximation, not time-varying
  illumination and eclipses from every individual star.
- Fleet travel and avoidance still use the previous primary-centred navigation
  abstraction; moving companion hazards are not yet integrated into route planning.
- Display spacing and exposure are deliberately adjusted for readability; they
  are not a physically scaled astronomical view or photographic exposure model.
  Physically distant ice belts take centuries to revolve, so hourly motion is subtle.
- Cosmetic tumble phase is not persisted. Unresolved debris uses particles. Ice
  reflection/refraction samples the environment, not ray-traced nearby objects.
- Supplied images retain painted features. Hidden hemispheres use the existing
  reconstruction, and classes without accepted imagery retain procedural fallbacks.

Future consumers can reuse Core host eligibility and saved orbit records, Engine
linear-light materials, or configurable clock cadence without duplicating rules
inside a screen.
