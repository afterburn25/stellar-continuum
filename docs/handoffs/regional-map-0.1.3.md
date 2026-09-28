# 0.1.3 Alpha — regional star scale, approach and local sky

Base: integration `65ede7901f108c45cd883c3c821b5fde787d7df0`, accepted 0.1.2 (PR #316).

This was a downloadable preview. Its regional solar-disc art was superseded by the
user's point-flare reference before integration. See [0.1.4](regional-map-0.1.4.md).
Its five hosted gates passed on `64610be8`; that does not validate the subsequent
visual correction, which has its own receipts.

## Player changes

- The regional map previously capped catalogue symbols at 5.4 px radius, clamped zoom at
  3.2, and entered the previously selected system at 2.7 even when the pointer was over
  empty sky. Stars now have approximately 14.5 px photosphere radii at normal regional
  zoom and grow smoothly toward 72 px. Public stellar classes retain their spectral hues.
- Free regional zoom reaches 48 with the pointer as its anchor. At a close approach
  threshold of 18, hovering a reconnoitred star takes the player into its detailed
  photosphere view; prior selection is unnecessary. Further wheel zoom approaches the sun.
  Explicit Open System and double-click keep the ordinary 2D orbital route.
- Unexplored stars remain on the regional map at its safe maximum zoom. Observer filtering
  continues to redact body catalogues, real names and undiscovered hazards.
- Distant galaxy opacity follows the overview blend and reaches exactly zero in the
  regional map. The local background combines the existing nebula artwork with 356 seeded
  decorative stars, including three clusters. It fills the frame during deep pan/zoom.
- Detailed regional suns reuse the existing solar shader and spectral materials. A
  128-slot sprite pool reuses hidden slots, culls offscreen work, and resets on campaign
  replacement. Sprites disappear on system/surface entry. Background geometry is cached
  until seed or viewport size changes.
- Rendered stellar radii also determine selection bounds, labels and selection rings.
  The system-orbit, planet, travel-arrow, loading and music changes from 0.1.2 are retained.

## Maintained validation

`tools/ScreenshotCapture.RegionalMap.cs` adds the focused `regional-map` mode to the
existing native Godot capture scene. It uses actual mouse events at 1280×720 and
1920×1080 to check whole-galaxy exposure, local-sky replacement, star size/material
budget, empty-sky cursor anchoring to the limit, unknown-system privacy, known-star
approach without prior selection, deeper photosphere zoom and ordinary orbital entry.
The full camera journey also requires larger regional stars/local sky and the new
stellar approach, while retaining pan, resizing, input shielding and return-camera checks.

Local and hosted validation results, final source/tree identifiers and the self-contained
Windows package hash are recorded in the PR before validated integration. Generated
native images/logs stay under `work/`; source tests and this handoff remain maintained.

Local receipt `work/regional-map-native-02`: exit 0, empty stderr, 12 rendered captures
covering overview, region, free maximum zoom, unknown close approach, known approach and
the Sun close-up at both native resolutions. Region/approach/close-up images were visually
reviewed. Game build has zero warnings/errors; Quality passes 20/20 (pre-existing CA2014
warning in its test program); GodotSmokeChecks passes 39/39.
The complete native camera journey also passes in `work/regional-map-camera-01`,
exit 0 with empty stderr, covering pan/picking, resize, UI shielding, system/planet
focus, deeper regional approach, unknown privacy and restoring the prior camera.

## Scope limits

Map radii and zoom are presentation scales; this change does not alter physical travel
times, catalogue coordinates, survey rules, economy or save formats. It adds no scratch
executable, background retry process or new art dependency. Broader art/pacing work and
physical Earth–Mars transfers remain separate milestones.
