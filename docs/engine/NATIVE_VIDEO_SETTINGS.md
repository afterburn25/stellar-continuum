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

## General settings and screenshot destination

Settings opens the Audio panel with General and Video navigation. General is
available from both the main menu and the paused campaign, and provides Browse,
Use Default, Cancel and Save. The read-only path wraps and can be scrolled when
long; controls remain separate at 720p, 1080p, 1440p and 4K. F12 captures the game
to the saved folder, which defaults to the Windows Pictures known folder plus
`Stellar Continuum/Screenshots`.

Browse uses SDL's asynchronous, window-owned Windows folder picker. The game
continues servicing rendering/audio while it is open. One request is allowed at
a time. SDL's callback copies its UTF-8 result into independent synchronized state;
the application consumes it on the window thread. Cancellation is distinct from
an error, focus loss does not dismiss General, and results from a closed/reopened
settings view cannot modify its new draft. No callback holds a UI or Window pointer.

Folder selection and Use Default change only the draft. Only Save atomically
writes schema-1 `general-settings.json` alongside audio/video preferences and
updates the capture destination. Cancel/Escape/category navigation retain the
previous destination. Bounded 4 KiB reads reject invalid schemas, duplicate keys,
relative paths, embedded NUL, and unavailable/non-directory locations with a
visible settings diagnostic and default fallback. Failed writes leave the modal
open and retain the previous saved preference. Settings do not enter Player17.
The existing `STELLAR_SCREENSHOT_DIR` environment override retains highest
priority for isolated tests. PNG failure reporting and unique names are unchanged.

## Maintained verification

Build with the `windows-native-preview` preset. Relevant CTests are
`native_video_settings`, `native_video_controller`, `native_video_platform`,
`native_general_settings`, `native_audio_settings`, `native_audio_director`, `native_client_platform`,
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

### General folder acceptance, 2026-09-16

All 187 serial CTests passed; the final focused general/audio/video/controller
checks also pass after measured path wrapping was added. General tests cover
UTF-8 persistence and line boundaries, cached layout, scrolling, modal input,
navigation, draft cancellation/defaults, late picker results, repeat requests,
load validation and failed atomic writes. The long-path fixture stays within
Windows' legacy path limit while overflowing the visible path field.

The actual Vulkan game received bounded Windows input messages to open Settings,
General and SDL's native Windows folder picker. Selecting a Unicode folder,
saving there with F12, cancelling the picker, cancelling a default-folder draft,
restarting and applying the environment override all pass. The live General
screens were visually reviewed at 720p from a paused campaign and 1080p from the
normal main menu. The main-menu test exited normally without changing player
preferences. Relocated video/audio startup and paused-reload checks still pass.
Evidence is under `work/general-folder-runtime` and
`work/general-settings-{ctest,final-tests,runtime}.log`.

An existing system-inspection smoke could dereference a closed viewport when an
external test navigated away. It now rejects that precondition with exit code 1
and a terminal message; the corrected interaction test retains its expected
system view and finishes normally. No game rendering or simulation rule changed.
