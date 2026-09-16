# Native menu restoration — 0.1.10 Alpha

This release restores the approved menu composition and artwork to the C++ client using the five Godot reference photographs supplied on 2026-09-16. It does not claim full Godot gameplay or developer-tool parity.

## Restored presentation

- Open left-aligned main menu over the original cinematic artwork; Continue loads the latest native save or returns to an active campaign.
- Exact approved title logo recovered from the earlier image-generation archive. Its SHA-256 matches the source recorded in the existing application-emblem provenance. The title is now tracked and hash-checked for packaging.
- Approved silver/cyan/gold Windows emblem embedded in both executables. Native navigation uses transparent 256-pixel versions of the existing semantic Godot icons.
- Separate Story Campaign / Sandbox screen with large artwork cards. Story remains Coming Soon, matching the reference.
- Sandbox setup with species portraits and biography, authoritative environment and physiology facts, four galaxy sizes, rival and ancient empire options, an automatically randomized seed, Copy setup and Restore defaults. Long biology content scrolls at 720p; size labels stay on one line.
- Shared blue panels over the original scene. Settings categories are General, Audio, Video, Voice & Subtitles, and Controls. Category and child panels capture input and keep the campaign paused.

## Working settings

Windowed, Borderless Fullscreen (default), Exclusive Fullscreen, detected display modes, refresh/VSync/frame caps, screenshot folder browsing and F12 capture remain available. Video adds the detected adapter, installed NVIDIA Control Panel discovery (classic and Store), scene resolution and 2x/4x supersampling. Supersampling is labelled accurately; it is not presented as MSAA. Scene quality never scales UI text or hit targets. Allocation is bounded to 192 MiB and display previews retain rollback behavior.

Voice settings persist independently: enabled, volume, subtitles, size, background opacity, speaker labels, dry communication filter, frequency and interruption policy. Replay and Stop use the actual integrated scientist announcements. Disabled speech can still produce captions. The default filter is zero, with no reverb or echo. Settings load/save failures preserve usable defaults or the existing saved preferences, and Cancel rolls previews back.

The Development menu provides copyable system information and guidance to the existing in-campaign diagnostics exporter. The separate Godot Developer world and its editing tools have not been ported; the screen states this limitation explicitly.

## Verification

Native unit tests cover save/Continue routing, automatic/randomized setup, copied setup values, responsive bounds and scrolling, artwork identity, independent voice gain, captions, filtering, settings persistence/cancel, display rollback, actual scene-quality pixels, screenshot dimensions and pointer mapping. The Windows display acceptance test exercises all three display modes and restoration on a second connected monitor when available.

Actual application captures, including all settings categories, main menu, mode cards and species setup, are reviewed at 1280×720 and 1920×1080. Layout tests also exercise 1440p and 4K. Package validation additionally launches the relocated runtime, generates a new native campaign, saves/reloads it and checks gameplay workspaces. Export logs and the sealed package's validation sidecar record the final results.

No source saves are migrated or overwritten by this menu restoration. Native Player17 saves continue to use the existing isolated native save location.
