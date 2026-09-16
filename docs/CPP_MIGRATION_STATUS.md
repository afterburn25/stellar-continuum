# C++ migration status

Branch of record: `engine/stellar-engine-migration` (head `ac45d958`, engine 0.1.57).
Devin/SWE-2 working branch: `cpp/devin-swe2-native-conversion` (reviewed head
`d9d23f57` inventoried; support, notifications, tactical, civilian recovery
safe video settings, observer-safe system inspection and save-gated New Game selectively adapted. New voice,
logistics and newest empire/mission boards remain unimported pending review).
Codex working branch: `cpp/codex-native-architecture-integration`, based on `ac45d958`.
Reference: Godot 4.7.2 / C# / .NET 8 under `src/`, retained as behavioral and visual truth.

Evidence basis: states below cite real artifacts — `core/` parity CTests (`*_parity`,
145+ cases), `app/native_client/` presentation modules, `tools/stellar-export/` sealed
validators, and `docs/engine/*_VALIDATION.md` contracts. "PARITY VERIFIED" means the
subsystem has a maintained parity/validation gate that runs in the sealed export.

## Current integration checkpoint (2026-09-15)

### Observer-safe galaxy system inspection

Single-clicking a star now opens a native system card with survey progress,
metric/light-year distance from home, authorized stellar findings and every
owned settlement in that system. The navy/cyan card uses renderer-measured
wrapped text, pinned header/progress/close controls, separate settlement fields,
and a bounded scroll indicator. Wheel/drag input is captured before map/fleet
handling; Research and other workspaces hide the card. Paused updates rebuild
its observer-filtered value snapshot; campaign replacement clears it.

This selectively adapts Devin d2a41caa. Contact plus survey no longer leaks live
foreign colony existence, count, names or statistics. Unknown names/class/traits
remain hidden; detected/partial names follow the reference. Home chart distance
is intentionally public. Own colonies sort by name/ID and validate body membership.
No economy/logistics recomputation, swallowed exceptions, Core changes, Player17
changes or C# changes. Contract: docs/engine/NATIVE_SYSTEM_INSPECTION.md.

Validation: final MSVC native build and all 9 affected CTests pass. Export-tool
unit discovery reports 452 tests, 435 passed and 17 skipped because the optional
STELLAR_NATIVE_EXE integration fixture was not configured. Two relocated actual
Vulkan runs cover fresh 720p and 1080p reload, actual chart hit testing, known /
unknown / scrolled-end screenshots, mouse capture, Research roundtrip, close,
unchanged campaign/camera/selection and exact paused Player17 equality except
SavedAtUtc. Existing relocated navigation and fleet suites also pass. Final card
screenshots inspected; no body text escapes the header/panel or bottom boundary.
Evidence: work/native-inspection-{build,ctest,python}.log and
work/native-inspection-{runtime,navigation,fleet}.json; captures under
work/native-audio-validation/package-inspection-*.bmp. The package is UNSEALED.

Next: adapt ea9164e9 supply network with explicit unavailable/failed/retry states,
validated player/home identity, a single Core projection per refresh, and bounded
input/layout. Never turn exceptions into permanent “initializing” or invent supply
values. Latest fetched Devin head d9d23f57 includes bc681eb6 generated planet art,
fe364430 generated star art and d9d23f57 mission Land/Collect commands; these are
inventoried only. Review observer visibility, command revalidation, alpha edges,
bounded image preparation, asset provenance and preservation of approved Sol
textures before import. Earlier 641955f0 physical inspector and 1c8e9744 surface
management remain unimported. Shared base ac45d958 and PR332 remain unmerged.

### Previous checkpoint: safe native video settings

VIDEO is available through the existing main/pause Settings panel. Borderless
and exclusive fullscreen, detected resolution/refresh pairs, verified V-Sync
and Automatic/60/120/144/Unlimited pacing are owned outside campaign lifetime.
Apply is a 15-second preview; Keep alone persists atomically. Escape, inactivity,
expiry, rejected partial changes and write failures restore the previous mode.
Failed rollback gets one safe fallback; repeated failure reports unknown display
state and asks for restart without an automatic retry loop. Desktop default
preserves desktop refresh. Resolution options support forward/back selection.

This selectively adapts Devin 9b5ba16e. Ignored save errors, partial backend
acceptance, wrong SDL mode selection, missing resolution choices and session-owned
preferences were corrected. Preserve these interfaces when importing other
Devin work; do not replace recorded scientist cues or merge the old settings
lifecycle. No Core, Player17 or C# changes. Contract/reproduction:
`docs/engine/NATIVE_VIDEO_SETTINGS.md`.

Validation checkpoint: MSVC native build, 13 affected CTests and 54
Python startup/restart/audio validator tests pass. Actual Vulkan display tests
enumerate 227 modes on this machine, switch exclusive resolutions, restore
borderless, reject invalid settings and measure frame-cap throttling. Relocated
720p new-game / 1080p paused reload routes verify audio/video preview, rollback,
Keep and persistence, original-save preservation, independent new slot and full
paused Player17 equality except SavedAtUtc. Screenshots inspected; no overlapping
controls. Windowed smoke checks do not prove fullscreen behavior themselves;
the separate platform test covers that. No sustained-60-FPS claim.

Evidence: `work/native-video-{build,ctest,python}.log`,
`work/native-video-runtime.json`, and the `package-new-game-*-video-*.bmp`
captures under `work/native-audio-validation`. Five relocated live New Game runs also pass, retaining save gating,
Return/Exit handling and music/settings lifetime. Evidence:
`work/native-video-restart.json`.
Previous 1d42cee1 passed GitHub Actions 35046505665. The validation package is
UNSEALED; shared base ac45d958 and PR332 remain unmerged.

Next: observer-safe system inspection and explicit logistics failure/recovery.
Devin 1c8e9744 surface management and 641955f0 system physical/environment fields
were inventoried, not audited/imported. Review ownership and visibility before
adapting commands or exposing foreign colony data. Broader 3D graphics and
sustained performance work remain open; see prior checkpoints below.

### Previous checkpoint: surface inspection

### Colony surface focus and truthful operating status

Completed facilities no longer automatically claim to be operational. The
shared native status projection distinguishes construction, disabled, repair
needed, no workers, no power and operating, in prerequisite order. Both scene
markers and the selected inspector use it. Power and staffing remain membership
in Core's allocation output; the inspector shows condition and the projected
condition efficiency separately from actual production.

The surface header now offers **Overview** and **Focus Selected** using shared
render/hit rectangles. Overview fits horizontal and vertical extents separately;
Focus Selected centers and enlarges the chosen facility (the hub when nothing
is selected). Left-drag and wheel navigation remain available, routine colony
refresh preserves the camera, and confirmation modals capture camera input.
No authoritative Core, economy rule or Player17 field changed.

Validation: final MSVC native build; seven affected CTests; 62 surface-validator
Python tests pass. Controller tests exercise actual Core power/workforce
allocation, disabled/damaged/unfinished precedence and immutable old views.
Layout/input tests cover 720p, 1080p, 1440p and 4K. Six relocated Vulkan runs
cover fresh construction, paid cancellation/refund, paused reload, populated
720p/1080p surfaces and a low-workforce 720p fixture. Native input selects every
fixture facility, checks its rendered inspector label and compares its flags
with Core output. Focus captures prove enlargement, refresh preservation and
Overview recovery; paired image-layer captures prove clipped building pixels.
The complete paused Player17 payload remains unchanged except SavedAtUtc.

All populated views report 11 ready/drawn structures, 2,883,584 cached bytes,
and zero pending/deferred/failed/reserved work. 720p focus increases scale from
1.07692 to 3.22388; 1080p from 1.68 to 4. Captures were visually inspected.
The initial runtime proof incorrectly assumed a manually panned view was
Overview; it now establishes Overview through input before comparing cameras
and captures. This was an evidence correction, not a gameplay-rule change.

Evidence: `work/native-surface-clarity-build.log`,
`native-surface-clarity-ctest.log`, `native-surface-clarity-python.log`, and
`native-surface-clarity-runtime.json`. Captures/raw logs are under
`work/native-audio-validation/package-surface-*`. Reproduction and contracts:
`docs/engine/NATIVE_SURFACE_BUILDING_PRESENTATION.md` and
`tools/stellar-export/native_surface_runtime.py`.

Previous head 2cb82d83 passed GitHub Actions 35043992054. Its save-gated live
New Game lifecycle remains intact; see `NATIVE_NEW_CAMPAIGN_REVIEW.md` for
cancel/save failure/exit/unique-slot and exact roundtrip evidence. Shared base
ac45d958, C# reference and PR332 merge state are unchanged. The local validation
package remains UNSEALED and is not a released download. This remains an
oblique surface image layer with prototype architecture, not finished 3D cities
or a sustained 60 FPS certification.

Devin is inventoried through 9b5ba16e (VIDEO settings), including e49f4df0 colony
sites and the newer empire/mission boards; those changes are not yet audited or
imported. Next: review and integrate compatible native video settings with
safe display rollback and the existing audio/settings lifecycle, then
observer-safe system inspection and explicit logistics failure states.
Preserve recorded British female scientist cues; do not import foreign live
colony statistics based solely on contact/survey.

## Previous integration checkpoint: civilian recovery (2026-09-15)


### Civilian recovery and populated surface validation

The fleet panel now exposes Hold/Resume and Return to Base for owned civilian
mission ships, selectively adapting Devin `263b4396`. Commands bind observer,
campaign, ship and displayed mission state. Paid colony return shows the full
Core warning with separate confirmation and cancellation. Changed missions,
selection, focus and menu transitions invalidate approval. Frequent outliner
refreshes do not run route planning. Core recovery/movement rules are unchanged.

Final MSVC host build and five affected CTests pass: fleet controller,
workspace, presentation, system workspace and Core civilian recovery parity.
Python validators pass 10 fleet and 61 surface tests. The final relocated
Vulkan executable passes two fleet captures, two system-travel captures and
five surface captures. Fleet hold/resume uses native mouse events; ordered
travel and exact paused reload remain verified. Paid abandonment and queued
return in warp are controller-test evidence, not a live paid-colony screenshot.

The surface validator adds a clearly labeled test-only Player17 gallery: nine
completed power/research/housing/industry sites and one unfinished generator.
At 720p/1080p all ten sites plus the hub are ready and drawn (11 rasters,
2,883,584 bytes; zero pending/deferred/failed/reserved work). Entire paused
payloads remain equal except SavedAtUtc. Enabled, disabled, priority and
repair-condition inputs are checked. Powered/staffed flags are Core-derived
and not directly diagnosed. This sparse gallery is not a gameplay-built city;
small silhouettes at fit-to-all scale remain a visual gap, especially at 720p.

The fixture preserves a valid timestamp and removes only its temporary backup
before authored-primary launches. Earlier invalid test timestamps had caused
recovery of the old backup; strict equality exposed that test setup error.
No production save schema, surface footprints, economy or C# files changed.

Evidence: `work/native-civilian-recovery-build.log`,
`work/native-civilian-recovery-ctest.log`,
`work/native-civilian-recovery-runtime.json`,
`work/native-civilian-recovery-system-travel.json`, and
`work/native-surface-populated-final.json`. Contracts:
`engine/NATIVE_CIVILIAN_RECOVERY.md` and
`engine/NATIVE_SURFACE_BUILDING_PRESENTATION.md`.

Devin was inventoried through `263b4396`. New Game still ignores restart exit.
Inspection needs observed foreign-colony data; logistics needs explicit error
states. New voice/SAPI work is not fully audited or imported; retain existing
recorded UK-female scientist cues. Details: `engine/NATIVE_NEW_CAMPAIGN_REVIEW.md`.

Next: repair mid-session New Game, then improve surface overview legibility
and live operating-state proof. Package remains UNSEALED; PR #332 unmerged;
sustained 60 FPS is not established.

### Live native surface building presentation

The colony workspace now consumes prepared building images through an
observer-owned presentation adapter. Worker jobs prepare geometry/raster data;
owner updates coalesce visible hub/site/quote requests and drawing only reads
ready images. Exact quoted rotation, construction state, power/staffing,
condition, homeworld/outpost identity and campaign scope remain bound. Missing
or failed imagery keeps usable fallback meshes; explicit reopen retries errors.
Core construction, costs, canonical footprints, obstruction routing and
Player17 remain unchanged.

Actual captures exposed inward cylinder/ellipsoid winding and a duplicate
flat apron that made buildings appear to sit in raised bowls. Both were fixed
with regressions. Cosmetic road ends now continue beneath foundations while
the cached route and obstruction geometry remain unchanged. Ready and fallback
structures share depth order, with selection/status drawn last.

Final MSVC build and eight affected CTests pass; 55 Python evidence tests pass.
The final relocated Vulkan package passes fresh 500-system generation, paid
surface placement/cancel/refund at 720p and exact paused Player17 reload at
1080p. Both captures were inspected. Each shows the completed hub and one
unfinished generator using two ready images (512 KiB), zero pending/deferred/
failed work, and matched fallback-only captures proving real in-terrain RGB
changes without outside clipping changes. This sparse fixture does not prove
a complete populated city or every finished building family in live play.
The 68-variant geometry/raster matrix and workspace tests cover further states.

Two 120-frame profiles include balanced wheel/drag input. At 720p/1080p,
steady owner-update maxima were 0.328/0.309 ms, scene maxima 0.319/0.407 ms,
and mean frame intervals 16.715/16.735 ms. Frame p95 was 18.680/17.534 ms;
cold render/present spikes reached about 66.6 ms. Sustained 60 FPS is therefore
not established. GPU capture time is excluded from these samples.

Evidence: `work/native-surface-host-build.log`,
`work/native-surface-host-ctest.log`, `work/native-surface-live-runtime.json`,
and `work/native-audio-validation/package-surface-{1280x720,1920x1080}.log`
and BMP pairs. Contracts: `engine/NATIVE_SURFACE_BUILDING_PRESENTATION.md`.
Prior preparation c4c99fa5 passed GitHub Actions 35034467899. This local
package is still UNSEALED; full 3D surface navigation, populated cities,
manual roads and distinct alien architecture remain open.

This preceding checkpoint is extended by the populated proof above; live
operating-state diagnosis and the New Game lifecycle remain open.

### Tactical corvette artwork and interaction

The native tactical view now uses a transparent, top-down derivative of the
approved human patrol corvette. Exact owned encounter/vessel/design bindings
feed a bounded camera projection, rotated hulls and motion-driven blue nozzles.
Only human-player patrol corvettes use this first hull; alien/foreign/unsupported
ships and aggregate cohorts retain their markers. One full-resolution RGBA
resource is shared across at most 32 visible instances. The portrait cache is
unchanged. Core simulation, Player17 and the documented zero-fleet compatibility
exception are unchanged.

Clicking the actual hull selects its formation, with rotated hit geometry and
transparent-corner rejection. Camera/viewport changes invalidate geometry;
same-battle observer refresh preserves the last drawn click surface, while
removed/foreign targets are dropped. Labels reserve sprite space. See
`engine/NATIVE_TACTICAL_SHIP_ART.md` for asset, rendering and evidence contracts.

The final native MSVC build and six affected CTests pass. Ninety Python tests
across the battle, ship-art and native-client validators pass. Two relocated
Vulkan runs at 720p/1080p prove direct hull selection, an accepted Core order,
paused canonical equality, actual F6 save and exact reload except SavedAtUtc.
The detailed corvette uses one 1254-square RGBA resource; hostile detail remains
inexact and the hidden picket remains undisclosed. Matched captures with the
ship layer disabled confirm hull pixels, clear corners and no background changes.
Both final captures were visually inspected. Four neighboring ship-art and
500-system navigation runs pass with the final executable.

Evidence: `work/native-battle-sprite-build.log`,
`work/native-battle-sprite-ctest.log`, `work/native-battle-sprite-runtime.json`
and `work/native-battle-sprite-neighbors.json`. The local package remains
UNSEALED. This is one detailed 2D hull family on a schematic tactical battlefield;
full 3D ships fighting in the solar-system scene remain open. The previous
checkpoint `319ed4cd` passed GitHub Actions run `35027228449`; CI for the new
artwork checkpoint is recorded separately after publication. No sustained-60-FPS
claim is made: neighboring captures still measure cold artwork preparation spikes.

Devin's new `b9e55e79` mid-session New Game change is source-reviewed but not
imported. Its restart flow ignores explicit startup exit. Before integration,
split cancellation from Exit to Windows, preserve the current save/audio/cache
lifecycle, and test cancellation and save failure. The exact gates are in
`engine/NATIVE_NEW_CAMPAIGN_REVIEW.md`. Surface sprites and quoted-action
shortcuts remain pending their previously recorded corrections.

### Native tactical battle integration

The native client now opens an observer-filtered tactical workspace for active
encounters. Stationed armed fleets expose ENGAGE HOSTILES through the existing
fleet action slot; Core validates participation and hostility. Formation picking,
box selection, group orders, targeted orders, pan/zoom, fit, pause/resume, separate
speed control, menu and F6 save route through the existing campaign owner.
Changing speed while paused changes only the resume speed. Ordinary strategic
workspaces cannot receive hidden input during battle.

This selectively adapts Devin `357872e8`. Ordered clipped geometry repairs ships
hidden beneath the incoming opaque backdrop; owned context/non-targeted orders
apply to the whole selection. Cancelled/orphan/cross-panel gestures cannot issue
orders. Camera initialization is explicit, fit reserves control space, and dash,
token and effect work is bounded. Rendered captures exposed and corrected clipped
formation text and overlapping event rows. The corvette layer above extends
this checkpoint; it does not complete detailed ships in a solar-system scene.

Manual saves now accept a successfully completed tactical frame. Player17 shape
and Core clock/save flags remain unchanged, and strategic autosave policy is
unchanged. Paused/running capture, exact reload, matched continuation, completed
encounter reconciliation and failed-load recovery have maintained session tests.
The lifecycle tests exposed a separate zero-fleet identity defect: native fleet
0 uses reserved tactical vessel ID 2^32, while nonzero identities remain unchanged.
Begin, observer bindings, encounter validation, reconcile and reference validation
agree. Legacy C# still has this defect and cannot read that new zero-fleet tactical
identity; see `engine/NATIVE_TACTICAL_WORKSPACE.md` for the explicit parity exception.

Final MSVC build, twelve affected CTests and five Python evidence tests pass.
Two relocated Vulkan tactical replays at 720p/1080p select an owned formation,
issue a Core order, run eight tactical seconds, pause and save through actual F6
routing with no fallback. Reload preserves the entire canonical payload apart
from SavedAtUtc. An unobserved picket stays hidden and persisted; enemy detail
remains inexact; actual green token pixels are checked. Four neighboring fleet
and 500-system navigation replays also pass. See `work/native-battle-focused.log`,
`work/native-battle-runtime.json`, `work/native-battle-neighbor-runtime.json`,
`work/native-battle-python.log` and `work/native-battle-final-build.log`.
Final label-layout edits additionally passed `work/native-battle-final-workspace.log`
and regenerated both tactical Vulkan captures; neighboring checks preceded only
those isolated battle label changes.

The local package is unsealed; PR #332 remains unmerged. No 3D, graphical-parity,
release or sustained-60-FPS claim is made. Next: carry approved ship/system art
into tactical presentation, then address the remaining surface sprite contract
or stable quoted-action shortcuts. Devin is reviewed through `22cff368`; support
and tactical are now selectively adapted, surface `58aaf475` and shortcuts
`31a28e3c` remain unimported. Shared base remains `ac45d958`.

### Native diagnostic export integration

F8 and the pause menu now export a local support ZIP through one bounded
background worker. Repeated requests do not queue; existing exports are never
overwritten. The archive contains a bounded recent log, truthful native runtime
metadata and the last completed save. Read/write/publication failures remain
in the UI while the campaign stays open. No save request, simulation change,
Player17 field or upload is introduced. See `engine/NATIVE_SUPPORT.md`.

This selectively adapts Devin `22cff368`. Its eager disk writes, synchronous
UI-thread work, overwrite risk and uncaught file errors were corrected. Candidate
shortcuts `31a28e3c` were reviewed but not imported pending stable selection,
quoted-action integration and successful-command evidence. Existing navigation,
research, production and Core authority remain in their established paths.

MSVC build, four focused CTests and six Python validator tests pass. Two
relocated Vulkan support runs exercise menu/F8 exports and an actual blocked
destination, prove unique valid ZIPs, unchanged save bytes and full canonical
state, and show success/failure panels at 720p/1080p. Two neighboring navigation
runs retain paused fresh/reload and keyboard/modal behavior. Evidence:
`work/native-support-build.log`, `work/native-support-focused.log`,
`work/native-support-python.log`, `work/native-support-runtime.json` and
`work/native-support-navigation-runtime.json`. No sealed release is claimed;
PR #332 remains unmerged. Previous notification-head CI `35019577587` was still
in progress at review; no CI result is attributed to this new checkpoint.

### Native recent-events integration

The top-right Events button now opens a scrollable, dated history of the last
32 reports. It shares the existing observer-filtered campaign summaries and
retains accepted research, shipbuilding and construction outcomes. Diplomatic
reports use Core's audience view, require identified counterparts for links,
and use player-facing messages without internal IDs or enum names. Following
OPEN RELATIONS selects the identified empire through the existing workspace.

This selectively adapts Devin `8f6b720a`; its raw session event harvesting,
separate audio loop and notification-only smoke mode are not imported. There
is one existing audio path. Admission/load seeds retained diplomatic history
silently; this session-local feed is cleared on activation and is not persisted
into Player17. All 32 items are scrollable, text uses measured wrapping, and
matching press/release ownership prevents stray clicks from activating links.
See `engine/NATIVE_NOTIFICATIONS.md` for contract, review and validation.

Final MSVC build and five focused CTests pass. Thirty-eight Python diplomacy
and navigation checks pass. Two relocated Vulkan diplomacy launches verify
fresh acceptance, event opening/acknowledgement, a link changing the selected
empire, full canonical state preservation, and silent paused reload. Two
neighboring 500-system navigation launches verify the existing input/save
contract. Final panel captures at 720p and 1080p were inspected. Evidence is
`work/native-notification-runtime.json`,
`work/native-notification-navigation-runtime.json`,
`work/native-notification-final-build.log` and
`work/native-notification-focused.log`. The previous keyboard head `830e7907`
passed Windows CI `35015114001`; that CI result belongs to the previous head.
The package remains unsealed and PR #332 remains unmerged.

Strategic Space/1-4/F6 shortcuts are integrated with the existing clock and save
service, including galaxy/system routing and modal/text ownership. Four CTests,
six Python navigation checks, two relocated navigation replays and two research
replays pass. F6 is the navigation replay's only save request, with complete
paused fresh/reload payload equality except `SavedAtUtc`. Full details and
scope limits are in `engine/NATIVE_NAVIGATION.md`.

The native C++ client now has a compact left icon rail for Research, Shipyard,
Construction and Relations, using the four approved designs. It reserves
workspace space, routes matching input before workspace handlers, and leaves
pause/play and speed in the top strip. See `engine/NATIVE_NAVIGATION.md` for
the shared layout contract and final validation evidence. The previous
research inspector repair remains integrated; evidence is in
`engine/NATIVE_RESEARCH.md`.

Incoming tactical port `357872e8` remains outside PR #332 while render order,
group commands, gesture ownership, paused speed and save checks are corrected.
Devin head `e390b659` adds duplicate audio settings and timing diagnostics;
these were reviewed in issue #324 comment `5685997691` and not imported. Our
bounded timing already separates update, scene, submission, readback, throttle
and present; the incoming counters retain unbounded normal-play history and
cannot establish GPU-only timing. Prioritize corrected tactical work and
uncovered native behavior over a second audio or diagnostics implementation.
Historical evidence below belongs to its named checkpoint, not this head or a
sealed release.

Latest fetched Devin head `7a85a099` adds keyboard shortcuts (`749ad8e7`),
not the pending tactical corrections. The Engine key primitive and strategic
mapping are selectively adapted with stronger input/modal ownership; the
tactical shortcuts remain unimported.

### Native HUD and pause-control checkpoint (2026-09-15)

The six campaign controls and pause-menu buttons share restrained blue panels,
hover/active accents, padded text clipping and measured font fitting. Label
height comes from the actual render font so text stays centered. At most five
cached measurements fit each label; the last measured size is also the drawn
size. Existing hit rectangles, workspace routing and simulation rules are
unchanged. This is a small visual pass: the compact icon navigation requested
for final HUD parity is still outstanding.

Final MSVC native build passed (`work/native-hud-final-build.log`), as did
`native_ui_layout` and `native_client_input` (2/2 CTests). The focused
`test_native_client_runtime.py` suite passes 58 checks, including missing,
wrong-size, truncated, uniform and malformed reload capture rejection, plus
acceptance of valid vertical stripes. Three final Vulkan
launches passed: fresh pause menu at 1280x720, paused Player17 reload at
1920x1080, and active research at 1280x720. Both menu captures and the research
capture were inspected after the measured-text correction. Complete paused
save equality excludes only `SavedAtUtc`. Evidence:
`work/native-hud-validation/native-client-validator.json`,
`work/native-audio-validation/package-native-preview-fresh.bmp`,
`work/native-audio-validation/package-native-preview.bmp`, and
`work/native-hud-validation/research-final-1280x720.bmp`.

The maintained export check now requires separate fresh/reload images at those
exact dimensions, valid BMP structure and a complete nonuniform pixel payload.
It preserves both captures and resolves the package path before changing the
working directory. This closes a false-positive gap where a missing reload
image could reuse the fresh image. These checks establish captured output, not
artistic quality. Research inspector spacing/status clipping at 720p remains
a follow-up alongside compact icon navigation.

Devin was reviewed through `bfc51a23`. Surface sprites `58aaf475` are not imported:
rotation, canonical footprint, completion-state, road and bounded preparation
contracts need correction before the useful geometry can be integrated. See
`docs/engine/NATIVE_SURFACE_SPRITE_REVIEW.md` and issue #324 comment `5684977733`.
Surface head `f13192ea` passed Windows CI `34998925922`; that result does not
apply automatically to this new HUD checkpoint. PR #332 remains unmerged and
unsealed, with full 3D and sustained 60 FPS unproven.

### Native top-down surface checkpoint (validated, 2026-09-15)

Three actual Vulkan launches passed on the unaltered fresh 500-system campaign;
ordered 720p and paused reload 1080p evidence is in
`work/native-surface-scene-evidence.json`. Placement, cancel/no-change,
half-refund, unfinished progress, paused Player17 equality and observer gating
remain proven. Counters are `1/13/142/1` ordered and `1/14/174/1` reload for
sites/meshes/triangles/roads. The presentation-only 14-type gallery passed at
both resolutions with 20 meshes, 1,302 triangles and 14 roads. Six unique CTests
and 121 Python checks pass; no manual roads, full 3D, finished-art, FPS or
sealed-release claim is made.

The scene uses clipped, batched C++ triangle geometry for building families,
canonical positions and rotations, layered roofs/status indicators and a hollow
selection ring. It fits the local colony on opening and clamps pan/zoom. Roads
connect the hub and site aprons, check other footprints analytically and cache
world-space routes; six bounded two-leg alternatives are tried before omitting a
blocked route. The road-gap and hub-boundary rounding failures found during
visual validation were repaired. This is cosmetic routing, not manual roads or
full legacy A* parity. Advanced modules currently share their functional family
silhouette. Scene limits are 128 sites, 8,192 triangles and 192 road segments,
with simpler bodies above 24 sites; Core placement still enforces its own limits.
The native client, workspace, art and optional gallery targets build with MSVC.
The six focused CTests cover colony/construction controllers, surface workspace,
triangle-mesh safety, image preparation and terrain assets. Python evidence is
46 surface validator tests plus 75 colony/client neighbor tests. Real campaign
captures and the separately labeled 14-type presentation gallery were inspected.
The gallery is not proof of a naturally developed 14-building campaign.

Surface head `f13192ea` passed Windows CI `34998925922`, including foundation
build/test/export and native client/presentation checks. Prior readability head
`884accb2` passed Windows CI `34992777279`. Core, legacy C#,
Player17 and approved terrain artwork are preserved. Next work should improve
native HUD styling and detailed colony presentation toward the legacy reference;
3D surfaces, manual roads and broader character casting remain incomplete.

### Historical readability checkpoint

This native C++ checkpoint builds the full MSVC app with the galaxy-label target at
`/W4 /WX`. Six focused CTests passed in 1.67 seconds (`native_diplomacy_controller`,
`native_diplomacy_workspace`, `native_galaxy_backdrop`, `native_galaxy_star_markers`,
`native_galaxy_labels`, and `native_territory_projection`), recorded in
`work/native-label-runtime.log`. Ninety-seven Python checks passed in 6.183 seconds
across the three native diplomacy, galaxy, and client runtime suites
(`work/native-label-python.log`).

Four relocated real-Vulkan runs passed: 500-system galaxy fresh/reload at 720p and
1080p, and diplomacy acceptance/reload at 720p and 1080p. The evidence files are
`work/native-label-galaxy-evidence.json` and `work/native-label-diplomacy-evidence.json`.
They preserve the complete paused Player17 payload, observer secrecy, unrelated
acceptance state and claims, and the source-fixture hash. Overview reports 500
canonical markers with zero label audit counts. Regional 720p and 1080p each report
6 candidates, 6 measured, 3 placed, and zero collision/offscreen audit counts. Root
inspection covered overview 1080p, regional 720p/1080p, and diplomacy 720p: Sol and
empire names are visible, fill is stronger, names do not stack, and the 720p empire
label leads to territory outside its border.

Placement measures the actual render font and filters invalid/offscreen candidates
before deterministic sorting. Placements are collision-checked after measurement. It
prioritizes selected system, empire, then nearby known stars; tests 48 fixed,
size-aware empire positions nearest first; and keeps an anchor leader line when the
gap exceeds 32px. HUD, stars, the fleet panel, labels, and viewport bounds are checked.
The maximum is 128 text measurements, including 32 empires. The 2,500-candidate dense
test validates that measurement budget only, not 2,500 GPU labels or
60 FPS. Fill multiplier/contour are `0.16`/`0.72`; known names without a valid place
are intentionally omitted. Selected priority is unit-covered only. Core, projection
math, and Player17 remain unchanged.

`43622e72` is the prior checkpoint: its 95 Python checks and CI `34985891256` covered
seven presentation targets before galaxy labels. Its faint-fill/overlapping-label
finding is addressed above. Current 97-test and label evidence belongs to the
historical readability checkpoint `884accb2`, which passed CI `34992777279`. `graphicalParity=false`; the Engine 0.1.58 candidate/
game 0.1.7-alpha is unmerged and unsealed, and clean-machine and broad-hardware 60 FPS
are unproven. `891fbcb2` and duplicate-audio `dbf07f81` remain unimported; PR #332
audio is authoritative. Review found callback races, clip-tail looping, non-atomic
lowercase preferences, and lifecycle conflicts (issue #324 comment `5682904661`).
`eb1ab876` remains unimported until Core provides a physical-body/system/orbit contract.
Next: wider casting and final graphical parity work.

- Devin/SWE-2 was reviewed through `891fbcb2`. Territory presentation commit
  `ef7a4007` was selected: it ports the C# projection's observer-gated anchors,
  smoothed continuous fill geometry, stitched contours, fog mask, unexplored
  dimming and observer-visible claim outlines into a bounded native presentation
  cache. Core behavior and the Player17 schema/payload are unchanged.
- Projection now runs from an observer-filtered DTO on one Engine `JobSystem`
  worker. At most one request is admitted; newer input coalesces. Owner polling
  accepts only the matching generation, fingerprint and clear epoch. Current
  terminal errors report once without retry, and stale results/errors are dropped.
  Snapshot requests are capped at 2 Hz and 2,500 systems, 4,096 visible anchors,
  4,096 claims and 256-byte names. The fill atlas plus fog remains below 2.1 MiB.
- Synchronous cold projection measured 179/347/591/3,915 ms for workloads
  500 systems/3 empires/6 colonies, 500/3/100, 2,500/6/30 and 2,500/6/500.
  Corresponding final owner requests were 0.0937/0.0948/0.4770/0.4718 ms;
  unchanged polls were 0.0432/0.0889/0.4609/0.4915 ms; scheduling-inclusive
  worker completion was 392/479/609/4,985 ms. This is responsiveness/latency
  evidence, not a sustained FPS result.
- Earlier territory validation passed a full-native MSVC build. Five focused CTests passed in 1.79 seconds
  (`native_diplomacy_controller`, `native_diplomacy_workspace`,
  `native_galaxy_backdrop`, `native_galaxy_star_markers`,
  `native_territory_projection`). All 95 focused checks passed in 5.778 seconds
  across
  `test_native_diplomacy_runtime`, `test_native_galaxy_runtime` and
  `test_native_client_runtime`; these include 26 diplomacy checks and two
  duplicated-map sidecar rejection cases. Logs:
  `work/native-territory-visibility-runtime.log` and
  `work/native-territory-final-python.log`. Published head `c4744e1c` passed CI
  run `34978964654`; the updated
  GPU-free job compiles the full native app plus territory, star-marker and
  diplomacy-workspace targets. Prior head `43622e72` passed CI `34985891256`;
  that result predates the galaxy-label target.
- A closed RELATIONS workspace had retained its view and rendered hidden portrait
  work. Close intentionally retains its view for reopening; visibility-gated
  rendering skips all hidden draw/provider work, and regression coverage verifies
  a clean reopen. Relations Escape/Close
  also cancels the stale map gesture, so the next wheel input reaches map zoom.
- Two repaired Vulkan diplomacy runs passed at acceptance 720p and paused reload
  1080p. Both inspected map BMPs show the rounded cyan home border, purple dashed
  foreign claim, original nebula and no hidden RELATIONS panel. Each reports one
  region/claim, 14 contour and 36 claim draws, one fill atlas, one fog image,
  1,998,656 cached bytes and 19 unknown systems. Acceptance preserved unrelated
  state and claims; paused reload matched the whole Player17 payload and the source
  fixture hash remained unchanged. Evidence:
  `work/native-territory-diplomacy-evidence.json`.
- The `43622e72` predecessor found faint regional fill and overlapping nearby labels.
  The current readability checkpoint above addresses those findings; the candidate
  remains unmerged and unreleased.
- Orbital commit `eb1ab876` was not imported. Its project/state projection follows
  the reference schematic, but its most-populous-colony host and screen position
  are invented presentation values, not persisted physical locations. Input
  overlap, synchronous rasterization and cache/thread issues also need redesign.
  Its geometry is suitable only for an explicitly labeled construction preview
  until Core/save data defines physical orbital sites.

## Subsystem matrix

| Subsystem | C# source | C++ target | Build | Tests | Parity | Notes |
|---|---|---|---|---|---|---|
| Core primitives / deterministic utilities | Simulation/* | `core/legacy_random`, `interstellar_distance`, `atomic_file_write` | OK | parity tests | PARITY VERIFIED | Seeded RNG preserved |
| Galaxy generation | GalaxyGenerator | `core/galaxy_catalog`, `galaxy_generation_metadata` | OK | `galaxy_catalog_parity` | PARITY VERIFIED | Up to 5000-system benchmarks in export |
| Stars / star systems | StarSystemState, NearbyStarCatalog | `core/galaxy_catalog` | OK | `sol_catalog`, `galaxy_catalog_parity` | PARITY VERIFIED | Hidden core preserved |
| Planetary bodies | PlanetaryBodyGenerator/State | `core/planetary_catalog`, `planetary_body_persistence` | OK | `planetary_body_persistence_parity` | PARITY VERIFIED | Physical model, not enum-only |
| Species | Species* (~15 files) | `core/species_environment` + catalog seeding | OK | `species_parity`, `colony_biology_parity` | PARITY VERIFIED | Species-relative habitability preserved |
| Civilizations | Civilization* | `core/civilization_catalog`, `fresh_campaign` | OK | `civilization_parity`, `fresh_campaign_parity` | PARITY VERIFIED | |
| Leadership | CivilizationLeadershipState | `core/campaign_foundation_persistence` | OK | persistence parity | PARITY VERIFIED | |
| Population / colonies | Colony*, Population* | `core/colony_*`, `colony_economy`, `colony_operations` | OK | `colony_economy_parity` etc. | PARITY VERIFIED | |
| Economy / resources | EconomySimulation, treasury | `core/campaign_economy`, `sovereign_currency`, `colony_economy` | OK | `campaign_economy_parity` | PARITY VERIFIED | |
| Logistics / freight | LogisticsFlowAllocator, FreightSimulation | `core/logistics_*`, `freight` | OK | `freight_parity`, `logistics_*_parity` | PARITY VERIFIED | |
| Construction | Construction* | `core/construction_*`, `surface_construction` | OK | `construction_*_parity` | PARITY VERIFIED | |
| Industry | IndustryAllocation | `core/industry_allocation` | OK | `industry_allocation_parity` | PARITY VERIFIED | |
| Ship design | ShipDesignRegistry | `core/ship_designs` | OK | `ship_designs_parity` | PARITY VERIFIED | |
| Shipbuilding | Shipbuilding*, Shipyard* | `core/shipbuilding`, `shipyard_*` | OK | `shipbuilding_parity`, `shipyard_*` | PARITY VERIFIED | |
| Legacy research | Technology* | `core/legacy_research`, `legacy_technology` | OK | `legacy_*_parity` | PARITY VERIFIED | Superseded path, kept for saves |
| Adaptive Research | AdaptiveResearch* (~40 files) | `core/adaptive_research_*` (~25 modules) | OK | 10+ adaptive parity tests | PARITY VERIFIED | Authoritative research; integrated host |
| Diplomacy (simulation) | Diplomacy*, Diplomatic* | `core/diplomacy_*` | OK | diplomacy parity/persistence tests | PARITY VERIFIED | Observer-safe commands preserved |
| Diplomacy (presentation) | DiplomacyRelationsPresenter, ObserverDiplomacyCommandService | `native_diplomacy_controller`, `native_diplomacy_workspace` | OK | focused C++/Python checks + Vulkan smoke | PARTIAL | Observer-safe RELATIONS workspace covers the reference sections; strategic claims are map presentation |
| Territory / exploration | Exploration*, StrategicTerritoryProjection | `core/exploration_*`, `survey_operations`, `knowledge`, `native_territory_projection`, `native_territory_overlay` | OK | exploration/knowledge parity + focused territory checks | PARTIAL | Survey secrecy preserved; reference-shaped native overlay has Vulkan and bounded-worker evidence; broad-hardware FPS remains unproven |
| Strategic AI | CivilizationStrategic* | `core/strategic_*` (6 modules) | OK | `strategic_*_parity` | PARITY VERIFIED | Scheduled reviews, bounded work |
| Fleets | Fleet*, FleetTransit | `core/fleet_*`, `fleet_transit`, `fleet_reach` | OK | `fleet_*_parity` | PARITY VERIFIED | `design_id` now in native presentation |
| Combat | Combat*, MassiveCombat* | `core/combat_*`, `massive_combat_*`, `campaign_massive_combat` | OK | `combat_*_parity`, `massive_combat_*` | PARITY VERIFIED | Engine resolution; native battle view not started |
| Events | none in C# | none | — | — | N/A | No event subsystem exists in reference |
| Save/Load | Game/Persistence | `core/player_campaign_*`, `galaxy_payload_*`, `*_persistence` | OK | `player_campaign_*` parity + reload validators | PARITY VERIFIED | Player17 format; paused reload equality |
| Time simulation | SimulationClock, GalaxySimulationStepCoordinator | `core/campaign_frame`, `strategic_clock`, `campaign_coordinator` | OK | `campaign_frame_parity`, `strategic_clock_parity` | PARITY VERIFIED | Deterministic stepping |
| UI (native) | Main.*, panels | `app/native_client/*_workspace` (16+ modules) | OK | workspace + input tests + smoke validators | PARTIAL | Fleet/shipyard/research/construction/colony/surface/settlement/system/startup/diplomacy workspaces done |
| Rendering (native) | Main.VisualMap, renderers | `engine/native_map_platform`, `app/native_client` scene | OK | Vulkan smoke + capture validators | PARTIAL | Galaxy art, star markers, ships, route effects, textured surface ground, batched top-down surface site geometry and selected territory overlay; validated top-down surface scene; 3D and physical orbital sites deferred |
| Audio | AudioDirector | `engine/native_audio`, `app/native_client/native_audio_director` | OK | audio/director CTest + relocated audio startup/reload | PARTIAL | Score, settings, completion cues and bounded fixed scientist speech; broader casting/device recovery remain |
| Input | Main.PlayerCommands, input actions | `native_client_input`, `map_interaction` | OK | input tests | PARTIAL | Map/fleet/confirm flows done |
| Assets | asset library | `assets/` + exact-hash declarations | OK | packaging rejection tests | PARITY VERIFIED | Explicit reviewed manifests only |
| Voice | Main.Voice*, CharacterVoiceResolver | `native_audio_director` / fixed PCM cues | OK | finite channel/director checks + connected-lane smoke | PARTIAL | Three dry British human scientist lines. Legacy optional TTS is C#/Godot; no native dynamic TTS or alien casting yet |
| Packaging | export tooling | `tools/stellar-export`, `export/*.json`, `cmake/Native*` | OK | sealed export validators | PARITY VERIFIED | No Godot/.NET/compiler at runtime |

## What blocks "fully playable native"

1. Surface construction now has textured ground with bounded asynchronous loading and a typed, batched top-down scene: building-family geometry, rotations, hollow selection, powered/staffed status, bounded cosmetic roads, route caching, local colony framing and clamped panning. Its campaign and gallery checks pass; this remains presentation-only 2D artwork, not manual roads or 3D.
2. Physical orbital sites need an authoritative Core/save host and location contract. A labeled construction schematic can be ported without claiming physical placement.
3. The human scientist now has three fixed British cues, and owned simulation events produce bounded notices/sounds. Full character/species casting, dynamic speech and playback-device recovery remain.
4. The territory overlay has Vulkan/runtime and bounded-worker evidence above; broader hardware performance and complete graphical parity remain unproven.
5. Background artwork reduces first-scene CPU work to about 3 ms in both Sol and the galaxy; regional scenery transitions now cost about 1 ms. Cold profiling locates the ~67 ms tail inside presentation, even with no image uploads; its precise driver/display cause remains unproven. A developed 500-system campaign with 24 paid ships and nine total colonies averages ~16.7 ms on this host at 8X, including manual saves and exact paused reloads. Combat, much larger fleets and broad-hardware 60 FPS remain unproven.
6. `graphicalParity=false` retained honestly; `cleanMachineTest` needs a separate machine/VM.

## Upstream evidence (engine 0.1.58, Devin branch `cpp/devin-swe2-native-conversion`)

Latest Codex checkpoint fixes a real developed-campaign save failure: Player17
research outcomes were written as string enum names but its strict reader expects
integers. The writer now uses typed values at the seven affected outcome, tacit
asset and foreign-assessment fields. The reader, standalone research codecs and
simulation rules are unchanged. Both failed-hypothesis and successful progression
tests now restore and recapture the complete payload with no fields omitted.

The optional developed fixture pays for 24 ships through ordinary research and
shipyard commands, then issues real routes on day 10154. Four actual Vulkan
active/paused runs and two default system runs passed at 720p/1080p. All 24 fleets
move in each measured interval; they have arrived by the final 1080p save. Interval
means are 16.713/16.714 ms; p95 17.401/17.604 ms. Eight focused CTests and 17 Python
checks pass. See NATIVE_FRESH_PROGRESSION.md and NATIVE_CLIENT_VALIDATION.md for
offline preparation, full workload, source hash and timing limits. Previous head
`a56351f8` passed native CI `34960835811`, covering this repair and developed-fleet checkpoint. Audio foundation `6e44bb40` subsequently passed native CI `34968941076`, including both dedicated audio targets. Settings head `de65ce7b` passed `34972936916`. Scientist/feedback head `2d21d91c` has its own run, `34976618037`; its result must be checked independently of later surface work.

The preceding Codex checkpoint adds a separate opt-in running-campaign profiler. The
canonical 500-system Player campaign runs 600 measured frames at 8X using real
elapsed time and UI speed/resume/pause input, with a manual save during play.
It proves time advances after that save completes, pauses for the final save,
and checks the whole Player17 payload through an independent paused reload.
Actual 720p/1080p runs advanced day 0 to 80.2483424 to 160.514568; interval means
were 16.718/16.722 ms, p95 16.917/17.113 ms, update maxima 5.941/4.434 ms.
Strict native build, four focused CTests, 88 Python checks, four rejected CLI
cases and six Vulkan launches passed. This is early-campaign evidence, not a
busy-fleet benchmark or a universal 60 FPS claim. Previous cold diagnostic head
`86445dde` passed native CI `34952179710`; the new checkpoint needs its own run.

The preceding Codex checkpoint adds an opt-in ten-frame cold profile to the existing
bounded map profiler. The strict native build, 57 Python checks, four 600-frame
Vulkan map profiles and two default diplomacy launches passed. Frame 7 spends
65.471–66.460 ms inside presentation; Sol's submission takes 0.236/0.258 ms and
uploads no images on that frame. Exact paused save/reload and finished-art gates
remain intact. This is diagnostic evidence, not a fixed stall or GPU-only timing.
No normal-play histories or Engine rendering changes were added. See the handoff
for the next active-campaign performance work and the discarded queue experiment.

Committed galaxy checkpoint `63d62d31` prepares deep-field, galaxy and regional scenery on
the existing bounded Engine image worker. Each decoded scenery image is limited
to 8 MiB, with three cache slots; missing/oversized sources retain their path/cause.
Central fog remains visible while scenery prepares, navigation stays responsive,
and capture gates wait for finished imagery. Three focused CTests, 46 Python
checks and six actual Vulkan galaxy/system/diplomacy launches passed. Galaxy cold
scene CPU time fell from 45.432/46.248 ms to 3.163/2.695 ms at 720p/1080p;
regional transition maxima fell from 19.779/19.577 ms to 1.027/0.833 ms. All four
finished galaxy overview/regional BMP captures are byte-identical to the previous
build. This checkpoint requires its own CI; previous system-art head `96b83092`
passed native CI `34946960470`. See the handoff and validation document for limits.

The preceding checkpoint prepares system stars, rings and planet discs on one
existing Engine JobSystem worker, using copied observer-safe appearances.
Admission is bounded to 16 outstanding jobs / 32 MiB reserved output; cache
budgets remain unchanged. Generation changes cancel obsolete requests without
waiting, completed results drain on the owner thread, and preparation errors
retain their cause/path. No simulation, GPU or window access moves to workers.
Six focused CTests (including exact synchronous/background pixel comparisons),
a subsequent workspace readiness/input regression, 42 Python export checks and
seven actual Vulkan runs passed. Four 600-frame profiles at 720p/1080p measured
Sol first-scene CPU time 2.936/2.807 ms versus 211.452/211.567 ms; final images
arrive asynchronously in ~213–258 ms. Screenshot gates wait for real artwork.
Paused-map interval means 16.717–16.722 ms and p95 16.913–17.026 ms preserve warm
pacing; see the handoff/validation document for remaining stalls and evidence.
Profiling head `b491783c` passed native CI `34942125331`; system-art head
`96b83092` passed `34946960470`. Engine remains 0.1.58 candidate.

- 150/150 graphical CTest (incl. `native_diplomacy_controller`, `native_diplomacy_workspace`),
  144/144 headless CTest baseline, 61 Python export checks.
- Diplomacy presentation: `native_diplomacy_controller` ports `DiplomacyRelationsPresenter`
  over `DiplomaticStateView` only — unidentified contacts carry no civ id/name/species/
  metrics. Revision+signature stale-command guard covers relationship drift and
  identification changes. `native_diplomacy_workspace` provides the RELATIONS workspace:
  filterable contact directory, species transmission portrait or signal waveform, five
  relationship meters, negotiation/war confirmation modals, agreements/proposals/history/
  intelligence/overview tabs, proposal accept/reject/withdraw buttons, FocusSystem jump.
- Ship artwork: six approved images, bounded 6-entry/4 MiB cache, 224px thumbnails,
  design_id→role resolution, fleet+shipyard rows/details, Vulkan-validated.
- Fleet route effects: dashes, chevrons, trails for active owned fleets matching the
  Godot map; unsurveyed-leg drawing is player-authorized own-fleet data.
- No merge to integration/main; PR #332 is the candidate under review.

## Codex candidate checkpoint (PR #332)

- Cold celestial generation now skips surface calculations that cannot contribute
  to halo pixels, covered corona work and the transparent outer boundary. Exact
  before/after comparison preserves every byte of eight RGBA resources (19.4 MB).
  Star generation improved from ~210 to ~80 ms; full first system scene from
  ~340–346 to ~210–213 ms. Five focused CTests and four final Vulkan system/galaxy
  launches passed. Resource resolution, artwork, colors, flare behavior and observer
  rules remain unchanged. Entry stalls and sustained 60 FPS remain unresolved;
  see the cold celestial checkpoint in the handoff for evidence and next steps.
- Integrated Devin's diplomacy code `410753da` as `c9338699` and reviewed the real campaign path. Contact selection now refreshes immediately, unknown contacts retain stable selection, confirmation commands use their original generation/revision, and proposal terms/precise relationship changes invalidate stale commands. Scroll clipping, intelligence layout, and 720p filter labels were repaired.
- Communications-v2 PNGs now load through canonical species IDs and are explicitly packaged. Four lazy entries are capped at 32 MiB; the actual decoded artwork occupies 24 MiB. Missing declared artwork fails with its path/cause; unknown species retain the signal fallback.
- Final combined validation passed seven focused CTests, 18 diplomacy-validator Python checks, 45 package/checkout Python checks, and six actual Vulkan diplomacy/system/galaxy launches at 720p/1080p. Diplomacy acceptance, the visible new agreement, four known/unknown captures, unrelated-state preservation, and exact paused Player17 reload are covered by the maintained exporter. See `native-diplomacy-final.log` and `work/native-diplomacy-final-{diplomacy,system,galaxy}.json`.
- Final diplomacy frame means were 17.790–17.791 ms and p95 28.367–32.663 ms; system/galaxy means 20.748–21.215 ms and p95 33.458–33.937 ms. This is not a sustained 60 FPS result. Full graphical parity, audio, orbital structures and final surface scenery remain outstanding.
- Fresh-checkout byte stability is covered for reviewed native assets; soft-circle submission now preserves the legacy 20-segment pixels and ordered blending without per-circle heap allocation or trigonometry.
- Native system framing now uses measured labels, body/stellar/orbital envelopes and a 12px presentation inset. Selected labels take priority; lower-priority labels hide rather than overlap. Initial travel activation fits visible exits once, while later refreshes preserve pan/zoom.
- Final evidence: five focused CTests (`native_system_travel`, `native_system_workspace`, `native_system_view`, `native_system_colony_entry`, `native_settlement_workspace`) and seven actual Vulkan galaxy/system/travel launches passed established validators, including exact paused Player17 reload and observer secrecy. See `native-system-layout-tests.log`, `native-system-checkpoint.log`, `work/layout-{galaxy,system,travel}.json`, and `build-native/preview-*.bmp`.
- Smoke-only timing records bounded update/scene/render-present means, p95 and maxima with frame indices. Separate cold-entry, save-service, capture/transition and steady buckets expose loading/save spikes. Render-present includes VSync wait; this is not a GPU-only measurement or a 60 FPS claim. Cold artwork preparation and presentation pacing remain to investigate.
- Async manual-save evidence now covers cold, 61-save-service, capture-transition and steady buckets. System manual-save update fell from 130.901/130.302 ms to 7.272/2.610 ms; galaxy from 130.953/129.773 ms to 5.080/2.955 ms; travel from 32.392–35.940 ms to 1.008–1.157 ms. Cold system scene remains ~340–346 ms; VSync p95 remains ~33 ms. See `work/save-baseline-{system,galaxy,travel}.json`, `work/layout-{system,galaxy,travel}.json`, and `native-save-background-runtime.log`.
- Native manual saving now uses Core's `begin_manual` admission and immutable single-writer persistence. Queued clicks coalesce; failure stays visible before an explicit retry; load/exit drains remain ordered. Nine actual Vulkan map/transit/diplomacy launches passed durable-save, observer and paused-reload validation. See the async-save handoff for interfaces and focused native checks.
- The preceding layout/timing head `66c56b897ad04a99d2e662a2fb089cdcc160186b` passed native workflow `34929897092`; diplomacy head `0e0835ae` passed `34934085259`. The subsequent async-save candidate needs its own CI. Engine 0.1.58 is inherited from Devin's candidate; no new sealed release, shared merge, full-suite or clean-machine claim is made.
