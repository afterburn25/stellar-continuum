# Native video settings

The native main menu and pause menu expose VIDEO from the existing Settings
panel. Display mode, exclusive resolution/refresh, V-Sync and frame cap remain
application preferences. `NativeVideoController`, Window and audio settings
outlive campaign replacement; no Core rule, Player17 field or C# source changes.
Switching from audio to video cancels any unsaved audio preview.

## Window contract

`Window::display_modes()` returns at most 256 unique supported width, height and
refresh tuples for the current display. Borderless Fullscreen is the default and
recommended mode. It uses the desktop resolution. Exclusive Fullscreen uses
SDL's enumerated modes; Desktop default preserves the desktop refresh rate
rather than silently choosing a higher rate. Windowed is a decorated resizable
window: its selected resolution is its logical client-area size, fitted with its
border extents inside the current display's usable bounds. The current display
is queried for each change and negative multi-monitor origins are preserved.
Explicit sizes that do not fit are rejected and restored by the preview
controller; only the default window size is fitted automatically. Windowed
choices remove duplicate refresh tuples. The Borderless resolution label uses
the monitor's desktop mode even when the game is currently windowed.
A mode which disappears after enumeration is rejected. Fullscreen requests are
synchronized, then checked against actual window/display state. The renderer
output size is refreshed for shared drawing and input coordinates.

F12 queues one PNG capture of the rendered drawable. PrintScreen is also used
when SDL routes it to the game; this does not claim to repair operating-system
PrintScreen handling. Captures work in startup, settings and campaign screens,
write under `Pictures/Stellar Continuum/Screenshots`, and use timestamped unique
names. Only a queued request performs a GPU readback; write failures are
nonfatal. Tests may set `STELLAR_SCREENSHOT_DIR` or call
`Window::request_screenshot()` to keep output isolated. Existing explicit BMP
capture arguments remain BMP for graphical smoke compatibility.

V-Sync Off/On/Adaptive is verified against the renderer's actual result. Driver
rejection is reported. Automatic frame cap follows the detected current refresh,
including a window's display-change event;
60/120/144 and Unlimited are explicit alternatives. Unlimited disables the
software cap; V-Sync, if enabled, can still limit presentation. These controls
do not change NVIDIA driver settings or guarantee a particular game frame rate.

SDL requires exclusive modes from its enumerator, and warns that a successful
request can still be denied by the window manager. See
[fullscreen modes](https://wiki.libsdl.org/SDL3/SDL_SetWindowFullscreenMode),
[fullscreen synchronization](https://wiki.libsdl.org/SDL3/SDL_SetWindowFullscreen)
and [renderer V-Sync](https://wiki.libsdl.org/SDL3/SDL_SetRenderVSync).

## Transaction and recovery

Apply snapshots the previous preferences before touching the backend. A
successful preview opens Keep/Revert with a 15-second monotonic deadline.
Escape, pointer cancellation, focus loss, minimization, expiry, closing the
controller or a failed partial apply restores the previous settings. A late
Keep handled at expiry cannot commit. Only Keep writes `video-settings.json`
beside the application preference/save directory, through Engine atomic writes.
A write failure also rolls back; it is never presented as a successful save.
Reads are bounded to 64 KiB and sanitize malformed preferences.

If rollback itself fails, the controller tries borderless, V-Sync Off and
automatic pacing once. This recovery never overwrites saved preferences. If
that fails too, backend state is explicitly unknown, further apply attempts
are latched off, and the panel requests a restart. Terminal diagnostics retain
the backend exception; player-facing error text remains short and clipped.

Devin `9b5ba16e` supplied the initial settings view. This adaptation corrects
ignored persistence failures, partially accepted backend changes, campaign-owned
settings lifetime, missing resolution choices, fullscreen mode selection and
an Automatic option that behaved like Unlimited. It retains recorded scientist
audio and does not import the overlapping SAPI pipeline.

## Maintained verification

Build with the `windows-native-preview` preset. Relevant CTests are
`native_video_settings`, `native_video_controller`, `native_video_platform`,
`native_audio_settings`, `native_audio_director`, `native_client_platform`,
`native_campaign_session`, `native_startup_*` and `native_new_*`.

Controller cases inject a backend, clock and persistence callback to prove
partial rejection, Keep failure, expiry, inactive-window recovery, destructor
recovery and failure latching. View cases cover 640x360 through 4K, clipping,
detected resolution choices, reverse selection, disabled borderless resolution,
malformed/truncated/oversized files, repeated replacement and blocked writes.
The actual Vulkan platform test prints the SDL display count, enters
desktop-default and explicit detected exclusive modes, returns through
borderless and windowed modes, produces an isolated PNG capture through the
bounded capture API, rejects invalid settings, and measures software pacing.
It does not claim sustained game FPS.

For relocated graphical verification, call
`native_new_game_runtime.validate_native_new_game_export(package, env, fixture,
audio_check=True, audio_settings_check=True, video_settings_check=True)` from
`tools/stellar-export`. The maintained `--video-settings-check` route drives
Settings -> Video, Apply, Escape rollback, Keep and restoration through native
events. It captures the normal and confirmation screens at 720p startup and
1080p paused reload. The runner checks an independent generated slot, the
unchanged original save, complete paused Player17 equality except SavedAtUtc,
audio lifecycle, persisted preferences and actual BMP dimensions.

Graphical smokes are explicitly windowed and avoid changing the user's desktop
mode; the dedicated platform CTest covers real fullscreen mode changes. Native
event replay is not physical mouse injection. Current evidence is in
`work/native-video-{build,ctest,python}.log`, `work/native-video-runtime.json`
and `work/native-audio-validation/package-new-game-*-video-*.bmp`.
The local package is unsealed validation output, not a release download.

### Windowed and screenshot acceptance, 2026-09-16

All 186 serial native CTests passed. Final focused platform verification and
relocated startup/pause checks passed again after correcting the desktop label
and per-monitor DPI projection. See `work/window-capture-ctest.log`,
`work/window-capture-final-platform.log`, `work/window-capture-runtime.log`, and
`work/window-capture-runtime/startup-video-result.json`. The 720p and 1080p
settings captures were visually inspected, including the correct desktop label.

The platform test verifies movement between two actual displays, decorated and
resizable Windowed mode, desktop-default and explicit Exclusive tuples, Borderless
restoration, and input delivered through SDL's coordinate conversion. The current
display reports 2560x1440 at 59.95 Hz and scale 1.0. Different physical DPI scales
were not exercised; the implementation retains SDL mapping and uses per-monitor
DPI metrics for projected window decorations.

PNG tests decode rendered color quadrants and dimensions, use a Unicode filename,
reject replacing an existing file, recover after a write error, and verify F12,
PrintScreen, ignored key repeats, and unrelated keys. A separate bounded actual
game process received F12 via its Windows message queue and saved the visible Sol
system as a 1280x720 PNG with no capture notice baked in. Its result, log and image
are under `work/window-capture-runtime/live-f12`. These checks do not claim to
control the operating system's PrintScreen shortcut or certify all graphics drivers.
