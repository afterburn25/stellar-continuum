# Native first-ships playthrough validation

The native graphical replay starts a fresh Terran 500-system campaign with seed
115501 and earns its first scout and science vessel through the existing research,
construction and shipyard screens. The sequence follows
`NATIVE_FRESH_PROGRESSION.md`; it does not load an authored capable fleet or grant
funding, facilities, research capabilities or ships.

## Authority and time

Project selection, research search/panning, construction scrolling and ship orders
use the application's normal input routing. Read-only layout queries locate visible
cards and rows; they do not execute game commands. Each action must select the
intended item and produce the corresponding canonical state change.

Only validation time is accelerated. The ordinary Player frame policy runs at
Normal speed in exact quarter-day steps, grouped into four steps per day. The
replay requires strategic routing, zero backlog and the expected day increment.
It never changes the frame owner, runtime, cache identity or simulation rules.
The 10,958-day limit and wall-clock deadline bound failed research or progression.
Drawing and window event processing continue between bounded batches.

Offline batches do not schedule hundreds of autosaves. The final paused campaign
establishes the usual completed-frame save boundary and uses the existing save
path. The final capture is also restored and recaptured as Player17; every value
and array element must match. JSON object member order is not gameplay state.

## Required evidence

The maintained export validator runs fresh progression at 1280x720 and a separate
paused reload at 1920x1080 from a temporary working directory with a restricted
PATH. It requires real Vulkan rendering and exact per-file screenshot dimensions.
The generated save is not modified between launches.

Validation now passes on the native executable: MSVC build, 13 affected CTests,
and Python 516 checks (499 passed, 17 optional executable checks skipped). The
fresh run reaches day 6568.5 after 26,274 quarter-day steps, with 13 research
authorizations, 3 construction authorizations and 2 ship orders; it identifies
scout 0 and science vessel 1. The paused reload keeps that day and matches the
complete Player17 payload except `SavedAtUtc`. The earlier day than the
headless 6574 reference comes from issuing successive graphical orders at the
0.25-day boundary instead of the headless one-day boundary; no rule changes.
The 720p and 1080p captures were inspected, and existing navigation, research,
shipyard and construction Vulkan suites also pass. Visual review caught a stale
`Queued ... 2/8` notice after both ships had completed. Canonical order membership
or active/queued transitions now clear obsolete accepted feedback; changed
cancellation quotes clear their confirmation text. Progress-only refresh retains
ordinary feedback, and new command results are assigned after refreshing the
view. Final build, 13 CTests, fresh/reload and shipyard runtime checks pass; the
corrected 720p capture was inspected. Focused render regressions cover stale
queue and cancellation text.

The native target privately uses the existing pinned `stellar_json` interface
for full restore comparison. Core, C#, Player17 schema and approved artwork are
unchanged. The local package remains UNSEALED. C: temporary-space exhaustion
interrupted an earlier Python mock run; task-local TEMP/TMP on D: allowed the
full 516-check suite to pass without deleting user data.

The proof binds the actual seed and player, thirteen research authorizations,
three construction authorizations, two ship orders, elapsed quarter-day steps,
and the exact scout/science fleet identities. The saved campaign must contain the
completed launch complex, orbital shipyard and warp test facility, their required
research gates, and exactly the first two completed owned ships. Archived research
does not satisfy a maturity gate. Ship authorization alone cannot count as a
completed ship. The second process issues no orders and advances no time; its full
paused payload must equal the first save except for the save timestamp.

The executable modes are `--fresh-progression-smoke <capture.bmp>` and
`--fresh-progression-reload-smoke <capture.bmp>`. Both use `--seed 115501`; only the
second accepts and requires `--load`. They remain mutually exclusive with the
other graphical replay modes. Failures print a terminal diagnostic and exit
nonzero.

## Scope limits

This is application input replay, not physical mouse injection or a measure of
real-time player pacing. Existing fast authored shipyard/fleet cases and the
headless positive/negative research tests remain useful separate checks. Seed
115500's documented warp-theory disproof can still block the current drive chain;
this validation adds no alternate technology or gameplay bypass. This milestone
does not establish every seed/race, finished graphical parity, sustained 60 FPS
or clean-machine installation. The next meaningful playthrough is the first
earned scout's normal UI order through timed local departure, warp, arrival and
exploration, followed by save/reload evidence. Logs are
`work/native-first-ships-{build,ctest,python,runtime,regressions}.log` and
`work/native-first-ships-{runtime,regressions,final-shipyard}.json`.
