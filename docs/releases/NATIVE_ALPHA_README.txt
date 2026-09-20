STELLAR CONTINUUM - NATIVE WINDOWS ALPHA

Extract the entire ZIP, then launch stellar-continuum-native.exe.
Windows 10/11 x64 and an installed Vulkan graphics driver are required.
Windows Media Foundation is required for audio. No development tools, Godot,
.NET or Python are required to play.

New Game opens campaign setup, including galaxy shape, population and size.
Start with 500 systems for a shorter load; sizes through 50,000 are available.
Left drag pans, the wheel zooms, and double-click opens an explored system.
The regional star background stays fixed behind the map. Zoom out to see the
galaxy overview. Use the Galaxy tab to return from a system.

The resource strip holds play/pause and speed controls. Use the top tabs for
planets, economy, research, diplomacy, logistics and the shipyard.
Select an owned fleet and right-click a destination to review a travel order.
Select an owned planet or its roster entry to open planetary management.
Construction reviews show actual costs; cancellation shows the current refund.
In tactical combat, Alt + wheel changes the selected formation's depth.

Escape opens the pause menu. Save before exiting. Native saves are stored under
LocalAppData/Stellar Continuum/NativePreview and are separate from the earlier
Godot game's files. Existing native Player17 saves remain supported; Godot
version-19 planetary saves cannot be imported. Restored campaigns start paused.
The save drive needs free space; 50,000-system campaigns can exceed 400 MB each,
with additional space needed for the temporary write and previous-save backup.

Settings include audio, voice/subtitles, controls and display options.
Borderless Fullscreen is the default. Video changes offer Keep/Revert.
F12 saves a screenshot. Its folder can be changed in General settings.
F8 exports a support report. Developer diagnostics require launch with --devtools.

This alpha includes the new planetary interface only. Large galaxies need more
memory and longer save/load times. Full collision response, all detailed ship
models and full earlier-game parity remain unfinished. This build is unsigned.

Documentation/ReleaseNotes.md describes this release. build-manifest.json lists
its source revision, local-change status and packaged file checksums.
stellar-continuum.exe is a separate headless diagnostic tool, not the game window.
