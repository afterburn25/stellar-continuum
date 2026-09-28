# Cinematic maps and free-placement colonies

The selected art direction is B, Cinematic Strategy: blue and violet nebula detail,
warm stellar light, sharp silhouettes and readable graphical controls.

The Milky Way overview is original generated illustrative artwork. It shows the
complete barred spiral disc; it is not an external photograph, scientific star
catalog, or an assertion that all decorative stars are playable. The existing
regional catalog retains every physical position, travel distance and saved origin.
Sol occupies the local region marker in fresh campaigns; legacy regions retain their
identity. Zoom or use the breadcrumbs to move between the galaxy, local region,
surveyed system, and focused planet. Back returns one scale with the previous camera.

Fully surveyed canonical Sol bodies retain the original NASA textures documented
in SOL_VISUAL_SOURCES.md. GPU spherical lighting samples the original images at
planet-focus size. Generated map art does not replace the eight authoritative Sol
planets or expose undiscovered body data.

## Surface gameplay

Focus Earth (or another owned colony on a solid body), then choose Surface.
The game currently presents a freely navigable 3D colony area, 1,024 metres across. It is a
procedural landscape illustration, not a geographically reconstructed Earth site
or a full planet terrain-streaming implementation. Buildings have arbitrary valid
X/Z positions and rotations; no tiles or fixed construction slots are used.

Left drag moves the camera; middle drag looks around, WASD also moves, and the wheel changes
distance. Select a graphical building card, move the preview onto clear ground,
rotate with R, and click to place. Escape cancels a preview, then returns to orbit.
The header supplies Save, Pause and Return to orbit controls. The right-side panel groups colony facts and contains the construction palette and selected-building actions. At 720p it scrolls without shrinking text or covering the full width of the colony.

| Building | Industry cost | Completed effect |
| --- | ---: | --- |
| Power generator | 300 | +4 colony power |
| Science lab | 400 | +1 Effective Research Lab, requires 2 power |
| Fabricator | 450 | +1 industry/day, requires 2 power |
| Trade hub | 380 | +0.08 normalized treasury/day, requires 2 power |
| Habitat complex | 350 | -20% local life-support cost, requires 2 power |

The colony hub supplies 2 power. Only completed, enabled, staffed and powered buildings produce resources. Essential-service and player priority affect constrained allocation. Completed
generators supply power immediately; pending structures supply nothing. Existing
research-network and automation multipliers apply to the combined colony output.

Placement creates a real construction order and reserves its footprint. Industry
is spent as work progresses, at up to 30 industry per site per simulation day.
All surface sites and
the existing construction project share the established construction allocation;
shipbuilding retains its separate fair allocation. Insufficient industry slows
work. Pausing freezes it. Placement reserves the displayed monetary authorization; materials are consumed as construction advances.

The same authoritative function validates preview and placement: finite coordinates,
colony ownership, an exact solid body, terrain slope, boundary clearance, hub
clearance and building overlap. Capacity depends on body size and hub level, with a maximum of 64 modules. Initial capacity is lower.
Decorative rocks remain outside the buildable area. Incomplete sites can be cancelled
for half their monetary authorization; completed structures can be demolished without a
refund. Each base complex has one in-place advanced upgrade with explicit monetary and stored-material costs. Payment reserves the upgrade; the old facility remains operational until timed work completes. Hub expansions follow the same rule. All amounts display in the civilization's current currency. Three completed complexes of one functional family form a district
with a 25% matching output bonus. Habitat complexes can be upgraded to powered closed-loop
arcologies that reduce local life-support costs by 40%; total reduction is capped at 75%.
Decorative civic roads organize the established settlement. Functional player-built road networks and terrain editing remain future work.

The landscape palette follows the occupied world's canonical environment. Temperate,
frozen, hot, airless, oceanic, reducing-atmosphere and rocky colonies use distinct terrain,
exposed rock, sky, fog and sunlight colors. This is presentation only: every palette shares
the same authoritative heightfield, placement rules and saved coordinates.
The protected hub area also renders an established settlement cluster sized in bounded
population bands. Worlds requiring environmental mitigation use sealed habitat domes;
naturally supported worlds use open settlement towers. These structures visualize existing
population and never masquerade as player-placed production buildings or collision obstacles.

Positions, rotation, progress, pending upgrades and completion are persisted with the campaign. New optional work fields default safely when absent in older saves.
The loader rejects invalid geometry, duplicate IDs, unknown types, inconsistent
progress, and missing authoritative surface collections. Camera/preview state is
transient and closes when the campaign or focused owned colony changes.

Developer and Player use the same construction rules. Explicit Developer tools
can fund and finish orders in their separate campaign; see [GAME_MODES.md](GAME_MODES.md).

## Resolution, navigation and orbital construction

1920×1080 is the reference. At 1280×720 the research page becomes one column and operations cards reflow; text retains readable logical sizes and longer panels scroll. At 1440p/4K the reference UI composition scales while celestial scenes render at native resolution.

Left-drag pans galaxy and system maps. Wheel zoom moves from the fitted galaxy through a surveyed system into a focused planet. The right inspector organizes planets and moons. Select ship icons for live statistics and right-click stars to travel along supported routes. Travel consumes simulation time and fuel; scouting and surveying begin after arrival. Colony travel leaves passengers aboard until a specific planet receives a settlement order.

In the home system, orbital launch complexes, shipyards and asteroid resource networks have contextual build sites and original 3D models. Their panel shows requirements, monetary authorization, material cost, upkeep and minimum duration. Construction stages consume the same authoritative infrastructure progress used by the economy. These are currently civilization-level infrastructure projects anchored visually to the home world or asteroid region; independent per-colony orbital inventories and free placement are not implemented.

## Artwork provenance

The current galaxy layers are `deep-field-v2.png` and `milky-way-layer-v2.png`. Their original prompts are recorded in `CINEMATIC_ASSET_PROVENANCE.md`. System and planetary orbital backgrounds use deterministic local stars and nebulosity with no galaxy images. The older asset notes below are historical.


`assets/visual/space/milky-way-b.png` and `regional-nebula-b.png` were created with
the built-in image_gen tool for this project on 2026-09-08/09. They are original
generated raster artwork, informed by NASA's illustrative Milky Way references:
https://science.nasa.gov/resource/the-milky-way-galaxy/ and
https://apod.nasa.gov/apod/ap250513.html . They contain no third-party game assets.
The source images retain their embedded provenance metadata.

Galaxy brief: full barred spiral disc, warm bulge, blue-white/violet spiral arms,
fine dark dust lanes, restrained red emission clouds, black margins, no labels/UI.
Region brief: deep interstellar blue/violet cloud filaments and dark dust lanes,
low-density dark centre for interactive stars, no planets, hero stars or labels.
The user-approved four-panel direction board is a style reference only; generated
planet counts or layouts on that board do not enter the physical game catalog.

## Acceptance

Maintained model tests cover construction, output, ownership, malformed inputs,
shared industry and save compatibility. The Godot evidence driver uses actual
wheel, drag, button and ground-placement input, with snapshots of the resulting
camera and authoritative colony state. Full validation additionally requires
shader compilation, rendered screenshot review, all existing simulation and
Adaptive Research suites, and the native Windows export/startup gate on the exact
published source. A C# compile alone does not certify rendering.
