# Native C++ galaxy preview

The engine and client target is C++23. The preserved Godot/C# game remains the behavioral reference and current full playable baseline. C# added under `tests/` generates reference fixtures; it is not shipped with the native runtime.

The opt-in `windows-native-preview` preset builds `stellar-continuum-native.exe`. SDL 3.4.16 supplies the window, input and explicit Vulkan GPU rendering. The Engine platform accepts projected lines, soft round points and debug text, with no Core dependency. The client owns a real 500-system fresh campaign through the integrated runtime and campaign frame adapter.

## Current interaction

- Fullscreen default, left-drag pan, pointer-anchored wheel zoom and star selection.
- Separate pause/resume and speed controls; Escape opens Continue / Save / Load / Exit to Windows.
- Ordered drawable-pixel input preserves the press owner through a whole gesture. UI gestures do not move the map; releasing a map drag over a button does not activate it.
- Focus loss cancels held input. Minimized windows stop advancing and rendering; the first restored frame discards inactive elapsed time.
- Player knowledge filters names, details and lanes. Unknown selections stay unknown. The hidden galactic core is not rendered.
- VSync follows display refresh. If the backend rejects it, the reason is logged and presentation is bounded to the detected refresh.

## Validation evidence

The reviewed candidate compiled under strict MSVC Release `/W4 /WX /permissive-` against the frozen campaign frame sources. Camera/gesture checks and actual SDL event-queue replay passed. The event replay injects focus, minimize and restore events; it does not establish manual Windows taskbar restoration.

A different-working-directory Vulkan smoke rendered 120 frames at 1280x720 and captured a frame before presentation. It reported VSync, 500 systems, 462.798 ms startup, 16.348 ms mean frame time and 17.439 ms p95. These are bounded diagnostic observations on the development machine, not a sustained 60 FPS guarantee. The captured image was inspected. Its simple point-map and debug-text UI are explicitly unfinished.

Engine 0.1.44 adds native Player17 save/load ownership. A full 500-system manual save and explicit reload passed from another working directory with a restricted Windows PATH. Paused recapture compared the entire payload, excluding only the new save timestamp. The fresh/load captures observed 17.594/18.083 ms mean and 16.841/18.699 ms p95; these measurements include manual save and are not a sustained frame-rate claim. Integration passed 111 native checks, 29 existing export checks and eight native dependency/session export checks. The final session corrections also passed focused Debug and Release replays twice and the maintained save/recovery/session tests.

Maintained integration and sealed package results are recorded in `HANDOFF.md`, draft PR #325 and coordination issue #324. A package is attributed only to its own source commit.

## Campaign persistence

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

## Remaining migration

Production HUD, research, diplomacy, system/planet/colony views, fleet orders, tactical presentation and audio remain. Current Player17 UTF-8 recovery is maintained; UTF-16 input remains explicitly excluded. The point-map/debug-text preview is not a replacement player release and does not claim visual parity with the user's reference images. `windows-release` remains blocked.
