<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Stellar effects, visual rotation and navigation

This records the September 19 update. The September 20
[flare and arrow correction](FLARE_SHARPNESS_AND_TIMING.md) supersedes its flare
volume and arrow orientation: flares use crisp curved textured surfaces again,
flare speed/frequency stay at 1x, and names run across the broad arrow base.
Surface attachment and shared slow planetary rotation remain. The imported
planet pixels and cloud-free material policy are unchanged.

## Engine capabilities added / extended

`SurfaceEffect3D` now optionally describes a closed emission volume, with bounded
depth, density, seed and 8–64 integration steps. `surface_emission_volume` creates
an outward-facing proxy. The GPU adapter supplies its inverse view transform;
the fragment shader integrates object-space density and emission front to back,
with early opacity exit and existing photosphere occlusion. Depth zero retains
the existing surface-effect path. Ordinary materials use their existing shaders.

The superseded volume treatment used different thickness and density. Quality selected
16/24/36/48 samples. The six-effect global cap, per-star limits, four image jobs,
bounded caches and owner-thread GPU resource lifetime remain. No new render target
is allocated per effect. This is reusable for other image-guided emissive volumes.

`measure_emissive_disc` finds a robust bright-disc center and radius using radial
sampling, opposite limbs and median rejection. Close stellar artwork caches this
measurement. Both maps attach events to that measured surface, and preserve all
source pixels, including the faint outer glow. Effects wait for measured close
art instead of temporarily attaching to the image rectangle.

The authoritative activity generator now samples the complete sphere uniformly
in area. Developer random-location commands use the same rule. Linked CMEs retain
their parent location; explicit diagnostic coordinates remain available. Saved
events retain their coordinates and seed/counter replay remains deterministic.

Engine `Text::rotation_degrees` adds centered rotation to the existing cached text
renderer, retaining its clipping and font measurement. It does not create rotated
copies in the text cache. Travel labels and enlarged arrow geometry share the
same signed bearing in this initial version. The correction now places names
parallel to the broad base. Label overlap uses the rotated text bounds.

## Planets, moons and time

Free worlds turn once every 8–14 real minutes, with a stable rate derived from
their saved spin period and direction. The physical axis is applied to the mesh's
own north pole before spinning. Its pole and equatorial ring plane stay steady.
The shared display clock pauses with the game and is never multiplied by strategic
speed. Planet and system views share the same phase; static list portraits retain
their established reference poses. No cloud layers or altered artwork are added.

Core `PlanetAppearance::tidally_locked` is an optional saved boolean. Both ordinary
and ordered campaign JSON retain absent, false and true values. Explicit metadata
takes priority. Legacy regular moons are treated as locked, and legacy primaries
with an already synchronous saved period are recognized using their host mass and
orbital distance. New regular moons record the lock; new free primaries record
false. The shared `planet_is_tidally_locked` rule feeds both native views.

A locked object's orientation follows its displayed parent bearing, bypassing
cosmetic spin. This keeps the same hemisphere facing its star or planet regardless
of the display clock. An explicitly unlocked moon remains free to rotate.
This does not change Core orbital periods or add a tidal-evolution simulation.

The existing orbital motion was checked against the hourly native clock: 1×
advances one game hour per real second, and Kepler positions use those game days.
Increasing game speed accelerates orbits and synchronized locking, while free
visual spin and asteroid tumble remain slow.

## Camera and labels

Close body focus, or wheel zoom over a body at sufficient size, follows its live
position at the same screen anchor. Dragging, fitting the system, choosing another
target, or zooming back out releases tracking. Refresh and resize preserve the
followed target; pause preserves its position.

System, planet and moon labels are centered below their objects with a modest font
increase. Crowded labels can step down and are omitted when no clean placement
exists. Existing collision and offscreen protections remain. Travel arrows are
approximately 20% larger, and their larger labels now follow the broad base angle.

The solar-system indicator defines **1× as Fit System** for the current viewport.
The manual zoom-out limit is **0.05×** of that reference. This normalization keeps
wide systems and their travel arrows accessible, rather than imposing a raw
camera floor that crops the system. The existing close-up camera limit is retained;
its displayed magnification depends on the size of the fitted system.

## Validation

The download packaging check exposed a compatibility problem in an older real
developer autosave: its planet classes were stored as numeric enum ordinals.
Core now reads the frozen v1 ordinal mapping as well as canonical string IDs.
Existing planet identities and artwork are preserved; new saves write strings.
Malformed and unknown values remain errors. The portable download includes an
independent campaign copy and a launcher using paths relative to its own folder.

Automated coverage includes volume geometry and invalid parameters, side-on GPU
visibility, integration quality, photosphere blocking, clipped rotated text,
stellar-disc measurement with transparent padding, random sites and saved event
replay, fixed spin poles, retrograde rotation, speed-independent spin, lock-state
serialization and legacy resolution, camera following, hourly orbit displacement,
0.05× zoom, below-object labels and angled navigation geometry.

See the accompanying packaged validation files for the completed test results and
developer replay. Original user saves and the running developer build are kept
separate from the test package.

## Engine limitations remaining

- The volume treatment was withdrawn after it smeared fine image detail. Flares
  use curved 3D surfaces again; convincing full volume remains unfinished.
  Image-stage interpolation and generic one-frame families remain limitations.
- The renderer remains SDR. Traveling CME gameplay and propagation are separate
  future consumers.
- No new tidal evolution or full moon-orbit solver is introduced. Locked moons
  follow the existing schematic parent-relative orbit used by the system view.
- The user subsequently confirmed that desktop glow is gone. No Windows display
  settings were changed, and this was not independently confirmed by inspection.
