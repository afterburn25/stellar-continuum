# 0.1.4 Alpha — spectral point stars on the regional map

Base: integration `65ede790` (0.1.2, PR #316). This replaces the regional-disc art in
the downloadable 0.1.3 preview before integration through PR #317.

## Visual contract

The main star map shows compact white-hot points, colored coronas and fine tapered
diffraction rays, following the user's Barnard's Star reference. Stars keep their
physical spectral colors. Enlarging the regional halo does not turn it into a visible
solar surface. Names sit clearly outside the light and selection treatment.

Solar-system sun materials, shaders, flares and resolved stellar surfaces are unchanged.
The existing wheel transition can still approach a reconnoitred star's detailed system
view; direct Open System and double-click retain the ordinary 2D orbit map.

The regional renderer uses cached gradient artwork and immediate drawing commands;
it creates no per-star photosphere nodes or materials. Hidden-system filtering remains
in force. The previous regional sprite pool and its lifecycle machinery are removed.

The 48x free cursor-anchored zoom and local sky from the prior preview remain. Distant
galaxies reach zero opacity regionally; seeded stars, three clusters and a nebula fill
the frame. Camera movement does not expose undiscovered orbital data.

## Validation

The maintained `regional-map` capture mode exercises 720p and 1080p using actual mouse
input. Its updated image checks require a bright compact core, a warmer/dimmer halo
around Sol, and a fade into the background. It also checks core-size limits, real
point rendering, absence of regional photosphere nodes, free zoom, observer privacy,
star approach and unchanged ordinary orbital entry.

Game build and source-bound visual receipts, hosted gates and final Windows package
provenance are recorded in PR #317 before validated integration. The 0.1.3 results
remain historical and do not stand in for validation of this correction.

Local `work/regional-point-native-02` passed with exit 0 and empty stderr: 12 native
captures at 1280×720 and 1920×1080. Pixel checks confirm the compact white-hot Sol core,
warmer dimmer corona and background falloff. Real pointer checks cover free maximum
zoom, unknown-system privacy, unselected known-star approach, further system zoom and
ordinary orbital entry. The regional image was visually reviewed against the reference.
Game builds with zero warnings/errors. A source diff confirms that system star shader,
material and scene files are identical to the preceding preview.
The full native camera journey passed again in `work/regional-point-camera-01`,
exit 0 with empty stderr: pointer picking, pan, viewport resize, UI shielding,
system/planet focus, deep approach, privacy and restoring the previous camera.

No economy, survey, save-format or physical travel rules change. Main remains untouched.
