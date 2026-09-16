# C++ migration handoff

Concise cross-agent notes. Full milestone history lives in `docs/engine/MIGRATION_STATUS.md`;
subsystem state lives in `docs/CPP_MIGRATION_STATUS.md`.

## Current integration checkpoint (2026-09-16)

### Strategic fleet orders and Locate

Native fleet controls selectively adapt Devin `e8672801`: owned armed fleets
receive Hold, Defend and Retreat controls with current order and pre-action hover
explanations. Civilian and military Locate centers the owned fleet without changing
zoom or issuing an order. Civilian recovery and travel confirmation remain intact.
Distinct action hit regions, press/release matching, focus/menu cancellation and
720p detail/art bounds prevent input leaks and clipped information.

Core remains authoritative. Controller-held single-use tokens bind campaign,
observer, selection, mission, route/phase and combat decision state. Stale, replayed,
foreign, inactive/duplicate, unarmed and tactical-conflicting orders are rejected.
Ordinary travel progress does not cancel a valid click; Hold does not stop travel,
and Retreat does not route home. No Core, C# or Player17 schema changes. Contract:
`docs/engine/NATIVE_MILITARY_ORDERS.md`.

Validation: MSVC native build; 15 affected CTests (fleet UI retested after correcting
civilian Locate hit resolution); Python export discovery 468 tests, 451 passed /
17 optional STELLAR_NATIVE_EXE tests skipped. Two relocated Vulkan military runs at
720p/1080p exercise actual app input for all orders and Locate. Whole canonical
Player17 comparison permits only the selected owned ship's final Defend order and
defended-system field to change. Normal saving and a second process reload preserve
the whole payload except timestamp. Eight captures, including order/Locate frames,
were produced; 720p/1080p inspected. Existing relocated fleet travel, tactical battle
and system-travel/reload suites also pass; civilian Locate proves no gameplay/zoom
change. Evidence: work/native-military-{build,ctest,python}.log,
work/native-military-final-ctest.log and native-military-{runtime,fleet,battle,travel}.json.

Next: review Devin `a0f3b102` BMP dimension validation against existing drawable/pixel
checks, then outstanding authored planet/star rendering against approved Sol/Earth
art and bounded resource contracts. Generated discs bc681eb6, fe364430/aabde2b1,
Land/Collect d9d23f57 and colony sites e49f4df0 remain unimported. Latest inventoried
Devin head e8672801. Shared base ac45d958 and PR332 remain unmerged. The local tested
package is UNSEALED and not a release download; full 3D visuals and sustained
performance remain unfinished.

### Previous checkpoint: Sovereign Treasury and industry priorities

The native C++ economy workspace selectively adapts Devin `810d2a6d`. Six compact
metrics show sovereign reserves, income, recurring expense, net income, stored
materials and material production. Two income sources and all seven expense
categories (including research operations and separately reserved research funds)
remain reachable at 720p. Priority controls explain canonical allocation weights
before selection, are free/reversible, and use Core's existing command and save
field. The rail has a distinct economy icon. No Core, C# or Player17 schema changes.

Projection validates unique actual player/home/economy/construction records;
missing data and failures are explicit with Retry and Support diagnostics. Invalid
numeric values fail cleanly. Failed queries latch until Retry; paused frames cache,
active refresh requires changed simulation time and a one-second interval. Commands
revalidate campaign, revision, observer and current priority and consume their
revision. A same-choice command cannot leave the controller permanently locked.
Material rates are not formatted as currency. Controls and explanatory feedback
stay pinned; measured content scrolls without moving the map. Contract:
`docs/engine/NATIVE_ECONOMY.md`.

Validation: MSVC native build, 14 affected CTests, and Python export discovery
463 tests (446 passed / 17 optional STELLAR_NATIVE_EXE tests skipped). Two relocated
Vulkan economy runs at 720p/1080p exercise normal navigation, scrolling, all expense
rows, refresh, competing workspace isolation, priority changes and normal saving.
The complete before/after canonical Player17 permits only the actual player's
industry priority to change; reload preserves the whole saved payload except its
timestamp. Eight captures produced, including summary/end/priority screens;
720p/1080p screenshots reviewed. Existing relocated Supply, navigation and colony
regression suites also pass. Evidence: work/native-economy-{build,ctest,python}.log,
work/native-economy-{runtime,supply,navigation,colony}.json and
work/native-audio-validation/package-economy-*.bmp.

Next: review Devin `e8672801` armed-fleet strategic orders/Locate against existing
observer, command-authority, travel and tactical encounter contracts. `a0f3b102`
adds BMP dimension validation; review without weakening drawable/pixel gates.
Latest inventoried Devin head remains e8672801. Generated discs bc681eb6,
fe364430/aabde2b1, Land/Collect d9d23f57 and colony sites e49f4df0 remain unimported.
Keep approved Sol/Earth art, recorded UK scientist cues and existing save contracts.
Shared base ac45d958 and PR332 remain unmerged; this validation package is UNSEALED,
not a release download. Full 3D visuals and sustained performance remain unfinished.

### Previous checkpoint: owned surface building management

The native surface inspector now supports building upgrades, repair,
shutdown/restart, operating priority and colony-hub upgrades. Each action opens
a paused review showing canonical sovereign authorization, industry costs and
consequences. Upgrades use Core timers; repair explicitly reports Core's current
immediate restoration. Selection survives confirmation so results remain visible.
Measured/clipped inspector rows scroll independently of pinned controls.

This selectively adapts Devin 1c8e9744. Preview runs actual Core commands on copied
world vectors without mutation; confirmation checks an owned single-use quote,
all displayed fields, actual player/knowledge/colony/body/site membership,
target state and a fresh Core assessment. Missing/moved planetary bodies, changed
ownership/funds/prerequisites and stale/replayed/tampered quotes are rejected.
No authoritative Core rule, Player17 schema or C# changes. Contract:
docs/engine/NATIVE_SURFACE_MANAGEMENT.md.

Validation: final MSVC build and all 15 surface/colony CTests pass. Python export
discovery: 458 tests, 441 passed / 17 optional STELLAR_NATIVE_EXE tests skipped.
Six relocated Vulkan surface launches cover fresh generation, placement/cancel,
paused reload, populated 720p/1080p and reduced-workforce 720p. Management uses
actual app input routing for cost review, cancel, operation reversal and priority
reversal. Complete paused Player17 equality holds except SavedAtUtc; retained
art/focus/status/pixel checks pass. Six new review/result captures were produced,
with 720p/1080p visually inspected. Existing relocated colony/navigation checks
also pass. Paid upgrade/hub/repair are controller-test evidence; no claim of a
full graphical paid-action playthrough. Evidence:
work/native-management-{build,ctest,python}.log,
work/native-management-{runtime,colony,navigation}.json and
work/native-audio-validation/package-surface-*-management-*.bmp.

Next: review/adapt Devin 810d2a6d Economy panel using observer-safe canonical
projections, clear income/upkeep/shortage breakdowns, bounded refresh and measured
720p layout. Latest fetched Devin head e8672801 also adds armed-fleet orders and
Locate; a0f3b102 checks BMP drawable dimensions. Generated discs bc681eb6,
fe364430/aabde2b1, mission Land/Collect d9d23f57 and colony sites e49f4df0 remain
unimported. Preserve approved Sol textures, recorded UK scientist cues and
existing save/authority contracts. Shared base ac45d958 and PR332 remain
unmerged; the test package is UNSEALED. Full 3D visuals and sustained performance
remain unfinished.

### Previous checkpoint: surveyed planet inspection and Focus Planet

The native orbital inspector now groups metric physical facts, environment,
and known moons/signals into measured label/value rows. Header, survey status,
Focus Planet and the separately authorized Open Colony action stay pinned.
Bounded scrolling keeps all details reachable at 720p without moving the map;
1080p fits Earth's complete current facts. Focus centers the selected body at
unchanged zoom, including bodies without a colony. No ship order is issued.

This selectively adapts Devin 641955f0. Exact radius/eccentricity/inclination
remain unconfirmed during reconnaissance despite approximate geometry in the
safe drawing snapshot. Full survey plus details gates all exact physical and
environment values; nonfinite/invalid/overflow readings are unconfirmed. Parent
and moon membership use only the safe snapshot. Refresh redacts values, removed
selection clears, observer change closes, and campaign discard clears. Measured
UTF-8 layout caches until facts/viewport/measurer change, with a bounded header
for long names. No raw foreign colony telemetry, Core, Player17 or C# changes.

Validation: final MSVC native build and all 11 affected CTests pass. Python
export discovery: 456 tests, 439 passed / 17 optional STELLAR_NATIVE_EXE cases
skipped. Final relocated Vulkan 720p fresh / 1080p reload exercise actual mouse
routing, physical/environment rows, Focus, scroll end/reset and camera isolation.
Complete paused Player17 equality holds except SavedAtUtc. Screenshots inspected.
Existing relocated navigation, colony and local system-travel suites also pass.
Contract: docs/engine/NATIVE_BODY_INSPECTION.md. Evidence:
work/native-body-{build,ctest,python}.log and
work/native-body-{runtime,navigation,colony,travel}.json;
package-system-*.bmp under work/native-audio-validation. Package remains UNSEALED.

### Previous checkpoint: native home-system supply workspace

The Supply rail icon now opens an organized, read-only home-system network:
available surplus, import demand, delivered supply, shortfall, and every owned
location with facility kind/status/daily values. Tiny positive values show <0.01.
This selectively adapts Devin ea9164e9 without swallowed-initializing errors,
duplicate Core projection or an eight-row cutoff. Actual player/home identity and
required records are validated. Failure clears stale values, provides Retry,
logs the exception diagnostic, and latches until explicit retry or new identity.
One Core home_system_logistics call per admitted refresh; no rules/schema/C# edits.

The measured, clipped table caches detached index-based geometry. Unchanged
frames do not measure rows again; names, viewport and measurer changes invalidate
it. Pinned controls and bounded wheel/drag capture prevent map/fleet input leaks.
Automatic queries require changed simulation time and at least one second while
visible; paused frames use cached results. Navigation/battle/replacement close
or clear the panel appropriately. Contract: docs/engine/NATIVE_SUPPLY_NETWORK.md.

Validation: final MSVC build and all 14 targeted CTests pass, including two Core
logistics parity suites. Export Python discovery: 455 tests, 438 passed / 17
optional STELLAR_NATIVE_EXE cases skipped; final strict supply-proof tests pass.
Relocated Vulkan 720p fresh / 1080p reload verify actual navigation, canonical
totals, projection counts, paused caching, bounded scroll/drag, Refresh, Research
switching, Close and complete unchanged Player17 state except SavedAtUtc.
Final screenshots inspected. Existing relocated navigation and fleet suites pass.
Evidence: work/native-supply-{build,ctest,python}.log and
work/native-supply-{runtime,navigation,fleet}.json; captures under
work/native-audio-validation/package-supply-*.bmp. Validation package is UNSEALED.
It reports the existing Core shortage; no synthetic surplus conceals it.

The subsequent planet-inspection milestone above supersedes this checkpoint's
641955f0 follow-up. Surface management remains the next command review.

### Previous checkpoint: observer-safe galaxy system inspection

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

The subsequent supply milestone above supersedes the supply follow-up from this checkpoint.

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

### Strategic keyboard integration

Space, keys 1 through 4 and F6 now follow the legacy strategic pause/speed/save
commands. They route before system-view capture and respect search, menu,
settings, surface, Relations, settlement, production cancellation and fleet
preview ownership. Canonical clock/save behavior is unchanged. This selectively
adapts Devin `749ad8e7`; its tactical keyboard portion remains unimported.

MSVC builds and four focused CTests plus six Python navigation checks pass.
Two relocated Vulkan navigation replays exercise galaxy/system shortcuts,
four actual blocking contexts, text entry/correction and an F6-only save;
full canonical state remains unchanged and the saved reload differs only in
`SavedAtUtc`. Two neighboring research runs pass. Exact evidence and limits:
`engine/NATIVE_NAVIGATION.md`, `work/native-keyboard-runtime.json` and
`work/native-keyboard-research-runtime.json`. Package remains unsealed.

### Compact native navigation and integration review

Research, Shipyard, Construction and Relations now use the approved icons in
a compact left rail. All seven affected workspaces reserve its gutter and
main input routing owns navigation before workspace input. Menus and modals
retain ownership. Keep the layout/render/input changes together when adapting
Devin's workspace ports. `engine/NATIVE_NAVIGATION.md` contains the precise
contract, reproduction and validation evidence; artwork provenance and
regeneration are in `engine/NATIVE_NAVIGATION_ART_SOURCES.md`.

The prior research inspector repair remains integrated, including measured
wrapping, bounded scrolling, pinned actions and stable position during live
updates. See `engine/NATIVE_RESEARCH.md` for that checkpoint's evidence.

Devin is reviewed through `e390b659`. Tactical `357872e8` and surface sprites
`58aaf475` are not imported; their blocking source reviews remain in the
corresponding Engine review documents. Later audio settings duplicate the
validated NativeAudioSettings/NativeAudioDirector implementation. Later
cpu/draw timing duplicates finer bounded diagnostics, retains normal-play
history without a limit and cannot identify GPU-only timing. Issue #324 comment
`5685997691` records this review and asks for corrected tactical work/uncovered
ports instead. Keep Core's combat authority and avoid a competing bulk port.
PR #332 remains unmerged and unsealed.

Latest fetched Devin head is `7a85a099`, adding keyboard shortcut slice
`749ad8e7` on top of `e390b659`. Its strategic input has now been selectively
adapted as described above. It does not include the requested
tactical render/group/gesture/persistence corrections.

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

### Native top-down surface checkpoint (validated)

Three actual Vulkan launches passed on the unaltered fresh 500-system campaign;
ordered 720p and paused reload 1080p evidence is in
`work/native-surface-scene-evidence.json`. Counters are `1/13/142/1` ordered
and `1/14/174/1` reload for sites/meshes/triangles/roads. The presentation-only
14-type gallery passed at both resolutions with 20 meshes, 1,302 triangles
and 14 roads. Six unique CTests and 121 Python checks pass; no manual roads,
full 3D, finished-art, FPS or sealed-release claim is made.

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

### Historical readability checkpoint (2026-09-15)

This native C++ checkpoint builds the full MSVC native app, including the galaxy-label
target with `/W4 /WX`. Six focused CTests passed in 1.67 seconds:
`native_diplomacy_controller`, `native_diplomacy_workspace`,
`native_galaxy_backdrop`, `native_galaxy_star_markers`, `native_galaxy_labels`, and
`native_territory_projection` (`work/native-label-runtime.log`). Ninety-seven Python
checks passed in 6.183 seconds across `test_native_diplomacy_runtime`,
`test_native_galaxy_runtime`, and `test_native_client_runtime`
(`work/native-label-python.log`).

All four relocated real-Vulkan runs passed: fresh and paused-reload galaxy captures
at 500 systems/720p and 1080p, plus diplomacy acceptance and paused reload at 720p
and 1080p. Evidence is in `work/native-label-galaxy-evidence.json` and
`work/native-label-diplomacy-evidence.json`. Acceptance preserves unrelated state
and claims; paused reload preserves the complete Player17 payload; observer secrecy
and the source-fixture hash remain unchanged. The overview has 500 canonical markers
and zero label audit counts; this does not mean zero markers. Each regional capture
reports 6 candidates, 6 measured, 3 placed, and zero collision/offscreen audit counts.
The root captures were inspected at overview 1080p, regional 720p/1080p, and diplomacy
720p: Sol and empire names are visible, fill is stronger, names do not stack, and the
720p empire label has a leader line outside its territory border.

Labels use actual render-font measurements and filter invalid/offscreen candidates
before deterministic sorting. Placements are collision-tested after measurement.
Priority is selected system, then empire, then nearby known stars. Empire labels test
48 fixed, size-aware positions nearest first, retain their anchor leader line when the
gap exceeds 32px, and check HUD, stars, the fleet panel, other labels, and viewport
bounds. The 128-measurement budget includes at most 32 empires. A 2,500-candidate
dense test proves that measurement budget only; it is not a claim
of 2,500 GPU labels or 60 FPS. Fill uses multiplier `0.16` and contour `0.72`.
Some known names are intentionally omitted where no valid placement exists. Selected
priority is unit-covered only, not a selected-runtime-capture claim. This is
presentation work only: C++ Core, projection math, and Player17 remain unchanged.

`43622e72` is the prior readability checkpoint: it passed 95 Python checks and CI
`34985891256` for seven existing presentation targets before the galaxy-label target.
Its faint fill and overlapping-label findings were addressed by `884accb2`, which
passed CI `34992777279`. The 97 checks and label results belong to that readability
head, not this surface checkpoint. `graphicalParity=false`; Engine 0.1.58 candidate/game 0.1.7-alpha remains
unmerged and unsealed, with clean-machine and broad-hardware 60 FPS still unproven.

The selective Devin review through `891fbcb2`, including duplicate-audio commit
`dbf07f81`, was not imported because PR #332's audio is authoritative. Findings were
callback races, clip-tail looping, non-atomic lowercase preferences, and lifecycle
conflicts. Existing engine audio/settings and `bf_emma` voice remain authoritative;
issue #324 comment `5682904661` records the review. Orbital `eb1ab876` also remains
unimported pending an authoritative Core physical-body/system/orbit contract. Next:
wider casting and final graphical parity work.

- Devin/SWE-2 was reviewed through head `891fbcb2`. The selected change is
  territory presentation commit `ef7a4007`: a reference-shaped port of
  `StrategicTerritoryProjection` with observer-gated anchors, continuous clipped
  fills, stitched contours, fog, unexplored dimming and observer-visible claim
  outlines. It is a presentation cache only; Core rules and the Player17 save
  payload are unchanged.
- Territory preparation now accepts only an observer-filtered DTO on one Engine
  `JobSystem` worker. Owner-thread polling binds results to generation,
  fingerprint and an internal clear epoch. Admission is limited to one job and
  coalesces newer input; a current terminal error is reported once without retry,
  while stale errors/results are discarded. Snapshot requests are capped at 2 Hz;
  DTO limits are 2,500 systems, 4,096 visible anchors, 4,096 claims and 256-byte
  names. One fill atlas plus fog stays below 2.1 MiB and preserves the reference
  smoothing algorithm.
- Synchronous cold preparation measured 179/347/591/3,915 ms across 500 systems
  with 3 empires/6 colonies, 500/3/100, 2,500/6/30 and 2,500/6/500. Final owner
  request times were 0.0937/0.0948/0.4770/0.4718 ms; unchanged polls were
  0.0432/0.0889/0.4609/0.4915 ms. Scheduling-inclusive worker completion was
  392/479/609/4,985 ms. These isolate owner responsiveness and background latency;
  they are not sustained-frame-rate results.
- At the prior territory checkpoint, the full-native MSVC build passed and five
  focused CTests passed in 1.79 seconds:
  `native_diplomacy_controller`, `native_diplomacy_workspace`,
  `native_galaxy_backdrop`, `native_galaxy_star_markers` and
  `native_territory_projection`. All 95 focused Python checks passed in 5.778
  seconds across
  `test_native_diplomacy_runtime`, `test_native_galaxy_runtime` and
  `test_native_client_runtime`, including 26 diplomacy checks and two
  duplicated-map sidecar rejection cases. Logs are
  `work/native-territory-visibility-runtime.log` and
  `work/native-territory-final-python.log`. Published head `c4744e1c` passed CI
  run `34978964654`; the updated GPU-free job compiles the full native app plus
  territory, star-marker and diplomacy-workspace targets. New-head CI is pending.
- Review found that a closed RELATIONS workspace retained its view and `render`
  ignored visibility, allowing hidden drawing and portrait work. Close intentionally
  retains its view for reopening; visibility-gated rendering skips all hidden
  draw/provider work, and regression coverage proves a clean reopen. Main also cancels the stale map
  gesture on Relations Escape/Close, so the next real wheel event zooms immediately.
- Two repaired real-Vulkan diplomacy runs passed at acceptance 720p and paused
  reload 1080p. Both BMPs were inspected and show the map, rounded cyan home
  border, purple dashed foreign claim and original nebula art, with no hidden
  RELATIONS panel. Each reports one region/claim, 14 contour and 36 claim draws,
  one fill atlas, one fog image, 1,998,656 cached bytes and 19 unknown systems.
  Acceptance preserved the entire unrelated payload and claims; paused reload
  matched the whole Player17 payload, and the fixture hash stayed unchanged.
  Evidence: `work/native-territory-diplomacy-evidence.json`.
- The preceding `43622e72` readability checkpoint found faint regional fill and
  overlapping nearby labels. The current checkpoint above addresses those findings;
  it remains an unmerged local candidate, not a release.
- Orbital commit `eb1ab876` was reviewed but not imported. It reproduces the C#
  schematic project/state list, but infers a physical host from the most populous
  colony and invents screen-space placement without a Core/save location contract.
  The review also found marker/body/fleet hit conflicts, orbit/label overlap,
  synchronous rasterization on the UI path and weak cache/thread guards. Reuse of
  its geometry should be limited to an explicitly labeled construction preview;
  physical orbital sites remain deferred until their host/location is authoritative.

## Active branches

- `engine/stellar-engine-migration` — shared migration branch (head `ac45d958`, engine 0.1.57). Do not push directly; feed via reviewed PRs.
- `cpp/devin-swe2-native-conversion` — Devin/SWE-2 working branch; reviewed through
  `891fbcb2`. Territory commit `ef7a4007` was selected; orbital commit `eb1ab876`
  was rejected for this integration checkpoint. Its newer portrait resolution,
  bounded cache and asset declarations are already covered by this branch;
  diplomacy presentation at `410753da` was integrated here as `c9338699`.
- `cpp/codex-native-architecture-integration` — Codex architecture/integration branch, based on `ac45d958`. Carries Devin's existing coordination documents forward; changes go through a PR to the shared migration branch.
- `work/stellar-engine-editor` — separate WPF editor tool (`editor/` only, 2 commits, non-conflicting).
- `work/voice-engine-tts` — fully merged ancestor of migration head.

## Interfaces added in 0.1.58 (candidate for review)

- `app/native_client/native_diplomacy_controller.{hpp,cpp}` — `NativeDiplomacyController`:
  `build(frame, generation, contact_index)` returns `NativeDiplomacyView` (contacts,
  selected details, proposals, agreements, history + `diplomacy_revision`);
  `execute(frame, generation, revision, action, target, proposal_id)` revalidates the
  quoted revision against a fresh signature before calling
  `ObserverDiplomacyCommandService`. Signature covers contact awareness/identity,
  relationship metrics, access, agreements, proposals and event ids — selection moves
  never bump it. Review hardened the signature with length-prefixed strings, classic
  locale/max-digits precision, collection counts and full proposal/terms fields.
  Owner-thread pinned like other controllers.
- `app/native_client/native_diplomacy_workspace.{hpp,cpp}` — fullscreen RELATIONS
  workspace. `handle` emits `SelectContact`, `Action`, `ProposalAction`,
  `FocusSystem`, `Close`; `set_view` preserves selection by stable `contact_id`
  across contact reordering, including unidentified contacts. Main immediately
  re-projects `SelectContact`, even while paused.
- `DiplomacyWorkspaceCommand` now carries `campaign_generation` and
  `diplomacy_revision`. Action and confirmation commands retain the quote shown
  to the player; main passes these to `execute`, not the latest view revision.
  Refreshed generation/revision dismisses an obsolete modal with an explanation.
- `NativeUiLayout` gained `UiAction::Diplomacy` + `diplomacy` rect (top bar, RELATIONS).
- `NativeDiplomacyWorkspace::render` takes an optional `PortraitProvider`
  (`string_view relative_asset_path -> shared_ptr<const RgbaImage>`); `nullptr` draws
  the signal-waveform fallback. `main.cpp` resolves
  `assets/visual/species/<id>-communications-v2.png` (underscores→hyphens).
  The exact four reviewed PNGs are now declared/copied by CMake and the exporter.
  The lazy cache is limited to four entries/32 MiB (24 MiB actual decoded RGBA);
  declared art failures include the path and cause. Unknown species use waveform.
- Scroll drawing and hit rectangles are intersected with their visible region.
  Intelligence cards/focus controls use a non-overlapping stack. Filter widths
  give long labels enough room at 720p without shrinking their text.

## Interfaces added in 0.1.57 (candidate for review)

- `app/native_client/native_ship_art_assets.{hpp,cpp}` — `NativeShipArtAssets(asset_root)`:
  `image_for(optional<design_id>, FleetRole)` returns `shared_ptr<const RgbaImage>`,
  bounded 6 entries / 4 MiB, single decode per source, 224px box-average thumbnails.
  `ship_artwork_for()` maps design_id then falls back to role; unknown roles throw.
- `app/native_client/native_fleet_route_effects.{hpp,cpp}` — `append_fleet_route_effects(
  DrawList&, fleets, shipyard_view, systems_by_id, camera)` returns
  `FleetRouteEffectStats{routed_fleets, drawn_legs, chevron_segments, trail_strokes,
  position_circles}`. Pure draw-command appender, testable headless.
- `NativeOwnFleet` gained `std::optional<std::string> design_id` (presentation-only,
  copied from `FleetState.design_id` in `NativeFleetController::build`).

## Contract changes

- `NativeFleetWorkspace::render` and `NativeShipyardWorkspace::render` take an optional
  `const NativeShipArtAssets*` — pass `nullptr` for art-free rendering (existing tests do).
- Own-fleet routes draw through unsurveyed systems (matches `Main.VisualMap.cs`): own
  route geometry is player-authorized. Foreign fleet contacts remain unplaced;
  unsurveyed system labels remain redacted elsewhere.
- `copy_native_client_runtime` now requires `export/native-ship-art-assets.json` +
  the six declared files; mocks must stub them (see `test_native_client_runtime` setUp).

## Validated diplomacy integration

- Diplomacy smoke now covers 1280×720 and 1920×1080 Vulkan launches, contact
  selection, acceptance, scrolling, reload, communications PNG display, waveform
  fallback, and observer secrecy. Player17 starts with reciprocal known contacts;
  the isolated copy adds an unknown contact, a second known contact, and a pending
  incoming research-exchange proposal. Four communications PNGs use a bounded
  32 MiB cache (about 24 MiB decoded RGBA).
- `native_diplomacy_runtime.py` authors only an isolated test save. Baseline
  canonicalization follows `player_campaign_json_tests.cpp`: omit fixture-only
  `Control`, convert numeric X/Y to float32; contacts are authored in the native
  snapshot order. No general numeric tolerance or simulation changes are used.
  Only the intended proposal, new agreement, two events and counters may change.
  Reload compares the entire saved payload except `SavedAtUtc` exactly.
- Final combined check: seven CTests (diplomacy controller/workspace, native input,
  system workspace/travel, diplomacy observer-command parity, Player17 JSON parity),
  18 diplomacy-validator and 45 package/checkout Python tests, six actual Vulkan
  launches (diplomacy/system/galaxy, 720p and 1080p). Evidence:
  `native-diplomacy-final.log`, `work/native-diplomacy-final-{diplomacy,system,galaxy}.json`,
  and `build-native/preview-*.bmp`. Known/unknown portrait captures were inspected.
- Final diplomacy frame means 17.790–17.791 ms, p95 28.367–32.663 ms; system/galaxy
  means 20.748–21.215 ms, p95 33.458–33.937 ms. These short VSync-inclusive samples
  do not establish sustained 60 FPS. This is Engine 0.1.58 candidate validation,
  not a sealed release or clean-machine result.

## Async manual-save checkpoint

- `PlayerCampaignSaveController::begin_manual(runtime, options)` captures the immutable
  Player17 DTO on the simulation owner thread and submits encoding/atomic IO to the
  existing single writer. `nullopt` means admitted, not durably saved; an immediate
  capture/submission error is returned. `complete()` reports the actual outcome.
  Synchronous `save_manual()` remains for explicit durable shutdown/reference use.
- Native manual-save requests coalesce behind an active writer. Completion is polled
  through `advance()` or `service()` (also while minimized); a failed completion
  cancels queued save/exit requests so an immediate retry cannot conceal the error.
  Explicit load/exit still drain synchronously and in order. Existing autosave retry
  scheduling, backup protection, thread ownership and Player17 schema remain intact.
- Manual-save update maxima, before → after: system 130.901/130.302 → 7.272/2.610 ms;
  galaxy 130.953/129.773 → 5.080/2.955 ms; travel 32.392–35.940 → 1.008–1.157 ms.
  These are CPU update measurements. Cold system scene construction remains
  ~340–346 ms. Frame/presentation measurements include VSync and still show a
  ~33 ms p95; this does not establish sustained 60 FPS.
- Four focused save/session/JSON/recovery CTests and nine actual Vulkan launches passed: seven
  galaxy/system/travel and two diplomacy, covering 720p/1080p, ordered transit,
  observer secrecy, durable writes and exact paused reload. Native test details
  are recorded in `native-save-background-tests.log`.
  Other evidence: `native-save-background-{runtime,diplomacy}.log`,
  `work/save-baseline-{system,galaxy,travel}.json`,
  `work/layout-{system,galaxy,travel}.json`, `work/save-background-diplomacy.json`.
- Diplomacy head `0e0835ae104be5582cae1061a36bc96e04f77286` passed native CI
  `34934085259`. The subsequent save candidate needs its own exact-head CI.

## Cold celestial preparation checkpoint

- `native_celestial_appearance.cpp` no longer evaluates sunspots/granulation for
  halo pixels with zero photosphere coverage. It also skips corona math beneath
  the fully opaque disc and outside the corona's zero-coverage boundary. Original
  surface/limb/corona equations, 1024px resources, intermittent flares, cache limits,
  spectral colors and blend order remain unchanged.
- Existing repository tests now accept diagnostic captures:
  `stellar_celestial_tests --capture <directory>` and
  `stellar_native_planet_disc_assets_tests --capture <directory> <Sol asset root>`.
  They write raw RGBA plus dimensions/timing profiles after running their assertions;
  default CTest behavior is unchanged. No production helper or new runtime is added.
- Baseline `b110e223` renderer versus candidate: all 19,398,656 bytes in eight
  captures match exactly (three star colors/seeds, black hole, both ring halves,
  Mercury and Neptune). Star generation fell from 207.7–210.4 to 79.8–80.3 ms.
  Actual first system scene fell from 340–346 to 209.7–213.0 ms. Galaxy-to-system
  capture transitions measured 209.5–209.9 ms. These include remaining planet
  preparation and text work; they are not a claim of stall-free entry.
- Strict build and five CTests passed: celestial appearance, planet discs, system
  workspace, system colony entry and settlement workspace. Four final Vulkan
  system/galaxy launches passed at 720p/1080p, including paused save/reload and
  observer checks. Evidence: `native-celestial-cold-tests.log`,
  `native-celestial-cold-final-runtime.log`, `work/celestial-{before,after}/`,
  `work/celestial-pixel-comparison.json`, `work/cold-{before,after}-{system,galaxy}.json`.
- The saved-game fix at `b110e223` passed the Windows export job of native CI
  `34936942630`. The subsequent celestial candidate needs its own exact-head CI.

## Steady-frame profiling checkpoint

- Native map smoke accepts `--profile-frames N` (120–3600) with an isolated
  `--system-smoke` or `--galaxy-art-smoke`. The original warm-up/save frames remain;
  N extra steady frames precede the original screenshot/transition sequence.
  Normal runs and existing smoke defaults remain unchanged. Minimize/restore
  interruption cannot silently count a discarded interval as a valid sample.
- Engine `Window::draw` takes an optional `FrameTiming*`, reset per draw, reporting
  CPU submission, screenshot readback/write, fallback throttle and present times.
  No per-phase clocks or timing history are added to normal rendering. Presentation
  includes driver/display waiting and is not a GPU execution measurement.
- `steady_profile` reports exact sample count and mean/p50/p95/p99/max for interval,
  update, scene, submission, readback, throttle and present. The maintained system
  and galaxy export validators accept `profile_frames=600`, validate the complete
  diagnostic and still verify artwork, observer secrecy, saves and exact reload.
  No fixed hardware-independent FPS pass threshold is imposed.
- Four actual Vulkan runs, each 600 extra frames, passed at 720p and 1080p. Paused
  500-system campaign interval means were 16.716–16.723 ms; p95 16.834–17.025 ms;
  p99 17.284–19.705 ms. Update/scene/submission means were respectively
  0.026–0.033 / 0.053–0.175 / 0.258–0.323 ms. Present means 16.158–16.355 ms
  dominate; readback and fallback throttle were zero in all steady samples.
  This supports roughly 60 FPS after warm-up on this host, not busy campaigns,
  every resolution/hardware, stall-free entry or full visual parity.
- Strict native build, real Vulkan platform timing/reset/pixel checks, 34 focused
  Python tests, and four invalid native CLI cases passed. Two default-frame
  diplomacy Vulkan launches also passed UI/portrait/secrecy and exact reload
  checks (`native-steady-default-runtime.log`, `work/steady-default-diplomacy.json`). Evidence:
  `native-steady-profile-{build,tests}.log`, `native-steady-baseline-runtime.log`,
  `work/steady-baseline-{system,galaxy}.json` and `build-native/preview-*.bmp`.
- The previous celestial optimization `c373aed4` passed native CI `34939226242`.
  Profiling head `b491783c` also passed native CI `34942125331`; no shared merge.

## Background system-art preparation checkpoint

- Engine `ImagePreparationQueue` wraps one existing `JobSystem` worker. Owner-thread
  admission returns a move-only ticket or no capacity; polling never waits for
  rasterization. Defaults: 16 jobs / 32 MiB reserved output, including ready but
  uncollected results. Decoder scratch is separate (existing WIC bound, one decode
  at a time). Ticket cancellation drops queued/running results without waiting;
  explicit queue destruction joins the worker. Ready failures rethrow on `take()`.
- `NativePlanetDiscAssets::request_image` and celestial
  `use_background_preparation` opt into this queue. The existing synchronous path
  remains available for tools and exact pixel comparison. Pure workers own copied
  appearance keys/root paths, never Core, SDL, GPU or UI references. Only eligible
  observer-filtered data can request a body asset. Campaign discard cancels old
  requests; all ready results drain before admitting more work, even after moving
  away from the body that requested them. Existing cache budgets remain intact.
- `NativeSystemWorkspace::artwork_ready()` describes the last rendered frame.
  Temporary discs/progress text keep navigation usable while preparation runs.
  Smoke capture and multi-screen transitions wait for the actual final artwork,
  with a bounded failure, without counting extra wait frames in `steady_profile`.
  Diagnostics expose pending frames, preparation wall time and capture wait frames.
- Strict native build and six focused CTests passed; the added readiness/visible
  progress/mouse-navigation regression subsequently passed, as did 42 Python
  system/galaxy/travel export checks. Celestial and planet tests compare complete
  final RGBA output with synchronous generation and exercise duplicates, saturation,
  abandoned requests, generation cancellation, missing assets and worker errors.
- Seven actual Vulkan launches passed: system/galaxy at 720p and 1080p with 600
  steady frames each, plus three travel/save/reload runs. System cold scene CPU:
  211.452/211.567 -> 2.936/2.807 ms. Galaxy capture-transition scene maximum:
  207.769/209.287 -> 19.779/19.577 ms (remaining regional scenery preparation).
  Artwork completes asynchronously in 213–258 ms; galaxy captures waited 15/16
  frames and their completed Sol images were visually inspected. Warm interval
  mean 16.717–16.722 ms, p95 16.913–17.026 ms, p99 17.487–18.950 ms.
- Evidence: `native-background-art-tests.log`,
  `native-background-art-capture-tests.log`, `native-background-art-owner-tests.log`,
  `native-background-art-runtime.log`,
  `work/background-art-{system,galaxy,travel}.json`, and `build-native/preview-*.bmp`.
  Baselines remain `work/steady-baseline-{system,galaxy}.json`. System-art head
  `96b83092` passed native CI `34946960470`; `b491783c` passed `34942125331`. No release,
  shared merge, full-suite run or clean-machine certification is claimed.

## Background galaxy-art preparation checkpoint

- `NativeGalaxyBackdropAssets` now offers `request_deep_field`,
  `request_galaxy_layer`, `request_regional_nebula`, `pending_count` and
  `cancel_preparation`, using the same Engine queue as system artwork. Sources
  use absolute paths; workers own copied paths/labels only. Owner-thread polling
  drains all ready results, including abandoned-view requests. Queue saturation
  returns pending and is retried; campaign discard cancels outstanding tickets.
  The synchronous API remains for tools and exact comparisons.
- Each source is decoded and validated locally before cache assignment, with an
  8 MiB per-image limit and three cache slots (24 MiB maximum). Errors preserve
  source label, full path and underlying cause. Failed/oversized results never
  populate the cache. Existing decoder scratch bounds remain separate.
- `NativeGalaxyBackdrop::artwork_ready()` covers the last appended frame. Missing
  layers wait independently; the central secrecy fog remains drawn throughout.
  Main uses the existing status region for preparation feedback and applies the
  existing bounded final-art capture gate to galaxy views too. Finished artwork,
  geometry, colors and rendering order are unchanged.
- Strict native build, three focused CTests and 46 Python export checks passed.
  Tests cover blocked/saturated queues, no duplicate work, view changes, discard,
  owner guards, missing/oversized sources and exact final RGBA equality. The
  maintained backdrop test has a 45-second timeout and cleans only its own
  verified temporary fixture directory.
- Six actual Vulkan launches passed at 720p/1080p: galaxy and Sol with 600 steady
  frames each, plus default diplomacy checks. Existing artwork, input, secrecy,
  save and exact paused Player17 reload gates remain intact. Galaxy first-scene
  CPU maxima: 45.432/46.248 -> 3.163/2.695 ms; regional capture transitions:
  19.779/19.577 -> 1.027/0.833 ms. Sol remains 2.907/2.867 ms. All four completed
  overview/regional BMP files match `96b83092` byte for byte.
- Steady interval means were 16.716–16.721 ms, p95 16.935–17.087 ms and p99
  17.338–19.141 ms. Cold render/present still reaches ~67 ms at frame 7; its
  submission/driver/display contribution is not yet isolated. No broad 60 FPS
  certification, visual-parity, sealed-release or shared-merge claim is made.
- Evidence: `native-galaxy-background-tests.log`,
  `native-galaxy-background-runtime.log`, `work/galaxy-background-{galaxy,system,diplomacy}.json`,
  `work/galaxy-background-pixel-comparison.json`, and `build-native/preview-*.bmp`.
  Previous system-art head `96b83092` passed native CI `34946960470`; this new
  checkpoint needs its own run.

## Cold rendering diagnostic checkpoint

- `--profile-frames` now also emits `cold_profile={"rows":[...]}` for exactly the
  first ten rendered frames. Rows contain frame number, update/scene/submission/
  readback/throttle/present/render-present milliseconds and image-upload counters
  before/after the draw. This is a fixed-size diagnostic; normal play adds no
  history, per-phase clocks or upload-counter queries. Engine behavior is unchanged.
- The system/galaxy exporter returns `systemColdProfiles` / `galaxyColdProfiles`
  only when profiling is requested. Parsing rejects missing/duplicate tokens or
  JSON members, incomplete/out-of-order frames, nonfinite/negative timings,
  screenshot readback, backwards upload counters and subphases exceeding their
  enclosing draw (with three-decimal rounding allowance).
- Strict native build and 57 Python checks passed. Four actual 600-frame Vulkan
  galaxy/system profiles at 720p/1080p passed the existing artwork, UI, observer
  and exact paused save/reload gates. Two default-frame diplomacy Vulkan launches
  passed after restoring the unchanged Engine configuration.
- Frame 7 contains 65.471–66.460 ms of present time. Sol uploads no images on that
  frame and queues drawing in 0.236/0.258 ms; galaxy submission is 1.563/1.687 ms.
  Present includes SDL's deferred command flush and GPU/driver/display waits,
  not exclusively display waiting or GPU execution. The exact lower-level cause
  remains unresolved. Paused steady means remain 16.717–16.722 ms.
- A local one-frame-in-flight experiment was tested in four 120-frame map profiles.
  It did not change the ~66–67 ms cold tail and was fully reverted before final
  build/validation. Do not repeat that setting change without new evidence.
- Evidence: `native-cold-render-runtime.log`, `work/cold-render-{galaxy,system}.json`,
  `native-cold-profile-final-validation.log`, `work/cold-profile-default-diplomacy.json`.
  Discarded experiment: `native-cold-queue-experiment-{build,runtime}.log` and
  `work/cold-queue-one-{system,galaxy}.json`. Galaxy head `63d62d31` has native CI
  `34950285134` in progress; the new diagnostic needs its own run. No shared merge.

## Running-campaign performance checkpoint

- New opt-in `--campaign-profile <capture.bmp>` requires an isolated `--save-path`
  and `--profile-frames 120..3600`; it is exclusive with other graphical modes.
  Optional `--load` resumes an existing Player17 campaign. Normal smoke defaults
  and normal gameplay are unchanged; no simulation rules or fake fleets are added.
- Warm-up is paused through frame 119. UI speed clicks select Maximum (8X),
  then UI resume/pause clicks bracket the exact requested active frames. Real
  elapsed time goes through normal Player CampaignFrame policy. A manual save
  is requested midway; completion and subsequent time advancement must both be
  observed. Final UI pause freezes the day while the last save and artwork finish.
  Session/save failures fail the check instead of being retried.
- `native_campaign_profile.py` runs active 720p fresh and active 1080p reload,
  each followed by an independent paused reload/resave. Entire Player17 payloads
  must match except SavedAtUtc, including the frozen day; the active source copy
  must remain unchanged. Strict parsing rejects wrong speed/count/flags,
  unordered/nonfinite days and malformed/duplicate metrics. See the validation
  document for invocation and explicit early-campaign scope.
- Evidence: strict native build; four CTests (native_campaign_session,
  native_client_input, campaign_frame_parity, strategic_clock_parity); 88 Python
  checks; four invalid CLI cases; four active/paused Vulkan launches and two
  unchanged default system launches. The first CTest attempt found two unbuilt
  test executables; building their canonical targets resolved it and all four
  passed. No scratch checker or .NET process is involved.
- 600 measured frames per active run: day 0 -> 80.2483424 -> 160.514568.
  Interval mean/p95/p99: 720p 16.718/16.917/18.181 ms; 1080p
  16.722/17.113/18.817 ms. Update means 0.305/0.359 ms, maxima 5.941/4.434 ms;
  readback and fallback throttle are zero. This does not prove late-game workload
  or every frame/hardware configuration at 60 FPS.
- Logs: `native-active-campaign-{build,unit,runtime,validation}.log`;
  JSON: `work/active-campaign-baseline.json`,
  `work/active-profile-default-system.json`; screenshots:
  `build-native/preview-active-{1280x720,1920x1080}.bmp`. The 1080p capture was
  visually inspected: completed galaxy artwork, secrecy fog, final pause/day/save.
- Previous cold diagnostic `86445dde` passed native CI `34952179710`.
  This checkpoint needs its own CI; Engine 0.1.58 remains a candidate in PR #332.
- Read-only audit found known-system vector/set rebuilding every frame and fleet
  projection every 0.1 seconds even when its workspace is hidden. Early active
  timings do not justify speculative changes. First reproduce a developed
  campaign workload with real fleets/colonies using existing progression or an
  explicitly documented fixture, then optimize measured costs with observer and
  whole save/reload checks. Keep required simulation steps authoritative.

## Developed campaign save repair and fleet workload

- A real 24-ship campaign written by the native game failed explicit loading at
  research restore (82%): `Format v17 Adaptive Research state could not be decoded.`
  `AdaptiveResearch.Civilizations[0].Research.Outcomes.RecentRecords[0].Outcome`
  contained `"hypothesisSupported"`; the canonical ordered reader requires Int32.
  The existing empty-outcome fresh fixture could not expose this writer defect.
- Player17 now writes typed numeric values only at schema-defined paths: Outcome,
  tacit ScopeKind/AssimilationStage, and four foreign-assessment axes. Standalone
  Adaptive Research codecs, strict reader, C# fixtures and simulation stay unchanged.
  The populated enum regression preserves ordinary text. Negative seed 115500 and
  positive 115501 additionally restore/activate/recapture every payload field and
  array element; JSON dictionary member order is intentionally ignored (Leadership
  is reordered on restore), without excluding any campaign fields.
- The maintained fresh-progression executable can export `--profile-save` to an
  absolute nonexistent path. It uses actual controllers and paid research/builds:
  12 scouts + 12 science vessels, day 10154, treasury 346.233, nine total colonies
  (three owned). All ships receive ordinary routes and enter transit before save.
  Offline Developer stepping is disclosed; no funds, capabilities or fleets are
  injected. An initial 32-ship attempt exhausted the real budget after 25 ships;
  the reproducible benchmark uses 24 rather than bypassing that limit.
- `validate_native_campaign_profile` accepts `initial_save` and
  `minimum_moving_fleets=24`. It copies the source to an isolated slot, requires
  actual motion by stable fleet ID, performs two 600-frame Player-policy runs at
  8X and two independent paused reloads, and verifies the original source bytes.
  All 24 move in both intervals; all remain in transit after 720p but have arrived
  by the end of 1080p. This is not a claim of 24 transiting ships on every frame.
- Eight focused CTests, 17 Python checks, strict MSVC build, four active/paused
  Vulkan launches and two default system runs pass. Interval mean/p95/p99:
  720p 16.713/17.401/18.516 ms; 1080p 16.714/17.604/18.560 ms. Update mean/max:
  0.476/6.154 and 0.385/3.586 ms. Days 10154 -> 10234.2218632 -> 10314.4471072.
  The 1080p capture was inspected for finished art, fleet outliner and final pause.
- Source `work/developed-fleet-24-fixed.player17.json` SHA-256:
  `b5a57777a4eacc57458ee92c5ebc74cc77aaea5409732e86d665e7151915a17f`.
  Results `work/developed-fleet-24-profile.json` and
  `work/developed-fleet-fixed-system.json`; logs
  `native-developed-fleet-fixed-{validation,runtime}.log` and
  `native-developed-fleet-final-unit.log`. The older active fixture and runtime
  failure log remain local generated evidence; do not hand-edit them into passing.
- Prior `370079d0` passed native CI `34955639654`. This repair requires its own run.
  Engine 0.1.58 is still an unmerged candidate in PR #332, not a sealed release.

## Native audio checkpoint (2026-09-15, candidate)

- Engine `native_audio` adds immutable 48 kHz stereo clips, Windows Media
  Foundation decoding and SDL3 output. No Core dependency or new decoder DLL.
  One music stream queues at most 288000 bytes; eight finite SFX voices are each
  capped at 1 MiB. Decode runs on one Engine worker; window/output/Core stay on
  the owner thread. The director closes audio before Window's SDL teardown.
- `NativeAudioDirector` decodes the existing user score and six WAV cues once.
  Startup waits for staging or a reported audio failure, stays silent during
  boot, then starts music once at menu admission. Campaign entry does not restart
  it. Startup actions and campaign navigation confirm with the existing cue.
  Hover/event APIs exist; voice and full event hooks are still pending. Settings landed in the following checkpoint.
- Exact-hash CMake/export manifests package only seven clips and their credits.
  Credits use pinned CRLF bytes; both Git checkout modes pass. Application-only
  MFPlat/MFReadWrite imports are reviewed without broadening SDL's allowlist.
- Five focused CTests and 92 Python checks pass. Relocated Vulkan new-game 720p
  and paused reload 1080p pass with `audio_check=True`, restricted PATH, Unicode
  isolated save paths, independent new slot and complete Player17 equality.
  Each starts music once, queues 288000 bytes and stops cleanly. Fresh startup
  records 38 silent boot services and one confirmation; reload records zero for
  both. Real default output is used; these diagnostics do not verify speakers
  audibly or voice quality. An unavailable SDL audio driver fails the opt-in
  check cleanly with exit 1 after reaching campaign, one disable diagnostic and
  no retry; the source anchor remains unchanged.
- Evidence: `work/native-audio-runtime-evidence.json`,
  `native-audio-actual-runtime.log`, `native-audio-unavailable-device-final.log`,
  `native-audio-final-validation.log`, `native-audio-python-validation.log`.
  Final validation includes finite-effect flush/drain, plus 12 exporter integrity
  tests (17 unrelated headless executable tests skipped without their opt-in).
  Windows CI now explicitly builds/runs both audio targets on SDL's dummy driver;
  it does not need a GPU or sound device. Hardware-backed runtime evidence is local.
  The relocated folder is a local validation fixture, not a sealed release.
  See `docs/engine/NATIVE_AUDIO.md` for limits and remaining work.
- Audio foundation head `6e44bb40` passed native CI `34968941076`, including
  the dedicated GPU-independent audio checks. PR #332 remains unmerged.

## Native audio settings checkpoint (2026-09-15, candidate)

- Main-menu Settings and pause-menu Settings open the same owner-thread overlay.
  Master/Music/Effects sliders preview immediately; mute retains those levels,
  Defaults previews the original levels, Cancel/Escape restores the last saved
  preferences, and Save persists explicitly. Escape closes only this overlay;
  campaign time and the parent pause menu stay paused. Controls use drawable
  coordinates and resize/focus loss cancels a captured slider drag.
- Settings are independent of Player17: normal play uses Windows LocalAppData
  `Stellar Continuum/NativePreview/audio-settings.json`; all smoke invocations
  isolate that file beside their explicitly supplied save anchor. Loaded levels
  apply before music starts. No campaign payload, simulation or C# source changes.
- The version-1 JSON has five exact fields, a 4 KiB bounded read, finite 0–1
  gains, strict types, duplicate rejection and Engine atomic writes. Corrupt
  preferences retain their original bytes until explicit Save; the game uses
  defaults with a short visible explanation and a full path/cause on stderr.
  Save failure keeps the panel open. Playback failure also appears in the panel.
- Six focused CTests pass: audio backend/director/settings, startup workspace,
  responsive menu layout and campaign session. Settings tests pass twice in
  independent scratch children. All 97 related Python tests pass. Windows CI
  now also builds/runs `native_audio_settings` without requiring a GPU/device.
- Maintained opt-in `--audio-check --audio-settings-check` exercises actual menu
  routing, previews 25%/50%/75%, toggles mute, verifies Cancel, saves and reopens.
  Final relocated Vulkan runs pass at startup 720p and paused reload 1080p, with
  inspected screenshots, restricted PATH and Unicode temporary paths. Reload
  starts at the saved levels, retains byte-identical settings, and preserves the
  entire paused Player17 payload except SavedAtUtc. The original anchor is intact.
  Unsupported CLI use exits 1 with a useful message before opening a window.
- Final evidence: `native-audio-settings-final-build.log` (six CTests),
  `native-audio-settings-tests.log` (repeatability), `native-audio-settings-python.log`
  (97 tests), `native-audio-settings-runtime.log`, and
  `work/native-audio-settings-evidence.json`. Screenshots are under
  `work/native-audio-validation/package-*-audio-settings.bmp`. This is local
  validation, not a sealed release or proof of speaker/voice quality.
- The preceding audio foundation has green CI at `6e44bb40`; this new settings
  checkpoint needs its own run. Engine remains 0.1.58 candidate, game remains
  0.1.7-alpha, and `graphicalParity=false`. Keep work on the Codex branch; do not
  merge shared branches or duplicate the audited Devin changes.

## Native scientist and owned action feedback checkpoint (2026-09-15)

- The prior settings commit `de65ce7b` passed Windows CI run `34972936916`,
  including export and all three dummy audio/settings tests. Devin `8a3b71c9`
  adds sealed-validator documentation only; no new source overlap to import.
- Added three fixed, dry British human scientist cues from the existing `bf_emma`
  profile. Installed, hash-verified Kokoro generated the WAV files locally;
  the exported native player needs no Python, model, SAPI, Godot or .NET.
  Native dynamic TTS is still absent; the previously described upstream
  optional TTS path belongs to the C#/Godot reference.
- Dedicated finite speech playback: 8 MiB per decoded clip / 16 MiB total,
  288000-byte input queue, one speaking line, three coalesced pending cues,
  eight-second cooldown, Master x Effects volume and temporary 55% music duck.
  All 14 audio/credits files are pinned in both build and export paths.
- Fixed queue accounting discovered by the new tests: SDL available output
  includes sound convertible from queued input, so adding the two double-counts
  buffered sound. They are now reported separately; both must drain before
  finite speech/effects retire. No error suppression or retry loop was added.
- Actual CampaignFrame results now feed observer-filtered notices and bounded
  completion sounds. No raw event text/IDs escape into this presentation summary.
  Failed research is a report; outpost completion says settlement established.
  Six-second notices coalesce; load activation clears notices/pending speech.
  Human casting does not silently replace alien advisors.
- Validation: nine distinct native CTests passed (audio, director, settings,
  feedback, system workspace/travel, startup, UI layout, campaign session).
  128 focused Python/export-integrity checks passed. Real Vulkan runs exercised
  route preparation plus 720p moving-system and 1080p paused-reload windows;
  the latter two each played one scientist line after unknown-arrow input,
  rejected four rapid repeat requests, kept the input queue bounded and stopped
  cleanly. Connected-lane knowledge and full paused Player17 equality passed.
  Captures were inspected; audible voice quality still needs listening acceptance.
- Evidence: `native-scientist-build.log`, `native-scientist-final-build.log`,
  `native-scientist-tests.log` (includes the diagnosed first failure),
  `native-scientist-python.log`, `native-scientist-integration-python.log`,
  `native-scientist-runtime.log`, `work/native-scientist-evidence.json` and
  `work/native-audio-validation/package-local-travel-*.bmp`.
- This is a local validated checkpoint on the Codex candidate, not a new sealed
  downloadable release. Engine 0.1.58 candidate / game 0.1.7-alpha and
  `graphicalParity=false` remain. The native surface terrain/art checkpoint is
  now extended by the validated bounded top-down scene described above.

## Native surface ground checkpoint (2026-09-15, candidate)

- Integrated the existing approved ground albedo into the native operational
  surface view, without changing its canonical placement coordinates, costs,
  construction, removal or saved state. This is generic top-down ground detail;
  the inspected scene still uses placeholder hub/building markers. It does not
  complete realistic colony graphics, roads, environment-specific terrain or 3D.
- The app submits one image through the shared background preparation queue,
  then injects an immutable image into the pure surface workspace. One cached
  decode is capped at 16 MiB. Camera-aligned tiles use a 512-unit base span and
  adaptive powers of two at extreme zoom-out, capped at 64 clipped images.
- Review caught a cleanup defect that could resubmit a failed image job after
  its future was cleared. The asset owner now caches and rethrows the original
  exception; repeated requests cannot admit another job. Explicit queue
  reinitialization is the recovery boundary. The new test enforces four identical
  repeat failures with zero outstanding jobs or reserved memory.
- Both surface CTests and all 105 related Python/export checks pass. The new
  artwork test also covers source decode,
  cache identity, owner-thread access, 720p/1080p/4K, pan, anchored zoom, extreme
  zoom-out and tile coverage. The initial fixed-size test assumption was wrong
  at adaptive detail levels; the final assertion verifies power-of-two world
  span, alignment and projected size with float tolerance.
- Three actual relocated Vulkan launches passed: fresh standard 500-system Earth,
  mouse placement/cancel/refund at 720p, and paused reload at 1080p. The entire
  paused Player17 payload matches except its timestamp, including the unfinished
  site and exact treasury/progress. Both surface captures were inspected.
  Frame means were 17.051/17.106 ms for the two surface runs; these short checks
  do not establish sustained 60 FPS. The previously observed cold presentation
  tail remains (~67 ms).
- Export pins the unchanged image and its provenance note, with LF preserved for
  the note on Windows checkout. The maintained exporter runs the new asset tests;
  Windows CI also builds/runs `native_surface_art` without a GPU.
- Evidence: `native-surface-art-build.log` (diagnosed first test failure),
  `native-surface-art-final-build.log`, `native-surface-art-final-python.log`,
  `native-surface-art-runtime.log`, `work/native-surface-art-evidence.json` and
  `work/native-audio-validation/package-surface-*.bmp`. This folder is local
  validation, not a sealed new release. Engine remains 0.1.58 candidate / game
  0.1.7-alpha, with `graphicalParity=false`.
- Next orbital work needs a canonical placement contract first: current
  `ShipyardState` is empire-level and has no system/body ID; the native system
  snapshot contains no structures. Completed empire-level orbital construction
  capability is insufficient evidence to invent a station orbiting a body.
  Coordinate this association with the Core owner before rendering stations.

## Remaining blockers / next work

- Diplomacy presentation has been audited against the C# reference. That workspace
  does not contain a grievance display, demand/trade composer or claims panel;
  claims belong on the strategic map and are covered by the selected territory
  port. Do not track those absent reference features as native parity gaps.
- System and galaxy CPU imagery preparation is staged; early and developed
  24-ship campaigns now have measured baselines. Prioritize the missing native
  presentation/audio below. Revisit timing for new failures or substantially
  larger fleet/combat workloads, not repeated unchanged maps or speculative
  renderer settings. Keep Core access and GPU/window work on the owner thread.
- Detailed 3D colony presentation and manual roads remain incomplete. Orbital construction geometry
  may be added as a labeled schematic preview, but physical station placement waits
  for an authoritative Core/save host and location contract.
- Full character/species casting and dynamic speech remain. Fixed human
  scientist cues, owned action feedback, main score and persisted settings work.
- `cleanMachineTest` still needs a separate machine/VM.
- `graphicalParity=false` stays until visual parity evidence exists.

## Systems intentionally not touched

- Godot/C# `src/` reference (kept as behavioral baseline until parity gates pass).
- `editor/` WPF tool (other workstream).
- Legacy research runtime — retained for save compatibility; Adaptive Research is
  authoritative.

## Coordination

- PR #325 (draft) + issue #324 remain the coordination hub. PR #332 is the current Codex candidate; do not merge either candidate or claim a shared-branch/full-suite/clean-machine result without the corresponding review and evidence.
- Sealed export = `tools/stellar-export/stellar.py export windows-native-preview`.
- Dev env: VS 2022 BuildTools `VsDevCmd` + `.tools/build-tools` venv (cmake/ninja/python).

## Codex checkpoint: native framing, renderer overhead, and reproducibility

- A fresh Windows checkout at `ac45d958` failed CMake's ship-art credits hash check: Git converted the reviewed LF file to CRLF. `.gitattributes` now pins all six hash-verified native text assets to their existing reviewed byte formats. No manifest hash or integrity check is weakened. A real Git checkout regression covers `core.autocrlf=true` and `false`.
- Soft-circle submission retains the same 20 triangles and ordered blending, using stack vertices and cached directions/indices instead of two heap allocations and 42 trigonometric evaluations per circle. Engine remains independent of Core.
- System framing uses measured body labels, disc/orbital/stellar envelopes, and a 12px presentation inset. Labels are collision-resolved with selected-body priority; initial travel activation frames exits once, while later travel refreshes retain the user's pan/zoom. Authoritative AU, transit, lane, and observer contracts remain unchanged.
- Final evidence: `native_system_travel`, `native_system_workspace`, `native_system_view`, `native_system_colony_entry`, and `native_settlement_workspace` CTests passed, alongside seven actual Vulkan galaxy/system/travel launches with exact paused Player17 reload and observer-secrecy validation. Final logs are `native-system-layout-tests.log` and `native-system-checkpoint.log`; JSON is `work/layout-{galaxy,system,travel}.json`; captures are `build-native/preview-*.bmp`.
- Smoke-only timing records bounded update/scene/render-present mean and p95 before JSON diagnostics. System/galaxy means were ~20.6–21.2 ms and p95 ~33.4–33.7 ms; render-present means ~16.4–16.6 ms include VSync wait. Treat this as investigation evidence, not a GPU-only metric or 60 FPS result.
- Layout/timing head `66c56b897ad04a99d2e662a2fb089cdcc160186b` passed GitHub workflow `34929897092`; diplomacy head `0e0835ae` subsequently passed `34934085259`. Neither covers the subsequent async-save changes. No shared merge or clean-machine certification is claimed.
- The older 0.1.55 checkout/scratch work is preserved separately and must not be reapplied over the integrated 0.1.57 artwork.
