# Alpha Playthrough Handoff

## Current checkpoint

PR #314 on `work/alpha-playthrough-integration` combines the visual expedition source `dd18ea96`,
the diplomacy source `4f368634` and the combat source `086c5c5f` (combined foundation `7e84f29d`). The runtime includes the
mouse-driven alpha map, adaptive research and construction timing, diplomacy workspace,
colony surface and the current combat presentation.

## Native validation complete; hosted release gate pending

The final tested production source is `c98010f1`; this checkpoint records its tested capture
fixture updates. The generic suite now uses actual left-click Play/Pause and the separate
surface speed button. The standalone combat runner explicitly requests and proves native
720p before capturing, then separately proves 1080p. It does not resize captured images.

| Receipt | Result |
| --- | --- |
| `work/alpha-loading-contexts-41f5d6c-r2` | Startup, generation and save art; three 720p images; stable tips; correct restoration; exit 0, empty stderr |
| `work/alpha-playback-41f5d6c` | Separate Play/Pause and speed, preserved resume rate, Player/Developer gate; exit 0, empty stderr |
| `work/alpha-player-resume-c3ea0dc-r4` | Hash-verified ordinary Player continuation through shipbuilding, survey, timed colony founding and real save/reload; 16 checks, exit 0, empty stderr |
| `work/alpha-full-c3ea0dc-r3` | 35 captures, 141 strict input checks at 720p/1080p/1440p/4K; exit 0, empty stderr; source delta recorded and committed here |
| `work/alpha-massive-menu-c98010f` | Combat menu/pause/rate and reload return; exit 0, empty stderr |
| `work/alpha-massive-100k-c98010f-r2` | Real 50k-vs-50k inventory, conservation, masked intelligence, selection/orders, playback, LOD and ceasefire; 20 checks, six native images, exit 0, empty stderr |

The dedicated 100k sample recorded 240 frames averaging 16.65 ms (p95 16.79 ms,
maximum 32.25 ms). This is a short deterministic renderer sample, not a broad campaign
performance guarantee. Dense 720p tactical labels overlap and ship close-ups remain
schematic; presentation polish and pacing remain Alpha work. All captures and source
deltas are retained locally. The final PR body supplies hosted run and package provenance
after CI completes; historical failures below are retained for diagnosis, not current blockers.

## CPU evidence

The Release game build completed with zero warnings and zero errors. Maintained validation
projects completed successfully: Simulation 71/71, CoreRuntime, Quality, DiplomacyWorkspace,
MassiveCombat and MassiveCombat.Persistence. The exact logs are under
`work/alpha-integration-validation/`.

## Remaining receipts

The first combined native run at `fe6ef1bb` passed 35 screenshots and 141 input checks,
including the audio resource GC stress and autosave artwork check. Its tactical menu focus
also passed. The ordinary Player opening exposed territory triangulation errors and search
centering a description match instead of the exact technology title. Exact-title selection
was repaired at `ee71b14b` and passed native pointer/search checks at 720p and 1080p.

Removing duplicate boundary vertices was insufficient: the next ordinary Player attempt
still produced native triangulation errors. The preserved autosave isolated a valid convex
triangle with coordinates `(-18.147076,-55.087666)`, `(-18.150757,-55.083984)`,
`(-18.15281,-55.086037)` and area approximately `7.56e-6` world units squared. Godot's
general polygon triangulator rejected this nonzero sliver. `2fcfc292` replaces repeated
general triangulation with one cached explicit triangle-fan mesh per civilization; it
preserves the geometry instead of suppressing or discarding errors. `c45d3ed7` disposes
the cached meshes on refresh and scene exit. Exact failed-save native replay passed with
exit 0 and empty stderr (`work/territory-replay-mesh-ee71/`); the fresh complete Player
opening must still pass before the download can be called playthrough-ready.

The follow-up also makes the renderer and gate-click handler share canonical travel-edge
validation: known or nearby unconnected systems cannot create arrows. Hosted diplomacy
capture now receives an explicit requested resolution from its runner, restores the native
window after production fullscreen initialization, and asserts both window and image size
before capturing the flow. Earlier runs inferred 1920x1080 even for requested 1280x720;
their successful Windows packages remain held because the screenshot gate failed.
The production game continues to start fullscreen. The earlier Windows candidate passed
startup but is superseded by these follow-up repairs.

The user-supplied Earth-orbit splash is now a dedicated loading asset. Its baked 61% bar
was removed so the runtime can show actual resource readiness with a smoothly paced
minimum seven-second presentation. The main-menu backdrop is retained. Startup and
campaign transitions are covered; autosaves never invoke the loading screen. Artwork
provenance and exact edit prompt are in `docs/art/LOADING_SPLASH_PROVENANCE.md`.

The subsequent user requests add separate New Galaxy and Load Saved Game artwork and
a stable random beginner tip for each loading transition. New generation and manual save
restoration prepare plain-C# campaign data on a worker, report completed stages, then
apply the prepared world on the main thread. Generation callback/noncallback hashes are
identical; corrupt-primary/valid-backup restoration stays monotonic and leaves current
state and save bytes untouched. CoreRuntime now passes 84/84. Existing new-campaign
checkpoint failure reporting remains in force: a ready world can open with a save warning;
the progress bar does not promise successful disk persistence.

Play/Pause is now a single toggle and speed selection has its own button. Choosing a rate
while paused preserves pause; Play resumes the selected rate. Both strategic/surface and
tactical controls use this arrangement and preserve their existing allowed rates.

Native evidence at `e0910bd4`: startup loading passed with empty stderr; diplomacy 720p
passed 23 screenshots and 121 checks. Build, research, voice and Windows hosted gates
passed. Generic capture exposed one test still expecting an instant Developer transition;
it now waits for the real timed loading completion. The ordinary Player run completed warp
research and built its first warp scout over about 18 minutes without territory errors,
then the Recent Events panel covered the science-vessel button. The maintained route now
closes that panel through its real Close control. A checkpoint continuation verifies the
preserved autosave SHA-256 `0A563BFCC8FF59968EF65E773B023B7A2CCD42E4021B232C967628BD2C7E4DE1`
before continuing normal Player shipbuilding, survey, settlement and reload. This prefix
is attributed to `e0910bd4`; the continuation and new loading/playback captures need their
own exact-source receipts before release.

Hosted CI packaging, native capture and the final integrated playthrough still need their
source-matched receipts. The current evidence does not claim Windows release approval,
native visual acceptance or a complete player expedition. Review remains open for pacing,
art polish, accessibility and the breadth of connected diplomacy/combat outcomes.

The `41f5d6cd` native loading-context capture passed with three distinct 720p images,
stable tips, real campaign identity/save-byte restoration, and empty stderr. The playback
focus passed pause/resume, independently selected speed and the Player/Developer rate gate.
Its startup-caption assertion needed whitespace normalization for the intentionally spaced
title. Hosted Build, Research and Voice passed; Windows detected two pending texture RIDs
when its smoke quit on the first frame, and generic capture clicked a star covered by the
First Light guide. Neither failure was waived. Startup smoke now waits for all requested
resources to be retrieved before normal shutdown; early/late injected failures skip those
requests and late-failure validation suppresses startup persistence. Healthy startup exits
0, injected failures exit 1, and all three cases report no texture leak. The generic click
fixture now chooses a star outside actual visible button bounds.

The preserved Player checkpoint predates the scout completion; it contains mature warp
research and the warp test facility. Continuation verifies the original hash and ordinary
Player state, then builds the ships through normal controls. It reached scout/science survey,
colony-ship travel and arrival, exposing an actual interaction defect: the idle fleet icon
swallowed right-click orders on the planet beneath it. Fleet buttons now retain normal
left selection/double-click focus while passing unhandled right-clicks to the orbital canvas.
The corrected settlement/reload suffix, full generic native capture and final hosted gates
remain required before merging and distributing this batched repair.

The corrected Player continuation subsequently passed on `c3ea0dc6` plus its recorded
fixture delta (`work/alpha-player-resume-c3ea0dc-r4/`): exit 0, empty stderr, 16 checks.
It waited for both colony founding and ship-consumption cleanup, then allowed the existing
0.1% population tolerance symmetrically for the immediate demographic tick (249.995733M
observed from 250M embarked). Ownership, body identity, exact colony count, consumed ship,
canonical settlement timer and real saved-state restoration all passed. The fixture changes
are part of this checkpoint; no simulation rules were changed for acceptance. Full generic
and tactical native captures plus hosted gates are still pending at this documentation point.

Historical project-state entries below the current checkpoint remain historical and should
not override this source or its pending gates.
