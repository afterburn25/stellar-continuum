# Star-map zoom crash repair — September 19, 2026

The game closed while zooming because the label layout rejected a valid distant
object footprint. The captured error was:

`Galaxy label obstacles must be finite and positive.`

The footprint was finite and positive, but a shared validation helper also
limited its position to one million pixels. The discovered central black hole
can project farther away than that when the camera zooms into a distant star.

**ENGINE CAPABILITIES ADDED / EXTENDED**

The native Galaxy label layout now clips positive finite object/HUD footprints
to the visible viewport, dropping invisible footprints before collision checks.
Intersections use double precision so even large finite float endpoints are safe.
The function signature and existing viewport/invalid-geometry checks remain.
Clipping is an in-place linear pass and reduces subsequent collision work.
There are no Core, save-format, timing, artwork, or camera-limit changes.

Validation:

- Reproduced the original process exit with an isolated copy of the user's
  developer save. The new regression also failed with the same error before
  applying the production fix.
- Galaxy-label, stellar-artwork and stellar-eruption suites pass (3/3).
- Packaged native replay loaded the copied 1,000-system campaign, navigated
  overview/regional/system views, and rendered maximum star-map and system zoom.
  All checked views reported zero label/obstacle overlaps. Replay exited normally
  and saved the isolated campaign successfully.
- Verified the original campaign hash was unchanged by these tests before
  reopening the developer game.

**ENGINE LIMITATIONS REMAINING**

This repair addresses the confirmed label-layout failure. Existing renderer
budgets and invalid-geometry checks still apply; other previously documented
engine limitations are unchanged. The clipping contract is reusable by future
map overlays without adding a separate simulation or per-view rule.

Modules: `app/native_client/native_galaxy_labels.{cpp,hpp}`.
Coverage: `native-tests/native_galaxy_labels_tests.cpp` and the existing native
galaxy-art replay. The capability registry and architecture document describe
the shared presentation boundary.
