# 0.1.2 Alpha — system scale, free zoom and loading continuity

Base: integration `b3eb693bc67d9c9e0e5bc9eadd4dee02c56f7548`.

## Player changes

- Wheel zoom in the 2D orbital map stays anchored to the pointer with or without a selection. It no longer forces planet focus at a zoom threshold. Left-drag remains available. Double-click and the focus controls still open the optional 3D view.
- Shared display radii preserve large gas giants and a larger primary star. Complete planet/moon families and Saturn's rings receive orbital clearance. Close views no longer clamp gas giants to terrestrial sizes. The perspective camera can approach nearer to the limb; zooming back out restores the orbital view without a long sequence of wheel steps.
- Orbital-map planet and star discs shrink with their orbit paths at overview zoom. The previous 9–30 px planet and 44 px star floors caused crowding; only tiny visibility floors remain, with a separate click tolerance. Planet centers keep the same orbital transform throughout zooming, including Pluto's eccentric path. Close-up sizes are unchanged.
- The outer dotted delimiter accounts for the complete orbital extent and visible ring/body size. Labels and arrows move outward along their real lane bearings. Arrow bodies stay 40×34 screen pixels at every zoom. Offscreen gate polygons are culled before triangulation at extreme zoom.
- Pluto is dwarf planet ID 10, after Neptune, with an eccentric and inclined display orbit. Earth remains ID 3 and Moon ID 9. Explicit canonical Sol saves receive a narrow additive upgrade; procedural saves are unchanged. See `docs/SOL_STARTING_CATALOG.md` for orbital sources and migration limits.
- The distant galaxy artwork is composed at 26% regional / 46% overview opacity rather than 7.5% / 12%. It remains absent in system and surface views.
- The scientist profile uses female British English `bf_emma`, with `en-GB` / Hazel as the Windows voice preference. A real neural audition now exercises the actual unexplored-lane warning and voice-specific cache.
- Godot's separate boot image is disabled. The runtime loading surface owns startup artwork, progress, error handling and the existing seven-second minimum. New-galaxy and saved-game loading retain their own artwork and actual progress.
- Background music remains stopped throughout startup loading and begins only after the main menu becomes ready. Later campaign loads retain the existing music session.

## Validation receipts

- Core Runtime 84/84 and Simulation 71/71 on the isolated Pluto catalog change.
- Integrated Quality validation 20/20, including deep camera convergence, cursor anchoring, gas-giant hierarchy, moon visibility/picking and eccentric/inclined geometry.
- VoiceCoreChecks 12/12 with the real installed Kokoro pack, including the British scientist warning and cache round trip. This is synthesis/routing evidence, not a claim of a human-recorded voice.
- GodotSmokeChecks 39/39. Game build: zero warnings/errors.
- Maintained native entry: `STELLAR_CAPTURE_FOCUS=system-scale` through `tools/ScreenshotCapture.tscn`, with real pointer input at 1280×720 and 1920×1080. Visible receipt `work/system-scale-native-05`: exit 0, empty stderr, six images covering overview and unselected deep Earth zoom at both resolutions. Assertions cover cursor anchoring, left-drag pan, stable arrow size, outward boundary clearance and Pluto/gas-giant geometry. Focused-view/loading receipts and hosted gates are recorded in the PR before integration.
- `work/system-scale-focused-01`: visible native focused star/planet and travel-arrow validation, exit 0, empty stderr, 11 images. Sol, Earth and gate-hover images reviewed.
- `work/system-scale-loading-01`: visible native startup/new-galaxy/saved-game validation with real audio enabled, exit 0, empty stderr, three images. Assertions verify a single continuous startup surface, monotonic progress, the minimum loading interval, music stopped throughout startup and playing once the main menu is ready. Startup and saved-load images reviewed.
- Final orbital-sizing receipt `work/system-scale-native-06`: exit 0, empty stderr, eight images at native 720p/1080p. Actual planet sprite centers remain attached to orbital positions through wheel zoom; the native Sol check verifies pairwise clearance including Saturn's rings, plus overview, zoomed-out and deep free Earth views. Quality validation remains 20/20 with a close-conjunction regression that fails under the previous minimum-size floors. Existing unrelated CA2014 warning remains in Quality `Program.cs`; the Game build has zero warnings/errors.
- Additional native camera-flow and combat save/menu checks passed at the preceding head in `work/system-scale-camera-01` and `work/system-scale-combat-01`, both with empty stderr. The final hosted screenshot gate covers the complete campaign journey on the release head.

## Remaining simulation gap

System drawing units remain schematic. Existing `FleetLocalTransit` is chart movement, not an AU-based orbital transfer. Sol currently has orbital indices but no authoritative semi-major axes or body-to-body transfer orders. Enlarging the picture therefore does **not** implement a six-month Earth–Mars journey. A separate simulation milestone must define physical orbital distances, local propulsion, departure/arrival phases, transfer windows, progress/ETA, interruption/recovery, and save persistence. Do not fake that duration by multiplying display coordinates or changing light-year fuel rules.

Source evidence is maintained in Git; native images/logs are generated under `work/` and excluded from source commits. No Windows validation helper executable was introduced.
