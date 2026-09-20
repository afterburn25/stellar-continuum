# Native orbital presentation

The C++ system view consumes a detached, observer-filtered snapshot of one
selected system. Core remains the sole owner of research, exploration,
planetary facts, travel and campaign time. This is an orbital presentation
migration; it does not add simulation rules.

## Knowledge and lifetime

The controller checks the player's reconnaissance level before finding or
copying raw system/body records. Unknown and merely detected systems produce
no orbital snapshot. Reconnaissance includes the existing orbital catalog,
approximate radii and positive signatures. An empty signature collection is
not confirmation that a resource, anomaly or civilization is absent.

Full survey exposes confirmed environmental measurements, stellar classes and
broad visual classes. Only fully surveyed canonical Sol bodies receive an
approved image key. Pluto retains its eccentric orbit and a class illustration;
there is no invented Pluto texture. The image adapter checks full-survey
eligibility independently of the controller.

Snapshots own their strings and values. The simulation owner thread builds
them. Campaign replacement discards orbital selection, camera state and cached
appearance ownership. Older generations cannot replace the current controller
or asset cache. Rendering never retains pointers into campaign containers.

## Geometry and controls

The native projection preserves the current source-game family spacing,
relative body radii, eccentric focus ellipses and complete moon-family fit.
These are schematic presentation distances, not new astronomical or travel
measurements. Camera movement does not move a planet off its orbit or advance
simulation time. Moons become visible when their screen separation is useful.

Known systems open through map input. Within a system, left dragging pans and
wheel zoom remains anchored to the pointer without requiring a selected body.
Body selection uses the same transform as drawing. Reset fits the complete
system; Back and Escape return to the galaxy. The inspector consumes its own
input. Its grouped physical/environment/satellite facts scroll independently;
Focus Planet centers the selected body without changing zoom, while Open Colony
retains ownership gating. Exact measurements require full survey even when
reconnaissance geometry is available. See NATIVE_BODY_INSPECTION.md.
Global research, shipyard and construction controls retain explicit
routing and cannot silently issue a fleet command.

The workspace uses system-specific decorative stars, with no background galaxy
image. Approved planet images are projected into transparent orbital discs;
photographs retain their observed illumination and map sources receive stellar
lighting. The nine original JPEGs and their source credits are declared by
exact paths and hashes in `export/native-celestial-assets.json`. CMake and the
Windows exporter reject missing or altered assets. Headless presets do not
package these graphical resources.

## Resource ownership

Engine exposes immutable owned RGBA images and ordered world rendering; it
does not know about Core, planets or asset paths. Windows WIC decoding supports
native Unicode paths, checks dimensions and decoded byte count before pixel
allocation, and releases COM resources on success and failure. Each input is
bounded to 8,192 pixels per dimension and 64 MiB decoded RGBA storage.

The application produces cached 256-pixel Sol discs and 96-pixel class
illustrations. Source images are released after generation. Repeated camera
frames reuse the same image identity; panning and zooming do not decode or
upload textures again. The application cache is bounded to 96 entries and
16 MiB; the platform texture cache is independently bounded to 128 entries
and 192 MiB estimated CPU-plus-GPU residency. Caller-held image memory is
outside the latter estimate. Eviction releases each cache's ownership.

## Validation and remaining work

Maintained tests cover observer gates, generation/thread ownership, positive
signatures, family geometry, Pluto's eccentricity, fit at 720p through 4K,
camera transforms and body hits. Platform tests cover Unicode/corrupt/missing
images, limits, texture reuse, entry and byte eviction, input ordering and
large finite offscreen geometry. Export negative tests reject missing imagery,
unproven input, invalid camera values, wrong body selection, missing saves and
paused-world mutations.

The maintained system export proof opens fresh Sol at 720p and reloads the
same campaign at 1080p. Both runs select Earth through input, pan, zoom, render
images and save. The entire paused Player17 payload must match, excluding only
the save timestamp. Exact committed build evidence belongs in HANDOFF.md and
the migration PR; planned checks are not counted as passed until executed.

Connected system exits and owned local fleet motion are now integrated; see
NATIVE_SYSTEM_TRAVEL.md. Atmospheres, detailed stellar shaders, Saturn's
rendered rings, local combat, orbital structures, 3D planet/surface views
and audio remain migration work. The current cached orbital discs are not a
full-resolution close-up renderer. This preview does not claim graphical or
gameplay parity with the preserved game and is not a replacement release.
