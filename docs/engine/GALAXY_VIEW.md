# Galaxy view — editor 0.1.1

The desktop editor opens in a detailed galaxy view. Its seed-dependent stellar
light, warm central bulge, spiral clouds, dark dust lanes and small star-forming
knots give the world a galaxy-scale appearance. The actual native catalog remains
selectable and inspectable. Map mode retains clear class-based editing markers.

## Controls

- Scroll to zoom around the cursor; left-drag to pan.
- Right-drag to rotate and tilt, or use Viewing angle (0–65 degrees).
- Click a catalog star to inspect it. F or Focus centers the selected system.
- Fit frames the galaxy. Names reveals system labels with overlap suppression.
- Expand fills the workspace; Restore returns the outliner, inspector and assets.
- Galaxy / Map changes the presentation without changing the world.

## Rendering and data

`GalaxyArtwork` builds a deterministic 2048×2048 premultiplied light texture on
background workers, with cancellation on world replacement. It combines a disk,
bulge and bar, four winding arms of unequal brightness, multi-scale cloud noise,
patchy extinction and 115,000 unresolved decorative stars. It uses no downloaded
photograph, external service or additional asset license.

The artwork follows the compact native generator's center, radius, winding,
disk flattening and bar. A cached WPF image is transformed during camera movement;
annotations and camera changes do not regenerate the expensive light field.
Generated system positions use their actual X, Y and depth for projection and
hit testing. Artwork and decorative stars never enter the catalog or receive IDs.

This is an artistic galaxy visualization with projected depth. The light field
itself is a tilted plane, not a volumetric galaxy or a simulation of billions of
individual stars. Native gameplay coordinates are compact; their light-year scale
does not describe a physically sized Milky Way. The central glow is unresolved
stellar light, not a giant selectable star. Map marker radii communicate classes
and selection, not physical stellar radii. Project format and native rules remain
unchanged; camera preferences are currently local to the editor session.

Optical appearance references: NASA's [Hubble view of M81](https://science.nasa.gov/asset/hubble/detailed-images-of-spiral-galaxy-m81/)
for its central bulge, dust and arm structure, and [M101 across the Great Observatories](https://science.nasa.gov/asset/hubble/spiral-galaxy-m101-nasas-great-observatories/)
for clumpy stellar populations. No reference pixels are incorporated.

## Verification and next stages

The packaged Windows editor passes 48 integration checks against its pinned
native 0.1.9 runtime, including a 2500-system world, transformed selection,
zoom anchoring, panning, focus, view switching, cache reuse and world immutability.
Rendered screenshots cover 1480×940 and 1180×800 windows and expanded galaxy views.
The package is tested on the development machine, not a separate clean machine.

The native migration continues independently. Future Vulkan work can add
volumetric light and dust, distance-dependent detail and transitions into system
scenes. This editor feature does not complete those renderer or campaign gates.
