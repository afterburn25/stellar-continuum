# Native C++ galaxy preview

The engine and client target is C++23. The preserved Godot/C# game remains the behavioral reference and current full playable baseline. C# added under `tests/` generates reference fixtures; it is not shipped with the native runtime.

The opt-in `windows-native-preview` preset builds `stellar-continuum-native.exe`. SDL 3.4.16 supplies the window, input and explicit Vulkan GPU rendering. The Engine platform accepts projected lines, soft round points, cached antialiased text, immutable RGBA images and ordered world/panel/text layers, with no Core dependency. The client owns the selected 250/500/1,000/2,500-system campaign through the integrated runtime and campaign frame adapter.

## Current interaction

- RELATIONS opens an observer-safe contact directory, communications artwork, relationship meters, agreements/proposals/history/intelligence, and canonical negotiation/war/proposal commands. Contact changes refresh while paused; stale confirmations are rejected. Scroll drawing and hit areas are clipped together. Claims, grievances and a full demand/trade composer remain in migration.

- Approved main-menu and loading artwork uses immutable cached images, responsive translucent controls and the requested seven-second application boot. Generation/save artwork follows the request through activation; each operation displays a gameplay tip. See NATIVE_STARTUP_ARTWORK.md.

- Detailed class-coloured stellar discs, transient prominences and layered Saturn rings are cached with strict memory limits. See NATIVE_CELESTIAL_APPEARANCE.md.

- Startup offers New Campaign, Load Campaign and Exit. Species portraits, biographies, measured environment facts, size and seed precede generation. Work runs in the background, failures remain actionable, and a new campaign never selects an existing save for its first write. See NATIVE_STARTUP_FLOW.md.

- Owned solid worlds expose an operational surface workspace: choose a known building, pan/zoom, review its exact footprint and costs, confirm placement, or cancel an unfinished site for the canonical refund. Material-dependent progress and complete paused reload equality are maintained. See NATIVE_SURFACE_WORKSPACE.md.

- Select a populated colony/outpost ship and right-click an observed planet for exact settlement terms. Confirm invokes the canonical paid mission; Cancel spends nothing. The inspector shows live timed establishment, and paused saves retain mission identity and progress.

- Owned surveyed planets open a colony workspace with grouped population, labor/support, current reserves, surface production and sites; independently scrolling columns keep 720p readable. Back restores the same body; pause/speed retain it.

- System views display selectable green owned fleets at canonical local positions. Real connected exits sit outside the dotted boundary with fixed-size forest-green arrows and measured labels. Unknown destinations stay masked.
- Pause and speed retain the system view; map gestures are cleared at workspace transitions.

- Known systems open an orbital view with transparent planet discs, eccentric orbits, observer-filtered body stats, left-drag pan, pointer-anchored zoom and Reset/Back navigation.

- Construction shows known projects, requirements, costs, timed progress and canonical Start/Queue/Cancel. Active work precedes queued and completed entries.

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

The combined Engine 0.1.58 candidate passed seven focused native CTests, 18 diplomacy-validator Python checks, 45 packaging/checkout checks and six actual Vulkan diplomacy/system/galaxy launches at 720p/1080p. The diplomacy fixture retains existing contacts and agreements while adding isolated observations and an incoming research exchange. Real UI input changes selection, accepts the proposal, scrolls the new agreement into view, renders the packaged 2172×724 communications scene and hides unidentified identities/metrics/art. Progress validation preserves unrelated state; paused reload compares the complete saved payload except its timestamp. Baseline normalization follows the existing Player17 parity contract for fixture-only metadata and single-precision map coordinates. See the current C++ migration handoff and `work/native-diplomacy-final-*.json`; this is focused candidate evidence, not a new sealed release or full visual parity.

The async manual-save candidate keeps the existing atomic writer/recovery and save schema: immutable snapshots are captured on the owner thread, encoding/IO runs on the existing worker, and queued requests coalesce. Completion is polled even while minimized; failed completion cancels queued manual save/exit requests so they cannot conceal the error. Autosave retry scheduling is unchanged. Explicit load/exit drains remain synchronous. Nine actual Vulkan map/transit/diplomacy launches passed; focused save/session/JSON/recovery results are in `native-save-background-tests.log`. Manual-save CPU update maxima improved from 130.901/130.302 to 7.272/2.610 ms (system), 130.953/129.773 to 5.080/2.955 ms (galaxy), and 32.392–35.940 to 1.008–1.157 ms (travel). Cold system scene remains ~340–346 ms; frame p95 remains ~33 ms including VSync, so this is not a 60 FPS claim. Evidence: `native-save-background-runtime.log`, `native-save-background-diplomacy.log`, and `work/save-background-diplomacy.json`. Earlier diplomacy head `0e0835ae` passed CI `34934085259`; the save candidate requires its own run.

Smoke diagnostics retain the existing frame-interval mean and p95 and now report
`phase_samples` plus mean/p95 for `update`, `scene`, and `render_present` in
milliseconds. These measure campaign update, CPU draw-list preparation, and
renderer submission/presentation respectively. The last phase includes VSync or
fallback waiting; it is not a GPU-only measurement. Capture frames are excluded
from phase samples so screenshot readback and file writes do not inflate them.
Normal play does not retain timing histories. These short checks locate likely
bottlenecks; they are not sustained-FPS certification.

`smoke_timing` also records phase maxima and frame indices for cold frames 1–10,
the frame-61 save request service, capture/transition frames (normally 120 onward), and other
steady frames. Unlike the existing phase means/p95, maxima include capture frames
in their separate bucket; expensive cold work remains visible in the overall maxima.

Cold celestial follow-up: five focused CTests (celestial appearance, planet discs,
system workspace, system colony entry, settlement workspace) and four final Vulkan
system/galaxy launches passed. Optional `--capture` in the existing celestial/planet
test executables records RGBA and dimensions/timings; eight before/after resources
match exactly across all 19,398,656 bytes. This covers three star colors/seeds, the
black hole, front/back rings, and Mercury/Neptune. Star generation improved from
207.7–210.4 to 79.8–80.3 ms by skipping mathematically invisible work; resolution,
granulation, sunspots, corona and transient flare behavior are retained. Full first
system scene is now 209.7–213.0 ms, and final frame p95 remains about 33 ms including
VSync. Further entry/pacing work is needed. Evidence: `native-celestial-cold-tests.log`,
`native-celestial-cold-final-runtime.log`, `work/celestial-pixel-comparison.json`, and
`work/cold-after-{system,galaxy}.json`. Native CI `34936942630` covers the preceding
save fix at `b110e223`; the celestial head `c373aed4` subsequently passed native
CI `34939226242`.

### Background system imagery

The native client now shares one Engine `ImagePreparationQueue` between stars,
rings and planet discs. It uses the existing JobSystem with one worker, a maximum
of 16 outstanding jobs and 32 MiB of reserved output. Ready results still consume
capacity until collected. CPU factories capture immutable appearance/root values;
simulation, image-cache changes and GPU work stay on the owner thread. Existing
final-image/cache limits remain. Missing/corrupt assets deliver a useful path and
cause on the owner thread; they do not become silent fallback successes.

While images prepare, navigation and temporary imagery remain available with a
progress label. Screenshot gates wait for the completed rendered frame (or fail
after 600 extra frames). Added wait frames never extend the steady sample window.
Smoke output reports `artwork_pending_frames`, `artwork_prepare_max_ms` and
`artwork_capture_wait_frames` separately from scene CPU time.

Six focused CTests passed (queue ownership/lifetime/error behavior, complete
synchronous/background RGBA equality, observer-safe planet requests, workspace,
colony entry and settlement). An added workspace readiness/progress/mouse-input
regression and 42 Python system/galaxy/travel export tests then passed. Seven real
Vulkan launches passed the existing image, travel, secrecy and exact paused
save/reload gates; finished Sol and galaxy-to-Sol captures were inspected.

| Scene CPU maximum | 720p before | 720p after | 1080p before | 1080p after |
| --- | ---: | ---: | ---: | ---: |
| First Sol frame | 211.452 ms | 2.936 ms | 211.567 ms | 2.807 ms |
| Galaxy capture transitions | 207.769 ms | 19.779 ms | 209.287 ms | 19.577 ms |

Final artwork arrives asynchronously in about 213–258 ms. Galaxy-to-Sol capture
waits were 15/16 frames, so these images include finished textures, not placeholders.
Four 600-frame paused-map profiles averaged 16.717–16.722 ms, with p95
16.913–17.026 ms and p99 17.487–18.950 ms. Galaxy startup still costs 45–46 ms;
regional scenery and upload/presentation tails remain, and busy campaigns/hardware
are not certified at 60 FPS. No visual-quality reduction or gameplay rule change.

Evidence: `native-background-art-{tests,capture-tests,owner-tests,runtime}.log`,
`work/background-art-{system,galaxy,travel}.json`, and `build-native/preview-*.bmp`.
Baseline: `work/steady-baseline-{system,galaxy}.json`. Previous head `b491783c`
passed native CI `34942125331`; system-art head `96b83092` passed `34946960470`.

### Background galaxy imagery

Deep-field, galaxy-layer and regional-nebula decoding now shares the same bounded
Engine queue as system imagery. Three cache slots allow at most 8 MiB per image;
the existing WIC decode scratch bound is separate. Completed layers retain their
original pixels, order and geometry. Central secrecy fog is present even while
scenery is pending. Main keeps navigation active, shows preparation in the existing
status region and waits for final galaxy imagery before smoke screenshots.

The strict build, three focused CTests and 46 Python galaxy/system/diplomacy export
checks passed. Backdrop tests exercise a deliberately blocked worker, saturation,
duplicate requests, view changes, cancellation, owner-thread violations, missing
paths and an oversized WIC-readable source. Repeated synchronous and asynchronous
oversize attempts fail with the path/cause and leave the cache empty. Final RGBA
bytes match the synchronous path, including the secrecy fog.

Six real Vulkan launches at 720p/1080p passed existing galaxy/system/diplomacy
artwork, UI, observer and exact paused Player17 save/reload checks. Galaxy and Sol
each included 600 steady frames per resolution. Four completed overview/regional
BMP files are byte-identical to the preceding `96b83092` captures.

| Scene CPU maximum | 720p before | 720p after | 1080p before | 1080p after |
| --- | ---: | ---: | ---: | ---: |
| First galaxy frame | 45.432 ms | 3.163 ms | 46.248 ms | 2.695 ms |
| Regional capture transitions | 19.779 ms | 1.027 ms | 19.577 ms | 0.833 ms |

Sol cold scene remains 2.907/2.867 ms. Across all four profiles, steady interval
means were 16.716–16.721 ms, p95 16.935–17.087 ms and p99 17.338–19.141 ms. A cold
render/present maximum of about 67 ms at frame 7 remains to investigate; these
measurements do not certify every frame, busy campaigns or other hardware at
60 FPS. There is no visual-quality reduction or authoritative simulation change.

Evidence: `native-galaxy-background-{tests,runtime}.log`,
`work/galaxy-background-{galaxy,system,diplomacy}.json`,
`work/galaxy-background-pixel-comparison.json`, and `build-native/preview-*.bmp`.
Previous head `96b83092` passed native CI `34946960470`; the galaxy checkpoint
requires its own run. It remains an unmerged Engine 0.1.58 candidate.

### Optional steady-frame profile

Add `--profile-frames 600` to an isolated `--system-smoke <capture.bmp>` or
`--galaxy-art-smoke <capture.bmp>` invocation. Only whole numbers 120–3600 are
accepted. The existing warm-up and frame-60 manual-save request remain; the
requested extra frames run before screenshot/transition work. Existing smoke
defaults are unchanged. The helpers `validate_native_system_export` and
`validate_native_galaxy_export` accept keyword `profile_frames=600`, retain all
their normal gates and return `systemProfiles` / `galaxyProfiles` respectively.

`steady_profile` contains the exact sample count and mean/p50/p95/p99/max for
frame interval, update, scene, CPU submission, readback/write, fallback throttle,
and present. Readback must be zero for these samples. The validator rejects
missing/duplicate/truncated JSON, nonfinite/negative metrics, invalid counts and
inconsistent quantiles. A minimize/restore interruption cannot replace a valid
sample with a discarded interval. No sample history or per-phase draw clocks
are collected in normal play. CPU presentation time can include GPU/driver/display
waits; it is not GPU execution time and should not be summed with interval time.

Four 600-frame profiles of a paused 500-system campaign:

| View | Resolution | Interval mean / p95 / p99 (ms) | Update / scene / submission mean (ms) | Present mean (ms) |
|---|---|---|---|---|
| Sol | 1280×720 | 16.716 / 16.869 / 17.375 | 0.026 / 0.137 / 0.258 | 16.275 |
| Sol reload | 1920×1080 | 16.717 / 17.025 / 17.284 | 0.033 / 0.175 / 0.323 | 16.158 |
| Galaxy overview | 1280×720 | 16.718 / 16.834 / 18.382 | 0.026 / 0.053 / 0.260 | 16.355 |
| Galaxy overview reload | 1920×1080 | 16.723 / 16.973 / 19.705 | 0.026 / 0.059 / 0.279 | 16.317 |

All four passed actual Vulkan rendering, artwork/observer validation and exact
paused save/reload. Readback and fallback throttle were zero throughout. Evidence:
`native-steady-baseline-runtime.log`, `work/steady-baseline-{system,galaxy}.json`.
The strict build, platform pixel/timing-reset test, 34 Python tests and four
rejected native CLI cases are recorded in `native-steady-profile-{build,tests}.log`.
These measurements support roughly 60 FPS after warm-up on this host; they do not
cover busy campaigns or certify every resolution/GPU. Cold entry still takes
~210–213 ms and needs staged asset preparation. Screenshot work remains visible
in the separate timing maxima. No quality, authoritative simulation or save
contract was changed by profiling.

Engine0.1.54 passed143CTest,245Python checks and28actualVulkan launches. New-game input and reload prove selected species/size/seed metadata, independent Unicode save paths, unchanged existing campaign bytes and whole paused payload equality exceptSavedAtUtc. The four screenshot sidecars cover setup, actual generation status, new campaign and restored campaign. Tests reject spoofed diagnostics, unsafe paths, malformed captures and altered payloads. The final load-list scrolling fix is included in the combined build.

Engine 0.1.53 passed 138 CTest, 206 Python checks and twenty-six actual Vulkan launches. Surface validation starts from an unaltered fresh 500-system campaign and verifies preview cancellation, paid placement, half-refund cancellation, noninstant progress and exact paused reload/resave. Engine image-overlay tests use actual GPU pixel readback for ordering, clipping, tint and immutable texture reuse. Surface captures at 720p/1080p were inspected; this operational grid is not final 3D presentation.

Engine 0.1.49 passed 128/128 CTest, 96 Python checks and thirteen actual Vulkan launches. New orbital checks select Earth, pan, zoom, reset and return at 720p/1080p, with full paused save/reload equality. Both runs upload nine immutable images; camera movement reuses their identities. Platform tests cover Unicode decoding, malformed/oversized inputs and cache eviction. This is an orbital preview, not full visual parity. See NATIVE_SYSTEM_VIEW.md.

Engine 0.1.48 passed 125/125 CTest and 83 Python checks. Eleven real Vulkan launches cover map, research, fleet, shipyard and construction inputs and saves. The maintained production validator rejects wrong design/authorization, nonfinite values, missing debit, skipped saves and reload mutations. Fresh seed 115501 reaches real scout transit through ordinary research and facilities; seed 115500 retains its canonical failed hypothesis and locked ships. See NATIVE_CONSTRUCTION.md and NATIVE_FRESH_PROGRESSION.md.

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

Production HUD styling, full construction/unlock progression, remaining diplomacy commands, 3D planet/surface views, orbital structures, tactical presentation and audio remain. Native new-game selection/generation, orbital browsing, owned-colony telemetry, ship-delivered settlement, operational surface placement/cancellation and basic diplomacy are integrated. These are native migration workspaces; complete source-game behavior and visuals remain unfinished. Current Player17 UTF-8 recovery is maintained; UTF-16 input remains explicitly excluded. The preview is not a replacement player release and does not claim visual parity with the user's reference images. `windows-release` remains blocked.
