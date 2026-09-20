# Flare sharpness, timing and arrow labels — 2026-09-20

The image-derived emission volume visibly smeared the supplied fine filaments.
The previous tests checked visibility, occlusion and stability with uniform-color
textures, which did not verify retention of textured detail. Visual comparison
with the previous surface renderer confirmed the regression.

## ENGINE CAPABILITIES ADDED / EXTENDED

- Native eruptions use the existing Engine curved surface mesh and emissive
  material again, with one texture lookup per stage instead of depth integration.
  Surface attachment, random sites, measured photosphere occlusion, event identity
  and both map consumers remain shared. UV distortion is disabled. Stage transitions
  occupy the last fifth of each stage, avoiding continuous double exposure of
  independently authored images. High and Ultra use the prepared 1024-pixel tier.
- The shared Core runtime owns a separate persisted activity clock. CampaignFrame
  advances it once per unpaused strategic frame at one activity hour per real
  second, independent of strategic speed, substep count or backlog draining.
  Scheduler, both rendered views and developer event controls consume that same
  clock. Pausing or opening the menu stops activity; elapsed offline time is not
  added. The headless runtime exposes explicit unscaled advancement for callers
  without a CampaignFrame. CME launch records are returned by that advancement
  and by CampaignFrame, rather than accelerated strategic steps.
- Galaxy payloads optionally store `StellarActivityDay` for player and developer
  saves. Old saves initialize at their saved simulation epoch, preserving event
  identity, seed, counters and timing; new saves retain the independent clock.
  Negative, nonfinite, incorrectly typed and out-of-range times are rejected.
- Navigation names now run parallel to the broad arrow base, perpendicular to
  the tip direction. Rotation stays readable within a half-turn and collision
  bounds follow the rotated text. Names sit just behind the base. Canonical
  travel destinations and transit gates do not change.

The scheduler keeps its due-event heap and bounded histories. Rendering retains
four image jobs, six visible effects and bounded caches. High quality now uses
the existing Ultra texture cache budget; removal of ray marching reduces fragment
work. No imported source pixels, planet materials or Windows settings are changed.

## Validation

Validation includes the 420 class/type/variant cases at all four quality settings,
actual GPU captures for all five eruption families, a fine-strand contrast test
using the application's material and mesh, and eight-direction native arrow
captures. Geometry checks include sixteen bearings, crowded exits, collision
bounds, hit testing and orbit clearance. Timing checks compare equal real time
at 1x and 25x, event identities/counts/progress, save/load continuation, old-save
migration, malformed clocks and pause/menu behavior. All eleven selected suites
pass: stellar activity, campaign frame, foundation persistence, galaxy payload
persistence/JSON, legacy galaxy persistence, player campaign JSON, navigation,
developer simulation, eruption artwork and native navigation visuals. The frozen
player-save oracle still checks all legacy fields; new appearance/clock metadata
also passes a complete second load/capture without change.

The GPU fine-strand test measured bright bands at 240 and dark bands at 8,
retaining the supplied contrast. The full native Vulkan developer replay passes;
its Galaxy/System captures show the same event at 35%, then progression to about
67% without restarting on view changes. Actual Sol, A-class rising and system
navigation captures were visually inspected alongside the eight-direction sheet.
The replay's critical-diagnostics banner is a deliberately injected earlier QA
fault, not a flare crash.

The portable corrected executable also loaded a copy of the user's 1,000-system
developer campaign, entered a solar system and exercised maximum star zoom in
both views. Its source and included campaign files were not written by the replay.
The included copy remains in its own UserData/Developer folder. Packaged
`regressions.log`, `smoke-proof.json` and `download-load-proof.json` record results
and the verified executable hash.

## ENGINE LIMITATIONS REMAINING

Flares currently use artwork on curved 3D surfaces, not full volumetric plasma.
This deliberately restores source detail; the rejected thick-volume treatment
is no longer used for flares. Convincing full volume that preserves that detail
remains unfinished. Generic single-image families and inferred stage matching
remain. Moon orbits remain schematic. The user confirmed desktop glow is gone;
this was not independently established by changing or inspecting Windows settings.
