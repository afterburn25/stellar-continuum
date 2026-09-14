# Native C++ galaxy preview

The engine and client target is C++23. The preserved Godot/C# game remains the behavioral reference and current full playable baseline. C# added under `tests/` generates reference fixtures; it is not shipped with the native runtime.

The opt-in `windows-native-preview` preset builds `stellar-continuum-native.exe`. SDL 3.4.16 supplies the window, input and explicit Vulkan GPU rendering. The Engine platform accepts projected lines, soft round points and debug text, with no Core dependency. The client owns a real 500-system fresh campaign through the integrated runtime and campaign frame adapter.

## Current interaction

- Fullscreen default, left-drag pan, pointer-anchored wheel zoom and star selection.
- Separate pause/resume and speed controls; Escape opens Continue / Exit to Windows.
- Ordered drawable-pixel input preserves the press owner through a whole gesture. UI gestures do not move the map; releasing a map drag over a button does not activate it.
- Focus loss cancels held input. Minimized windows stop advancing and rendering; the first restored frame discards inactive elapsed time.
- Player knowledge filters names, details and lanes. Unknown selections stay unknown. The hidden galactic core is not rendered.
- VSync follows display refresh. If the backend rejects it, the reason is logged and presentation is bounded to the detected refresh.

## Validation evidence

The reviewed candidate compiled under strict MSVC Release `/W4 /WX /permissive-` against the frozen campaign frame sources. Camera/gesture checks and actual SDL event-queue replay passed. The event replay injects focus, minimize and restore events; it does not establish manual Windows taskbar restoration.

A different-working-directory Vulkan smoke rendered 120 frames at 1280x720 and captured a frame before presentation. It reported VSync, 500 systems, 462.798 ms startup, 16.348 ms mean frame time and 17.439 ms p95. These are bounded diagnostic observations on the development machine, not a sustained 60 FPS guarantee. The captured image was inspected. Its simple point-map and debug-text UI are explicitly unfinished.

Maintained integration and sealed package results are recorded in `HANDOFF.md` and `MIGRATION_STATUS.md` after those checks finish.

## Build and export

```powershell
python tools/stellar-export/stellar.py build windows-native-preview
python tools/stellar-export/stellar.py export windows-native-preview
```

The export seals the client, exact reviewed SDL3 DLL, license and declared data. Direct and transitive imports are inspected. The headless dependency policy stays unchanged. The preview exporter additionally launches from another directory with a restricted Windows PATH, verifies Vulkan and the campaign count, and captures the displayed frame. That local GPU check is opt-in; ordinary CI remains headless.

The preview build folder is `build-native/preview` to stay within the Windows compiler's generated-path limits at this checkout depth. An offline SDL archive may be supplied through `STELLAR_SDL3_ARCHIVE`; its pinned hash is still mandatory.

## Remaining migration

Native save/load session orchestration, production HUD, research, diplomacy, system/planet/colony views, fleet orders, tactical presentation and audio remain. The recovery candidate is unaccepted and must not be promoted without repairing its file-reading and callback-recovery behavior. The preview is not a replacement player release and does not claim visual parity with the user's reference images. `windows-release` remains blocked.
