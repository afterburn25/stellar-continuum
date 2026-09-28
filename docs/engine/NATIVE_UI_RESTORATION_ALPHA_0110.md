<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> Start with [the current handoff](../AGENT_HANDOFF.md) and
> [verified project state](../PROJECT_STATE.md).

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

## Planetary management replacement

Manage Planet now opens the unified planetary operations screen, replacing the
player-facing free terrain-placement workspace. It uses the original colony
panorama and nine building-family illustrations recovered from
`work/planetary-command-window`, with grouped environment, population, resource,
power, economy and research-lab facts, building slots, and a visible queue.
Building reviews show actual prices and material requirements. Placement,
upgrade, repair, enable/disable, priority, cancellation and demolition use the
existing authoritative commands; construction advances only with simulation time
and available funding. Established colony and new-settlement starting conditions
retain the native game's existing balance.

Older native buildings receive a stable read-only slot projection. A successful
placement or removal persists those slots without moving terrain coordinates;
rejected and cancelled reviews do not mutate them. Native Player17 adds an
optional SlotIndex per building. Native save/reload is verified; this does not
implement import of the separate Godot version-19 planetary save format. Source
saves are not overwritten. Use this version for saves containing the new slots.

Checks include legacy-slot preservation, duplicate-slot rejection, cancellation,
timed construction and foundation support, JSON reload, and pause behavior.
Actual Vulkan runs select a slot, review/cancel/confirm through mouse events,
verify modal isolation, and reload the reserved building at 720p and 1080p.
Geometry/input checks additionally cover 1440p and 4K. Earlier terrain-rendering
smokes remain explicitly compatibility probes; a separate planetary smoke tests
the screen used by players.

## Menu hover audio

The original `ui-hover.wav` plays when the pointer enters a different enabled
menu item, game-mode card, setup control, settings control, pause-menu item or
navigation icon. Staying within one item, rendering, help text and disabled
Story/Continue controls stay silent. Modal pages block underlying hover targets.
Hover uses the existing master/effects gain and mute controls, remains rate
limited, and does not restart music. New-game audio checks require a real queued
hover effect; pointer transition tests cover 720p and 1080p.

## Galaxy navigation and star scale

Campaigns open centered on the player's home neighborhood (nearest eight systems),
not the fitted overview. Home returns to this useful scale. The overview is the
minimum zoom and remains centered when dragged; closer views pan the camera
within the fitted world bounds. Stars, lanes and territory share one transform.
Resizing preserves relative zoom. Star light profiles start larger, grow with
magnification, preserve spectral colors/multiplicity and use shared textures.
Giant markers have a larger silhouette. Picking grows with the visible core;
labels avoid bright cores rather than the entire faint corona. Solar-system
star rendering is unchanged. Camera regression checks cover 720p through 4K;
the real galaxy smoke verifies home framing, overview limits, pointer-anchored
zoom, regional panning, and star growth before entering a system.

Map markers use shared 256-pixel profiles with a compact white-hot center,
spectral rim and narrow diffraction rays. The previous broad Gaussian and dark
contrast annulus are removed. A short core falloff keeps magnified stars crisp
without hard planet-like discs or rectangular edges. Unexplored markers remain
neutral and slightly less bright than explored stars, but are now bright points at 86% opacity;
their names, spectral classes and multiplicity remain observer-filtered.
The complete 13-style cache is capped at 3.25 MiB regardless of galaxy size.
Pixel checks cover core brightness, halo falloff, rays and transparent borders.

The galaxy overview now uses the user's supplied spiral artwork. Its original
wide aspect ratio is preserved, its black matte is blended out during background
preparation, and its visible disc is fitted around the authoritative star field.
The existing count-dependent radius (proportional to the square root of system
count) and generated-system spacing remain intact for 250/500/1000/2500 systems.
This preserves neighborhood distances and saved routes while scaling the artwork
to the campaign, with the core still covered by observer fog.

A temporary `Map zoom` readout reports magnification relative to the full-galaxy
overview (`1.0x`). It occupies its own responsive HUD area and reserves space
against star labels. It is hidden on the other workspaces.

Ordinary mouse leave, focus loss and minimization clear stale hover coordinates;
captured map drags can still cross the window edge. Platform input replay covers
leave, return, captured dragging and focus changes. This fixes in-game hover
state, not the separately reported and still unconfirmed desktop cursor glow.
Automated screenshot runs ignore physical player-input events while retaining
window lifecycle events and their scripted interactions, so typing in another
application cannot accidentally unpause a capture fixture.

