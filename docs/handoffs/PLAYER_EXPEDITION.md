# Ordinary Player Expedition Handoff

Latest priority (2026-09-11): see [Visual finish acceptance](VISUAL_FINISH.md) for the current
combined checkpoint and remaining release gates. Source `08943fe` includes the reviewed
map/star/vessel repairs, metric presentation and responsive fleet/route feedback. Its full
native run passed with 35 captures and clean runtime logs; validator `061c6bf` requires
133 real-input checks including visible metric route feedback. The valid focused aged-save
receipt at `ee147a3` contains seven native 2560x1440 samples at roughly 59–60 FPS; it is
short-sample evidence, not long-session acceptance. The fresh ordinary Player journey,
package validation and voice listening review remain pending. Main is unchanged.

Status: the ordinary Player expedition remains pending final native acceptance. PR #312 targets
`integration`; `main` is unchanged. The historical combined source milestone `cf95654` recorded civilian
Hold/Resume/Return, readable project cards, caption-safe drawer bounds, and corrected test controls. Publication
and acceptance must be checked against the live PR head, not inferred from earlier receipts.

At `3286b78`, the full native suite passed 144 input checks and 35 screenshots, actual exit 0,
empty stderr, and the strict exact-SHA screenshot validator. All five hosted gates passed:
build `34588412234`, research `34588412232`, voice `34588412208`, Windows `34588412200`, and
screenshots `34588412193`. Those gates did not establish completion of the fresh Player journey.

The fresh `3286b78` journey failed cleanly at its bounded 22-minute deadline after the first-warp
checkpoint. The test reselected an already-selected paused 8x item without resuming the clock;
reconnaissance therefore never advanced. Source `842edb7` (integrated as `278592f`) uses the
visible pause button when necessary and verifies active 8x; its focused controls proof exited 0
with empty stderr and observed advancing days after paused-8x reselection. The equivalent
Developer 24x helper is corrected in `90bb245`. These are test-control repairs, not altered
production time or waived failures. The local runner also now preserves Unicode output without
overwriting native exit status; forced-cp1252 probes returned the actual 0 and 7 codes.

The historical a19 travel failure came from a paused selector displaying 1x while remembered
Developer speed was 24x, plus a fixed-frame test assumption. The focused route/fuel proof fixed
that behavior through `fd86063` → `08e8371`.

At `8235a121`, the full run reached the new shipyard Load check but selected the Player-only
menu button. The reviewed fixture commits `0c4506a` and `51e7248` route Developer mode through
the visible Load Developer save control. Their focused production proof passed 19 checks with
exit 0 and empty stderr.

Pure validation belongs to source `d7460f9`: CoreRuntime 79/79, Simulation 70/70, Quality
19/19, Logistics 4/4, Species checks passed and Python 32/32, all exit 0. At a19, the Debug
build passed with 0 warnings/errors, Python checks were 36/36, import exited 0, and the focused
checkpoint exited 0 with 20 checks, 3 images, route-recovery completion and empty stderr. The
focused early UI proof from `122f268` integrated by `d5dcc53` exits 1 as intended; the late
failure proof from `69805fa` also exits 1 with the source save unchanged. These receipts are
separate from combined Player acceptance.

The maintained pure Player case uses the actual `CampaignSessionService.CreateNew("20260908")`
bootstrap, including the 100-system Barred Spiral profile, Adaptive Research and diplomacy.
It reaches a first extrasolar colony through paid research, construction, physical ships,
reconnaissance, detailed surveys and authoritative settlement. This proves deterministic
simulation progression, not the visible Player journey. The native acceptance path remains:
research → construction → physical scout/science/colony orders → named-star travel →
reconnaissance and detailed survey → named-world settlement → save/reload with the same
people, ship identities, simulation day and application revision.

Generation placement failures were reproduced with seeds `1789000000017` and `1789000000154`.
Separately, a save/load regression for seed `SOL-ASCENDANT-42` lost the physical conditions of
20 guarantee-altered bodies. Galaxy
catalog v16, campaign v17 and fresh-home fallback v2 are implemented and reviewed; old saves
retain legacy reconstruction. These changes passed the `3286b78` full suite; the new combined
successor still requires its own native and hosted evidence.

The combined source builds without warnings/errors; CoreRuntime passed 80/80 and Simulation
70/70. Civilian return uses physical routes and actual fuel, preserves passengers, and previews
the loss of paid establishment work before confirmation. Losing an owned base must fail safely.
The maintained native focus `civilian-recovery` is explicitly Developer-labelled; it cannot be
substituted for ordinary Player progression. Project cards use a separate artwork band and an
opaque, measured information area with cost/action containment assertions.

Run short `player-expedition-controls`, `civilian-recovery`, `project-card-stability`, and
`production` checks before another long journey. Then freeze the source for full native capture,
fresh schema-3 Player progression, exact-head hosted gates, and final Windows package verification.
No release acceptance, fresh-journey success, or human pacing/fun approval is claimed yet.

Short native evidence at `02e0cdf`: Player controls (six manifest checks), project-card stability,
and production (19 checks, two 720p images) exited 0 with empty stderr. Visual review confirmed
readable card costs/actions but found caption occlusion. Caption changes `948bc92`, `8bed928`,
and `2c8d9a6` are now integrated with deterministic visible-caption probes; native confirmation
at the combined successor is still required.

Civilian recovery exposed several test-state assumptions. Hold at a system correctly prevents
departure; an in-flight Hold instead preserves the final destination on arrival. The route read
model counts that retained zero-distance destination as one entry. The corrected scout path
passed Hold/Resume, physical Return, fuel use and refuelling at `f692baa`. Its later settlement
probe missed progress while using 24x; `e476809` then compared an immutable snapshot across a
speed helper that intentionally advanced time. `2fb1984` configures 1x before paid authorization,
then observes and pauses actual settlement progress. Failed proofs remain retained, with actual
exit 1; neither complete civilian acceptance nor a production-rule change is inferred from them.

The next economy milestone is [PIONEER_FOUNDATION.md](PIONEER_FOUNDATION.md). Existing colony
ships carry 250M modeled founders and outposts 8M, with oversized implicit support. The design
target is 10,000 founders / 250 outpost personnel with paid, staffed services and finite supplies,
preserving existing people and saves. This remains unimplemented balance work.

The focused native run must use ordinary pointer controls and a canonical checkpoint, with no
Developer grants or hidden targeting. Use the pinned Godot 4.7.2 GUI under an offscreen
display, `--audio-driver Dummy`, an isolated writable profile, and the maintained validator:

```sh
STELLAR_CAPTURE_FOCUS=player-expedition \
STELLAR_SCREENSHOT_DIR="$PWD/player-expedition-artifacts" \
xvfb-run -a -s "-screen 0 3840x2160x24" godot \
  --audio-driver Dummy --path . --resolution 1280x720 \
  --rendering-method gl_compatibility tools/ScreenshotCapture.tscn
python3 scripts/validate_player_expedition_capture.py \
  player-expedition-artifacts --expected-sha "$EXPECTED_SHA"
```

On Windows, use the equivalent isolated `APPDATA`/`LOCALAPPDATA` profile. Dummy audio supports
capture determinism only; it does not establish real-audio playback. The native journey,
human pacing/fun review and production-quality content review remain open.

The bundled main score is the user-provided `claimed-by-the-void-loop.mp3`, whose original
SHA-256 is `25C81BEE74C37DC91F0895FA68DB72B026C028C65D951634D07CD4AE0B325FA2`.
The `Dummy` audio driver is appropriate for UI/capture determinism only and cannot support
a claim about actual audio playback or mixing.
