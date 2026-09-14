# Native C++ galaxy preview

The engine and client target is C++23. The preserved Godot/C# game remains the behavioral reference and current full playable baseline. C# added under `tests/` generates reference fixtures; it is not shipped with the native runtime.

The opt-in `windows-native-preview` preset builds `stellar-continuum-native.exe`. SDL 3.4.16 supplies the window, input and explicit Vulkan GPU rendering. The Engine platform accepts projected lines, soft round points, cached antialiased text and ordered panel/text overlays, with no Core dependency. The client owns a real 500-system fresh campaign through the integrated runtime and campaign frame adapter.

## Current interaction

- Shipyard opens canonical known designs, costs, readiness and timed build queues. Cancel pauses, refreshes the refund, and requires explicit confirmation.

- Research opens known programs, domain tabs, search and canonical Begin/Pause/Resume actions.
- Select a green owned fleet or its outliner entry; right-click a destination and confirm the canonical route preview. Unknown names remain masked in feedback.

- Fullscreen default, left-drag pan, pointer-anchored wheel zoom and star selection.
- Separate pause/resume and speed controls; Escape opens Continue / Save / Load / Exit to Windows.
- Ordered drawable-pixel input preserves the press owner through a whole gesture. UI gestures do not move the map; releasing a map drag over a button does not activate it.
- Focus loss cancels held input. Minimized windows stop advancing and rendering; the first restored frame discards inactive elapsed time.
- Player knowledge filters names, details and lanes. Unknown selections stay unknown. The hidden galactic core is not rendered.
- VSync follows display refresh. If the backend rejects it, the reason is logged and presentation is bounded to the detected refresh.

## Validation evidence

Engine0.1.47 passed121/121 CTest and57 Python checks, plus strict shipbuilding assessment parity (89actual-source rows in Debug and Release). Additional actual720p shipyard runs prove fresh locked state and running Start/pause/refund confirmation/save in an explicitly authored unlocked scenario. This is not natural unlock progression proof. See NATIVE_SHIPYARD.md and HANDOFF.md.

Engine 0.1.46 passed 118/118 CTest and 29+19+9 Python checks. Six maintained real Vulkan client runs passed map/research/fleet save/load and complete paused payload recapture checks; see HANDOFF.md for measured timings and scope. Fleet smoke inputs are actual-source Player17 test data, not injected fresh-game ships.

The reviewed candidate compiled under strict MSVC Release `/W4 /WX /permissive-` against the frozen campaign frame sources. Camera/gesture checks and actual SDL event-queue replay passed. The event replay injects focus, minimize and restore events; it does not establish manual Windows taskbar restoration.

A different-working-directory Vulkan smoke rendered 120 frames at 1280x720 and captured a frame before presentation. It reported VSync, 500 systems, 462.798 ms startup, 16.348 ms mean frame time and 17.439 ms p95. These are bounded diagnostic observations on the development machine, not a sustained 60 FPS guarantee. The captured image was inspected. Its simple point-map and debug-text UI are explicitly unfinished.

Engine 0.1.44 adds native Player17 save/load ownership. A full 500-system manual save and explicit reload passed from another working directory with a restricted Windows PATH. Paused recapture compared the entire payload, excluding only the new save timestamp. The fresh/load captures observed 17.594/18.083 ms mean and 16.841/18.699 ms p95; these measurements include manual save and are not a sustained frame-rate claim. Integration passed 111 native checks, 29 existing export checks and eight native dependency/session export checks. The final session corrections also passed focused Debug and Release replays twice and the maintained save/recovery/session tests.

Maintained integration and sealed package results are recorded in `HANDOFF.md`, draft PR #325 and coordination issue #324. A package is attributed only to its own source commit.

## Campaign persistence

Exact Engine 0.1.44 package `StellarContinuum-windows-native-preview-51c0af46-20260914T141133791491Z` was sealed from clean commit `51c0af4662196d1d7e36d5f7def427d1bf8e173b`: 80 verified files, identical ZIP/folder bytes, valid ZIP CRC, 8,712,122-byte archive and all 14 relocation/session/recovery checks. Its committed export passed 111 CTest plus 29 existing and eight native export checks. The restricted-PATH fresh and loaded graphical runs rendered 120 frames each; startup was 580.798/528.210 ms and mean frame time 17.546/17.397 ms. Evidence is in `work/native-044-sealed-export.log` and `work/native-044-verification.json`.

The default native save is `%LOCALAPPDATA%/Stellar Continuum/NativePreview/campaign.player17.json`. `--save-path <path>` selects another slot; `--load` explicitly loads it, starting paused. Wide Windows command-line paths preserve Unicode. Failed explicit loading never creates a fresh replacement or modifies the save files.

Background saves own detached Player17 payloads. Load drains the current write first and suspends new save admission until the read finishes. The restored campaign, clock and caches are validated before replacing the live owner. A failed read, decode, activation or save drain preserves the current session. Exit closes only after a successful save; failure remains visible. Public session access is restricted to its simulation owner thread, and a failed frame invalidates save eligibility.

The exporter passes an isolated temporary save path to both fresh and saved-game smoke runs. Each renders 120 frames and exercises the manual save. It verifies the full paused reload/recapture, so tests do not write the user's default slot. Direct `--smoke` also requires an explicit save path.

## Build and export

```powershell
python tools/stellar-export/stellar.py build windows-native-preview
python tools/stellar-export/stellar.py export windows-native-preview
```

The export seals the client, exact reviewed SDL3 DLL, license and declared data. Direct and transitive imports are inspected. The headless dependency policy stays unchanged. The preview exporter additionally launches from another directory with a restricted Windows PATH, verifies Vulkan and the campaign count, and captures the displayed frame. That local GPU check is opt-in; ordinary CI remains headless.

The preview build folder is `build-native/preview` to stay within the Windows compiler's generated-path limits at this checkout depth. An offline SDL archive may be supplied through `STELLAR_SDL3_ARCHIVE`; its pinned hash is still mandatory.

## Native UI assets and layout

The Windows platform rasterizes private Rajdhani SemiBold headings and the host's Segoe UI interface font to alpha textures. The bundled Rajdhani font and its OFL license are declared by exact paths and SHA-256 hashes in `export/native-ui-assets.json`. Native CMake builds copy the reviewed font; the exporter verifies and seals both font and license. It never redistributes a machine-installed font. Missing/tampered inputs and unreviewed source/output paths fail packaging; missing runtime headings fail clearly rather than silently substituting a font.

The renderer caches text by content, pixel size, wrapping and font role; color/opacity are texture modulation. Its LRU is bounded to 640 entries and 32 MiB. GDI drawing is flushed before alpha extraction; native resources are released on exceptions. Text supports clipping, wrapping and alignment. World primitives draw before the ordered overlay, so menu backgrounds actually occlude the map.

One drawable-pixel layout supplies both painted rectangles and hit targets. Main controls, status, fonts and menu scale together at 720p, 1080p, 1440p and 4K, with narrow-window caps. Layout replay checks complete button containment, separation, and center/four-corner hits, including 1280x1080 and 640x360. Actual 120-frame fresh/load captures at the four standard sizes were inspected under `work/102-native-ui/build/responsive-1789395568139396800`; mean frame times ranged from 17.541 to 18.221 ms. These are diagnostic samples, not a performance guarantee. SDL event replay also verifies hover coordinates, font failures, stable text reuse and cache limits.

## Remaining migration

Production HUD styling, construction/unlock progression, diplomacy, system/planet/colony views, tactical presentation and audio remain. Research, fleet and shipyard interaction are native migration workspaces; complete source-game behavior and visuals remain unfinished. Current Player17 UTF-8 recovery is maintained; UTF-16 input remains explicitly excluded. The point-map/debug-text preview is not a replacement player release and does not claim visual parity with the user's reference images. `windows-release` remains blocked.
