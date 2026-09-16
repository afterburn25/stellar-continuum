# Native video settings

The native main menu and pause menu expose VIDEO from the existing Settings
panel. Display mode, exclusive resolution/refresh, V-Sync and frame cap remain
application preferences. `NativeVideoController`, Window and audio settings
outlive campaign replacement; no Core rule, Player17 field or C# source changes.
Switching from audio to video cancels any unsaved audio preview.

## Window contract

`Window::display_modes()` returns at most 256 unique supported width, height and
refresh tuples for the current display. Borderless uses the desktop resolution.
Exclusive selections use SDL's enumerated modes; Desktop default preserves the
desktop refresh rate rather than silently choosing a higher rate. A mode which
disappears after enumeration is rejected. Fullscreen requests are synchronized,
then checked against actual window/display state. The renderer output size is
refreshed for shared drawing and input coordinates.

V-Sync Off/On/Adaptive is verified against the renderer's actual result. Driver
rejection is reported. Automatic frame cap follows the detected current refresh;
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
The actual Vulkan platform test enters desktop-default exclusive and an
explicit detected mode, restores borderless, rejects invalid settings, and
measures software pacing. It does not claim sustained game FPS.

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
