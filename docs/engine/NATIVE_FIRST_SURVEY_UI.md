# Native first science survey

## Player and authority contract

The first survey continues the unchanged Player17 campaign earned by
`NATIVE_FIRST_EXPLORATION_UI.md`. The scout has completed its first reconnaissance
of Proxima Centauri, while the paid, completed science vessel remains in Sol.
The replay copies that save; it does not grant research, ships, resources,
travel, or survey knowledge. Core remains the authority for every mission step.

Outliner selection, Locate, map zoom and drag, right-click route preview, and
Confirm send the science vessel along the existing Sol-to-Proxima travel lane.
Selection and preview must leave the complete campaign unchanged. Acceptance
issues exactly one order; ordinary simulation steps carry the vessel through
local departure, interstellar warp, local arrival, and detailed survey work.

The campaign is saved during the incomplete local survey. A separate paused
reload must preserve the complete Player17 payload except SavedAtUtc. Resume
finishes the existing survey without issuing another order. A second paused
reload preserves the completed state. The scout's complete saved fleet object
must remain unchanged throughout all four launches.

## Player feedback and discovery privacy

Locally stationed owned science vessels show the observer's recorded survey
percentage, a progress bar, Hold status, and completed-survey guidance. Route
previews and active travel take precedence. No raw planetary facts, undiscovered
hazards, or speculative duration estimate are added to this fleet projection.

The replay opens the destination system and selects a planet through actual
pointer input. Grouped planetary facts remain unavailable during partial survey
and become visible after Core records full survey. Inspector selection and
scrolling must be read-only across the complete campaign. Actual frame results
are delivered through the normal notification and scientist-voice feedback path;
the replay does not synthesize a completion event.

The replay advances exact 1/64-day Normal-speed Player frames, bounded to 256
days and 300 seconds per active stage. This is accelerated offline validation,
not a timing balance change or a real-time performance measurement. Core's
survey profile and operating capacity determine the amount of work per day.

## Maintained export gate

`native_first_survey_runtime.py` follows the first-exploration export gate:

1. `--first-survey-smoke`: departure and incomplete-survey captures at 720p.
2. `--first-survey-paused-smoke`: unchanged incomplete-survey reload at 1080p.
3. `--first-survey-resume-smoke`: completed survey and planet inspector at 720p.
4. `--first-survey-paused-smoke`: unchanged completed-survey reload at 1080p.

All modes require `--load --seed 115501`, run from a temporary working directory
with restricted PATH, and emit strict `first_survey` JSON evidence. The validator
binds that evidence to the actual source and result saves, fleet identities,
survey knowledge, selected planet, and exact-dimension images. Invalid evidence
fails clearly in the terminal with a nonzero exit code.

This covers the first earned science mission for one fixed campaign. It does
not establish all races and seeds, finished visuals, long-campaign performance,
sustained 60 FPS, or a clean-machine release. The local package is unsealed.

## Checkpoint evidence (2026-09-16)

The earned science vessel (1) travels from Sol (0) to Proxima Centauri (1).
One confirmed order raises mission revision 0 to 1. In 159 exact 1/64-day
steps, day 6572.359375 reaches 6574.84375 after all three travel phases and the
start of local survey. Survey progress rises from 0.35 to 0.35082453825857518.
Resume advances 788 further steps to day 6587.15625 and full survey/progress 1.
Science travel spends 4.23 ly of fuel; local survey preserves the remaining
1095.77 ly. The scout's saved fleet object remains unchanged.

All four relocated Vulkan launches pass. Paused reloads preserve the whole
Player17 payload except SavedAtUtc, and the source save remains untouched.
Six images and four distinct saves are retained. Planet 1001 (Ilyra) is selected
through actual clicks at both resolutions: physical and environmental readings
are Unconfirmed during partial survey and confirmed after completion. Inspector
scroll/reset is read-only. Completion reaches the ordinary notification path.

Visual review caught the inspector sidecar being taken while artwork was still
preparing. The capture now waits on the existing bounded artwork-ready gate and
returns the inspector to its top through wheel input. It does not add a gameplay
delay. The partial-survey card also now says detailed survey is incomplete,
rather than incorrectly describing completed reconnaissance as incomplete.

Validation: final MSVC build, 10 affected CTests across the milestone (seven
rerun after the final presentation changes), Python 552 checks (535 pass,
17 optional executable skips), and navigation, fleet, system inspection and
earned first-exploration Vulkan regressions. Missing load, wrong seed and
duplicate modes all fail clearly in the terminal. Evidence is retained in
work/native-first-survey-{build-final,ctest,ctest-final,python-final,runtime,
regressions,cli}.log and work/native-first-survey-{runtime,regressions}.json.
The shared base and reviewed Devin head remain ac45d958 / b023e384.
