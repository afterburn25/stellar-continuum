# Visual finish acceptance

User priority, 2026-09-11: continue implementation of the visual finish. This takes precedence over further pioneer-economy expansion. Existing simulation ownership, save compatibility, and the older-campaign performance repair remain required.

## Current milestone (September 11, source `b09bbf7e`)

This is a pre-release coordination checkpoint. Version metadata remains `0.1.0-alpha` /
`0.1.0 Alpha` (`c708210`). Current maintained receipts are CoreRuntime 81/81 at
`work/core-secret-fog-final.log`, Simulation 71/71 at `work/simulation-v4-final.log`, and
Quality 20/20 at `work/quality-v4-repaired.log`, all with exit 0. The product build also exits
0 with no warnings. Galaxy-v4 retains 100 ordinary systems and a separate non-routable central
SMBH for new saves; old positions and metadata remain unchanged, and future landmark visits are
not implemented. The landmark is now observer-specific secret knowledge: access alone does not
reveal it, exploration requires access, other observers remain isolated, and both knowledge
states persist. Until discovery, public map APIs expose no metadata or position, center clicks
do not select or order, and the map draws no core-centred mask, icon, label or tooltip.

The exact `e6f6c47a` visible run at `work/full-release-final-e6f6c47a` exited 0 with empty
stderr, 35 PNGs, 157 runtime pass markers and all 140 required real-input checks. Strict
validation passed at the full SHA. It includes actual 1920x1080, 2560x1440, 3840x2160 and
1280x720 captures, real timed fleet travel, surface construction and save/load, research
drag/zoom/search/tab/privacy/Begin/Pause/Resume checks, and a scheduled 720-day Developer
autosave whose every rendered frame kept startup artwork hidden. Its separate genuine Godot
editor import exited 0 in `godot-import.log`; gameplay output is retained accurately as
`godot-capture.log`. This receipt is superseded for final visual acceptance by the one-line
hidden-fog privacy correction `b09bbf7e`: the reviewed `e6f6c47a` galaxy frame still showed a
conspicuous circular fog mask, so a new focused or cumulative image is required before claiming
the secret is visually hidden. No new download is claimed.

Earlier input failures were fixture issues: Construction clicked a sidebar while fullscreen
Research correctly blocked it, fixed by `f105` Close; First Light requested a strip in a system
where it is intentionally hidden, fixed by `1e91` Home; and the notification check assumed a
hardcoded research title instead of the selected authoritative title, fixed by `acffd`. Do not
describe these as desktop-mouse displacement. Remaining release gates include the new hidden-core
visual receipt, a fresh ordinary Player journey, packaged Windows validation, voice audibility,
and broader long-session performance review. The existing aged 1440p sample held 58–60 FPS with
p95 near 16.8 ms, but it is not a long-session guarantee.

## Historical combined checkpoint (September 11, source `08943fe`, validator `061c6bf`)

Reviewed map, star, vessel, clock, planet and metric repairs are integrated. Primary physical
readouts use metres/kilometres, kilograms, m/s², kelvin and kPa. Interstellar lengths and
speeds show kilometres first, retaining ly/pc as secondary astronomical context. Route
confirmations and range/fuel denials now share the same pure unit formatter as the UI.
Long fleet quantities stack and wrap at 720p; command feedback wraps rather than truncating.
Simulation values, saved state and the original Earth photograph are unchanged.

Accepted source-specific receipts:

- `ee147a3`: clean combined map evidence at
  `work/map-evidence-ee147a390ee2e1684f153ef422a2f86ef074ab78`.
- `ee147a3`: `work/combined-ee147a390ee2-aged-1440`, seven native 2560x1440 captures,
  roughly 59–60 FPS and p95 frame times near 16.8 ms. One planet sample reached 83.97 ms.
  These are short steady samples of the preserved old save, not long-session acceptance.
- `c88bee9`: `work/combined-full-c88bee9d23a04e72e2b36fb0643f76ce67bfda77`, 35 captures,
  132 required input checks, exit 0 and clean logs. Strict validation passed after
  `c04004d` synchronized two renamed checks with their actual runtime assertions.
- `43a4a26` production metric code: build, CoreRuntime 81/81 and Quality 20/20 passed
  in `work/metric-validation-final`. The subsequent build and Simulation 71/71 passed at
  `08943fe`; only test assertions changed after the production metric commit.
- `08943fe`: `work/full-metric-route-08943fe`, 35 native captures, actual exit 0 and empty
  stderr. Root reviewed the complete metric route notification and reachable inspector at
  720p. The full flow also exercises timed ship travel, surface construction/upgrades,
  save/load and responsive 720p/1080p/1440p/4K input. Validator `061c6bf` requires the new
  metric proof; strict validation passed all 35 images and 133 required input checks.
  A real headless editor import at `061c6bf` exited 0; its separate `godot-import.log` and
  `import-exit-code.txt` are in the same proof directory. Its maintained evidence tests
  passed 36/36 (`work/metric-evidence-validation-061c6bf.log`).
- Historical focused receipts: planetary alpha/readback at `ced6057`; Settings,
  fullscreen, actual save-and-quit and four-resolution layout at `8f514c5`.

Rejected evidence remains available:
The earlier `a1a0371` performance receipt is invalid despite exit 0 because runtime errors
from the missing dust import included 447 `ERROR:` lines and 443 null references. The
`accf7f9` map receipt also exited 0 with 145 error lines, and its full run stopped on a
`GalaxyCloudRenderer` null reference. The source/import and atomic renderer repairs are now
integrated at `ee147a3`; these historical receipts do not certify the current head.

The full `ee147a3` run found a stale surface pause label, fixed by `272bc10`. The next full
run found the fleet inspector expanding beyond 720p, fixed by `c88bee9`. Standalone metric
test setups lacked a scout and are not accepted evidence; the metric assertion now uses
the maintained full fleet-order sequence. The interrupted first `43a4a26` full run has no
valid completion receipt. None of these failures was waived.

Remaining release gates: a fresh ordinary Player journey, packaged Windows validation and
listening review. Maintain older-save and responsive checks
when further rendering changes land. Colony dressing and some vessel geometry remain
stylized; passing functional tests does not establish the requested photoreal production finish.

Current user display contract: start fullscreen every time, expose no player windowed mode
or title-bar X, and group Audio, Video, Voice/subtitles and existing control help under one
main-menu Settings entry. Explicit Exit to Windows uses the established save/shutdown flow.
Test-only window resizing must not become a production startup option.

## Evidence and direction

Historical baseline: `f83128e` source and rendered set `3286b78`. The following weaknesses
guided the subsequent changes; current acceptance evidence is listed above:

- Galaxy: narrow, evenly spaced spiral stripes read as a diagram. Replace this with a coherent luminous mass, irregular dust lanes, a central bulge and softer broken arms. Keep the existing generated system coordinates, recognizable stellar colors, full-frame distant background and clear selection.
- Colony: washed-out lighting and a sparse radial arrangement read as a model on a paved disc. Improve material contrast, coherent urban blocks, facade depth, street detail and the surrounding landscape. Preserve buildable land, placement validity, actual building state and bounded scene cost.
- Interface: large flat panels and stacked instruction strips compete with the world. Use compact, consistent hierarchy, aligned grouped facts, deliberate art placement and legible actions. Costs and outcomes must remain visible before commitment; all existing actions retain their input targets and recovery behavior.

## Scope and ownership

1. Caption/UI owner repairs settled caption measurement and prevents drawer collapse. A passing build alone is insufficient: actual active captions, readable full text and reachable controls at 720p/1080p are required.
2. Map owner improves galaxy and restored 2D system presentation. Earth keeps the requested photograph. System/planet skies contain stars and their own deterministic local scenery; external background galaxies remain galaxy-view-only.
3. Surface owner improves lighting, materials and settlement dressing, including roads. Dense urban dressing belongs to developed homeworlds; uninhabited worlds and small colonies must not receive invented cities. Cosmetic objects must not conceal real placements or act as gameplay structures.
4. Core reviews screenshots, interaction and performance together, then integrates a coherent candidate. No owner edits authoritative economy/research rules to make a visual check pass.

## Acceptance evidence

- Primary composition: 1920x1080. At 1280x720 controls reflow, remain readable and fit their scroll view; at 1440p/4K world rendering retains detail. Do not fix fit by shrinking essential text.
- Review full galaxy, regional stars, 2D Sol orbits, Earth/Saturn focus, colony overview/street, building selection/placement, research/ship cards and campaign confirmation. Compare before/after from the running game, not mockups alone.
- Preserve left-drag pan, middle-drag look, wheel navigation, right-click fleet orders, survey visibility, moons/stations and reversible save/load flows.
- Record exact source revision, actual process exit, shader/runtime errors, screenshots and focused interaction results. Import source assets in new Godot worktrees before native capture. Keep one native GPU job at a time.
- Recheck old-save navigation and surface performance after rendering changes. Avoid per-frame model generation, unbounded particle/mesh counts or scene rebuilding caused by ordinary selection.
- Run the relevant combined capture and package checks before offering a new download. Earlier revision receipts do not certify the new candidate.

Production finish is a visual acceptance requirement, not a label awarded by a passing test count. Original custom meshes, animation and environmental art may still require further work after this pass; record visible remaining shortcomings honestly.

## September 11 reference additions and integration contract

- Galaxy stars: small bright core, clear spectral color and fine rays; genuine binary/triple groups. Local stars: detailed photosphere, hot rim and restrained irregular corona. Planet materials retain terrain and day/night depth. References are visual direction, not shipped assets.
- Empire space: continuous exterior borders and stable colors, readable names, sensible enclaves; disputed claims remain distinct from actual ownership. Full overview and regional views must both work. Fog reflects observer knowledge and must never reveal foreign territory, labels, claims or strategic facts prematurely.
- System lane arrows: only actual lane neighbors, placed in their catalog directions. Left click changes the viewed system without issuing a ship order or granting survey knowledge. They are also warp entry points: ships arrive on the origin-facing edge, traverse each intermediate system to its onward exit, then warp. This requires timed saved simulation phases, honest ETA and input proof, not a presentation-only animation.

### Stellar catalog implementation

New `galaxy-v3` campaigns retain the same primary spectral quotas, positions and planetary generation, and add persisted optional secondary/tertiary stellar classes. An independent seeded stream assigns approximately 20% binary and 5% triple systems among eligible ordinary non-authored stars. These are initial game tuning, not an astronomical population claim. Sol remains single; remnants and protostars retain their existing single model.

Save fields are optional additive catalog data in the existing envelope. Missing fields in older saves stay absent; loading never regenerates companions. Invalid enum values, incomplete A/B/C configurations and companions on canonical Sol fail with a system-specific terminal diagnostic. Companions are schematic stellar catalog detail; no new multi-body gravitation, stellar evolution or orbital stability model is implied. Detailed system projections retain the existing full-survey gate.

Validation: `dotnet run --project tests/Game.CoreRuntime.Validation/Game.CoreRuntime.Validation.csproj --no-restore` exited 0, 81/81 passed, including seeded multiplicity, unchanged planets, old-save defaults, save round trip, fog-safe projection and malformed catalog rejection. Log: local `work/stellar-companion-validation.log`. Native companion rendering is pending the map owner's implementation.

### Combined review checkpoints

- Core `ada8363`: imports/build succeed; maintained immersive focus exits 0 with empty stderr, nine captures and actual rotation, descent, atmosphere, surface and recovery checks. `work/visual-surface-ada8363` is the exact local receipt. Roads, colony density and output grouping improve, but the reviewed street image still has primitive tower silhouettes, simple facade depth and hard shadows. This is not photoreal production acceptance. Original NASA Earth brightness is preserved; an unregistered spherical night-map overlay was rejected, and airless worlds cannot gain cloud veils.
- Core `af3894c`: responsive focus exits 0 with empty stderr and native PNGs at 1280x720, 1920x1080, 2560x1440 and 3840x2160. Caption/drawer clearance passes at 720p and 1080p; research reflows and map clicks remain aligned at each size. `work/visual-responsive-af3894c` contains exact window/scale/texture diagnostics. An earlier resize race failed cleanly and is retained at `work/visual-responsive-ada8363`; expected resolution checks were not weakened.
- Isolated map `bed3fb1`: post-import galaxy capture exits 0, empty stderr. Root reviewed the more irregular dust mass and spectral cores as a visible improvement. Local companion, gates and green fleet-marker evidence is still required before combined acceptance.
- Territory is under further review for civilization ID zero, sparse/extreme grid bounds, disconnected holdings, ownership versus claims, fog edges and performance. No unreviewed territory implementation is integrated at this checkpoint.
- Local gate transit is under further review for intermediate sensors/refueling, queued returns, exact ETA/progress, malformed saves and partial-time partition tests. No unreviewed travel implementation is integrated at this checkpoint.

Own ships and fleets must now use clearly visible green map markers, with role silhouettes, grouped counts when appropriate, dark backing over bright nebulae and a distinct selection outline. Their local and strategic positions must reflect the same saved travel state used by simulation. Foreign markers must not be mislabeled as player-owned.

## Historical September 11 milestone update

This is a coordination checkpoint, not release acceptance. `main` remains unchanged. The tracked
release metadata is `0.1.0-alpha` / `0.1.0 Alpha` from `c708210`.

- CoreRuntime is complete at 81/81 after malformed-core coverage was repaired; the exact
  `galactic_core` receipt remains with that owner. Galaxy generator `1452730` plus `0eed113`
  adds optional persisted galaxy-v4 data: 100 ordinary systems and a separate non-routable
  central SMBH. Future SMBH visits are not implemented, and old-save positions remain unchanged.
- Research source `3d61f90` provides fullscreen tabs, search, two-axis drag, wheel navigation and
  lock previews for public ordinary future possibilities. `fdc` adds input shielding/edge culling
  and `041d` improves word wrapping. Unknown future possibilities remain anonymous; hidden
  hypotheses, private IDs and undisclosed details are not exposed. Known cash/lab shortfalls retain
  their names and requirements. `work/research-workspace-v3` is a reliable focused native receipt
  at 720p/1080p with functional evidence for exact authorization-plus-reserve charging and
  pause/resume. Its duplicate 720p frame is not distinct visual evidence.
- The strict UTF-8/no-replacement-character correction was established at `3d61f90`, not the
  historical `c39bf6c`. Locked nodes are selectable only to show a generic locked inspector and
  cannot be started; they are not noninteractive. The research workspace remains functionally
  passing but not full visual acceptance.
- Gate presentation latest `fc27c43` uses dark reference `#05250f`, a 32x34 canvas triangle,
  sampled muted edge, base labels and orange hover. Source `c8e18474` matches the warm Sun tone
  in the 2D orbit. Sun work `f818`/`791`/`e7f9368` has functional spectral and eruption behavior;
  quiet-burst proof `9fa` covers 18 samples over at least 35 seconds. The creamy art was rejected;
  native final evidence remains pending.
- Frozen source `125bff0` validated photosphere and close zoom. `cdd6293` supplies colored
  navigation and brighter deep-field rendering. Territory `b1cccbb` adds continuous dominance
  outlines; the earlier `946664d` CPU result alone did not establish filled territory acceptance.
  Native close/regional evidence and the final map-owner receipt remain pending.
- Camera source `ec94e3fa` includes observer-core behavior and actual 4K evidence at `4310bc3`.
  Visible-window restoration is covered by `215ae5` and `4310`; this is the user-watch path and
  should not be replaced with an offscreen-only claim.
- Window lifecycle hardening `800b803`/`d1b4830` has a clean `work/window-lifecycle-v3` receipt
  (exit 0, empty stderr), covering WM-close to paused menu, explicit Exit, preserved view/speed/
  process/input, and recovery timings of galaxy 24 ms, system 19 ms and surface 106 ms. It is
  windowed/offscreen lifecycle evidence, not a fullscreen-driver crash guarantee; no actual user
  crash was confirmed. Save/autosave flash guard `4844ade` and per-frame checks remain pending
  final native review. Voice CPU coverage is 12/12, with the Chief Scientist cue on an eight-second
  cooldown.
- Full 35-PNG strict validation, final 4K regional/sun/research-category evidence, ordinary Player
  journey, Windows package validation, save/autosave acceptance and voice audibility remain
  pending. No photoreal or production-finish claim is made, and no new download is claimed.

Do not infer final visual-finish acceptance or release completion from this checkpoint.

### Ordinary capture startup-window mitigation

`project.godot` starts fullscreen, while the maintained `work/run-polish-check.py`
now selects `--windowed` before process birth for ordinary ScreenshotCapture work and
retains production mode for the dedicated `startup-fullscreen` focus. This prevents a
boot splash from briefly occupying the user display before capture code runs. The
reported flash was not reproduced; this is a preventive tooling mitigation.
