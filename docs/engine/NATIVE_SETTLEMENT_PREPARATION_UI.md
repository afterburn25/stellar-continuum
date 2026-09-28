# Native settlement preparation UI

## Current contract (2026-09-16)

The native settlement preparation smoke starts from the earned, fully surveyed
Player17 save and inspects a non-owned body in system 1 (Ilyra, body 1001).
The inspector prepends canonical species suitability, site facts and habitat
needs, then exposes both colony and outpost design costs. Ship and expedition
costs, minimum build workdays and population reservation remain distinct and
readable. View Shipyard uses the existing navigation footer with matched
press/release behavior; an owned colony keeps priority over preparation.

Observer, generation, system, body and full-survey identity are checked before
the view is shown; a mismatch clears stale preparation state. The review has no
new modal and performs no grant, spending or build action. Both vessel designs
were reviewed in the existing shipyard and the review closed without starting a
build. Core, C#, Player17 and approved art are unchanged.

The strict native mode is `--settlement-preparation-smoke`. It continues from
`work/native-audio-validation/package-first-survey-paused-full.player17.json`
(seed 115501, player 0, day 6587.15625), and emits one exact preparation proof
plus final, assessment, costs and shipyard captures. The source and each of two
serial 1280x720 and 1920x1080 runs preserve the complete Player17 payload
except `SavedAtUtc`; eight images and two saves are retained. The captured site
is too harsh because its limiting factor is Pressure on Ilyra and has no rare
deposit, so this is a preparation/read-only proof, not a settlement
founding.

MSVC build passed; eight focused CTests passed; Python validation reported 564
checks with 547 pass and 17 optional skips. Relocated native runs passed, and
the assessment, costs and shipyard captures at both sizes were inspected. The
costs capture includes the readable scroll position above the fixed footer. The
final build and all eight focused CTests were rerun after adding the Primary
hazard field; final 720p/1080p preparation and four-process earned-survey
replays passed, as did navigation, system and shipyard regressions. Three
malformed CLI cases (missing load, wrong seed and conflicting modes) exited
nonzero cleanly.
Evidence: `work/native-settlement-preparation-build-final.log`,
`work/native-settlement-preparation-ctest-final.log`,
`work/native-settlement-preparation-runtime-final.log`,
`work/native-settlement-preparation-runtime.json`, and
`work/native-settlement-preparation-regressions.json`.

## Next work

Use earned exploration and science to find a genuinely viable surveyed site,
then build and dispatch an actual populated settlement vessel and establish,
save and reload it. Do not fabricate a planet or grant eligibility. Native graphics,
release sealing and sustained 60 FPS remain incomplete.
