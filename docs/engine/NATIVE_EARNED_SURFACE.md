# First earned colony surface building

This continues the earned seed-115501 Terran campaign from Xanthe's founded
Player17 save. The source is player 0, system 8, body 8004, colony 9, with
250 million settlers and no surface buildings. It follows the actual exploration,
paid colony vessel, travel and establishment chain in `NATIVE_EARNED_SETTLEMENT.md`.
It does not grant research, money, materials, population or completed buildings.

## Player guidance and authoritative costs

An empty colony now shows a compact development card alongside the existing
Open Surface action. It displays current power, materials, workforce and treasury,
then an available module with its authorization, material and workforce costs.
The recommendation uses the current native controller projection. It prioritizes
additional power for an existing power deficit, otherwise a supported fabricator.
No available module and insufficient treasury have explicit messages. The card
disappears when the colony has a construction site or building.

Core requires the authorization payment at placement; construction consumes
materials over time. The card therefore does not require the full construction
material stockpile before recommending a module. It states that incoming
materials supply construction and does not invent a completion estimate.

The canonical fabricator requires 50 simulation budget units ($500M UED),
450 construction materials, 2 power and 0.040 million workers. Xanthe's existing
2 power and workforce support it. Core controls material allocation, construction
progress, operating support, condition and output. Its base industry output is
one unit per day; the proof checks the actual controller projection after completion.

## Maintained graphical validation

`--earned-surface-smoke <bmp>` requires `--load --seed 115501` and the earned
empty Xanthe save. It opens the planet, colony and surface through existing input,
selects the available fabricator, reviews placement, cancels without changing the
full paused Player17 payload, reopens the quote and confirms exactly one payment.
The newly created site must be unfinished with zero construction progress.

Normal 1/64-day frames advance canonical construction, bounded to 1024 game days
and 300 seconds. A partial construction capture precedes completion. Completion
must be enabled, staffed, powered and produce positive industry output. The
runtime checks a full Player17 encode/restore round trip and explicitly saves.
`--earned-surface-paused-smoke <bmp>` reloads the completed site and verifies
unchanged time and full Player17 state while paused.

`tools/stellar-export/native_earned_surface_runtime.py` is wired into the export
pipeline after earned settlement. It runs the 720p continuation and 1080p paused
reload in a relocated package, validates the strict terminal proof against source
and saved state, retains the colony/review/construction/completion captures and
two saves, and preserves the original input file. Authored surface fixtures remain
separate regression coverage; they are not evidence of earned progression.

## Verified checkpoint (2026-09-16)

The actual 720p continuation advanced from day 6762.4134387 to 6832.1009387
in 4460 exact frames (69.6875 days). The fabricator charged 50 budget units
once: treasury 3873.798098454 to 3823.798098454 immediately after confirmation.
Later treasury changes are normal economy ticks, not part of that payment check.
Building 1 at (-70, -70), rotation 0, completed all 450 materials and contributed
1 industry/day with efficiency 1, power and assigned workers. The paused 1080p
reload preserved the full payload except `SavedAtUtc` and displayed the actual
selected building's Operating status. The intermediate capture shows 10% work
and 405 materials remaining. Both hub and building use prepared imagery, with
draw replacement counters checked after real preparation and rendering.

Visual inspection also caught an existing preview message claiming construction
had already been authorized. The native preview now describes the pending
authorization; commit still returns Core's original success message. The quote
binding checks the new deterministic preview text while retaining the canonical
assessment for confirmation. Rejections remain unchanged.

Validation: MSVC build, nine focused CTests, 586 Python checks (569 pass and
17 optional skips), the earned two-process Vulkan sequence, existing surface
and navigation runtime regressions, and four clean malformed-launch failures
with no save overwrite. Evidence is in `work/native-earned-surface-build-final.log`,
`-ctest-final.log`, `-python.log`, `-export-final.log`, `-export.json`,
`-regressions.log`, `-regressions.json` and `-cli.json`.

Next earned source: `work/native-audio-validation/package-earned-surface-paused.player17.json`.
All existing local power is now allocated to the fabricator. Further local
industry or research needs additional power under the existing rules. Continue
with clearer construction/operations feedback and a paid power/research expansion,
without changing the simulation to make the progression succeed.

## Boundaries

Core, the C# reference, Player17 and approved artwork are unchanged. Core does
not currently emit a construction-complete event for individual surface modules;
this check uses actual progress, completion and operating output rather than
fabricating an authoritative event. Offline accelerated stepping does not prove
real-time pacing or 60 FPS. The local validation package remains unsealed and is
not a release download. Detailed 3D cities, manual roads and broader production
visual polish remain unfinished.
