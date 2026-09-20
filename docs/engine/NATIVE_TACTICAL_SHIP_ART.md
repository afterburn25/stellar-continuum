# Native tactical ship artwork

The first native tactical hull is the human patrol corvette. The original
portrait remains in the fleet and shipyard panels; a new transparent top-down
derivative is used on the battlefield. This is 2D artwork in the existing
tactical workspace, not a completed solar-system battle scene or a 3D model.

## Authority and presentation

`bind_owned_battle_art` accepts the observer snapshot, matching active encounter
and observer-owned fleet view. It requires an exact owned formation, unique
bindings, the mapped campaign vessel identity and matching observed/canonical
`patrol_corvette` design. Fleet zero uses the existing native `2^32` mapping.
Foreign designs, unidentified contacts and aggregate cohorts cannot produce
human hull artwork. The host also requires the human `terran_baseline` player
species; alien-owned ships keep their markers until matching hulls exist. Unsupported ships retain the existing tactical markers.
The native-only zero-fleet save compatibility limit is documented in
`NATIVE_TACTICAL_WORKSPACE.md` and remains unchanged by this work.

The host refreshes bindings alongside observer snapshots at 10 Hz. A pure
render plan projects the observed position through the actual battle camera,
applies stable presentation offsets and derives heading from observed velocity
(or observed heading when stopped). These offsets are decorative formation
placement; Core does not provide individual-vessel physical positions.
At most 32 corvettes render. Zoom and UI scale produce bounded hull sizes;
ordinary markers continue to represent the full formation.

`NativeBattleSprites` shares one immutable 1254-square RGBA resource
(6,290,064 CPU bytes; 1,188,325 fully transparent pixels). It preserves alpha
and adds two clipped blue plumes only for observed movement. The plumes are a
motion cue, not a claim that thrust/acceleration is simulated per vessel.
Stationary sprites have no plume. There is no autonomous animation clock or
simulation mutation in the artwork layer.

`Image::rotation_degrees` adds clockwise rotation about the destination center
to the Engine draw command. Its zero default preserves existing aggregate
callers; nonfinite angles fail cleanly. SDL draws the rotated texture with the
same clip, tint, bounded cache and clip-restoration behavior as other images.
Ships are submitted above the field and below labels/controls.

Clicking a rendered hull selects its owned formation. Rotated hull hit regions
exclude transparent corners and plumes, expire on camera/viewport changes and
are cleared on campaign/battle exit. Existing order validation stays in Core.

## Artwork and export

`assets/visual/ships/patrol-corvette-tactical-v1.png` was generated with the
built-in image tool from the existing corvette design. The exact prompt is in
`NATIVE_TACTICAL_ART_PROMPT.md`. The source image is unchanged. Native packaging
lists this derivative explicitly and verifies its SHA-256 fingerprint, together
with the updated source note. No directory-wide or temporary-asset export was
introduced. The six-portrait thumbnail cache remains separate and unchanged.

## Evidence and remaining work

Focused tests cover binding secrecy/identity/duplicates, invalid geometry,
projection, culling, budgets, decoded alpha, resource reuse, rotated plumes,
stationary rendering, draw order and actual-hull interaction. The maintained
Vulkan replay has one native zero-ID corvette, one unsupported owned colony
ship, an inexact hostile formation and a completely hidden hostile picket.
It checks real hull selection, a Core order, pause/speed/menu behavior, F6 save
and full canonical paused reload equality except the save timestamp.

Each replay captures the live scene and a second frame with only the ship layer
removed. The validator reads either 24/32-bit BMP layout, requires changed hull
pixels inside the bounded rotated sprite area, unchanged transparent corners
and no changes elsewhere. Pixel comparison uses bounded byte buffers, not a
dictionary of millions of pixels. This supplements exact typed diagnostics;
counters alone are insufficient evidence.

Named evidence: `work/native-battle-sprite-build.log`,
`work/native-battle-sprite-ctest.log`, `work/native-battle-sprite-runtime.json`,
and `work/native-battle-sprite-neighbors.json`. Final results are recorded in
`../CPP_MIGRATION_HANDOFF.md`. The local package is unsealed. Other species and
hulls, actual orbital battle scenery, individual physical maneuvers and sustained
60 FPS under large battles remain open. No graphical parity or release claim.
