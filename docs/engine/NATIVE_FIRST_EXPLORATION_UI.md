# Native first scout exploration

## Player and authority contract

The first exploration replay continues the unchanged Player17 save earned by
`NATIVE_FIRST_SHIPS_UI.md`: fresh Terran 500-system seed 115501, research and
facilities completed through normal inputs, and paid, completed scout/science
ships. It copies that save; it never authors fleet, research, economy or survey
state. Core remains the authority for lanes, range, fuel, travel and discovery.

The scout is selected through the outliner. Locate, wheel zoom and dragging make
the near-home destination hittable. A right-click opens the ordinary route
preview; Confirm issues the only travel order. Selection and preview must leave
the complete campaign unchanged. The destination must be a real, direct lane
neighbor with less than partial survey knowledge.

Departure is followed by a positive, incomplete interstellar warp. The campaign
is saved and reloaded in a separate paused process. Resume continues the saved
order through local arrival and automatic reconnaissance, with no replacement
order or injected discovery. A second paused reload preserves the completed
mission. Each paused comparison covers the whole Player17 payload except the
save timestamp.

The replay advances exact 1/64-day ordinary Normal-speed Player frames with no
backlog, bounded to 256 days and 300 seconds per active stage. This is accelerated
offline validation, not a player pacing or real-time performance measurement.
Reconnaissance uses the canonical two work-days and 0.35 discovery threshold;
work may advance more slowly than campaign time when operating capacity falls.

## Visible mission feedback

Locally stationed owned scouts show their recorded reconnaissance work and a
progress bar. Hold reports that work is paused. Completed reconnaissance points
the player toward a science vessel for a full survey, unless that survey is
already complete. Active travel and route previews take precedence. Foreign
fleet work and unexplored system details are never exposed by this projection.
The existing route area is reused, including at 1280x720.

The playthrough exposed a stuck pointer capture after clicking a fleet control.
Consumed fleet-button releases now finish the UI gesture, so moving back onto the
map and scrolling immediately zooms without an extra click. The journey checks
the resulting camera scale and retains the scout selection after map browsing.

## Maintained validation

`native_first_exploration_runtime.py` runs after the first-ships export gate:

1. `--first-exploration-smoke`: departure and partial-warp captures at 720p.
2. `--first-exploration-paused-smoke`: unchanged mid-warp reload at 1080p.
3. `--first-exploration-resume-smoke`: arrival and completed reconnaissance at 720p.
4. `--first-exploration-paused-smoke`: unchanged completed reload at 1080p.

All modes require `--load --seed 115501`, use a temporary working directory and
restricted PATH, and emit a strict `first_exploration` JSON proof. The validator
checks the proof against each actual source/result save and image metadata,
including departure/arrival sidecars. Invalid evidence fails in the terminal.

This contract covers one earned scout's first direct trip. It does not establish
every seed/race, long multi-hop campaigns, finished graphics, sustained 60 FPS,
or clean-machine installation. The local validation package remains unsealed.

## Checkpoint evidence (2026-09-16)

Validation: final MSVC build; 8 affected CTests; Python 532 checks (515 pass,
17 optional executable skips). The relocated four-process Vulkan journey passes
at 720p/1080p, preserving the complete Player17 payload across both paused reloads
except SavedAtUtc. Six exact-dimension captures and four distinct saves are
retained; departure, arrival and completed scout feedback were inspected. Existing
navigation, fleet, local-system travel and fresh first-ships graphical regressions
pass. Missing load, wrong seed and conflicting modes exit nonzero in the terminal.
Evidence: work/native-first-exploration-{build-final,ctest-final,python,runtime,
regressions,cli}.log and work/native-first-exploration-{runtime,regressions}.json.
The shared base and Devin head remain ac45d958 / b023e384.

The route is Sol (0) to Proxima Centauri (1). The sole order raises mission
revision 0 to 1. Departure reaches a positive partial warp at day 6569.34375
from day 6568.5 in 54 steps; resume completes reconnaissance at 6572.359375 in
193 steps. Survey level changes from detected to partial, progress 0 to 0.35,
while the science vessel remains unchanged. Player17 reconstructs travel lanes;
the direct-lane gate combines the native Core route assessment, the actual saved
route identities and the native lane-connected check. It does not invent a
second Python lane-generation algorithm.
