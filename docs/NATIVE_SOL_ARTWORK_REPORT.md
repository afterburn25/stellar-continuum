<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Solar-system artwork update

Mercury, Venus, Earth, Luna, Saturn, Uranus and Neptune join the accepted Jupiter
and Mars artwork in both system and 3D planetary views. Double-click a body in
Sol to open its planetary screen; drag to rotate and scroll to zoom. Developer
Game.cmd opens developer play, where Developer Controls can reveal Sol.

## ENGINE CAPABILITIES ADDED / EXTENDED

- The shared catalog selects all nine full surface maps. System discs and
  inspection portraits no longer crop old isolated Sol photographs.
- Earth uses the new cloud-free color map and its existing separate cloud and
  night layers. Its system disc composites the same cloud map. Night lighting
  retains existing population/infrastructure rules.
- Engine `annulus_mesh` supplies validated, immutable XZ ring geometry. Shared
  application ring bands feed both the ordered back/body/front system rendering
  and depth-tested, double-sided 3D rings. Saturn and Uranus have different profiles.
- Shared presentation poses align globe and system textures. Uranus is tilted;
  survey projection and picking account for roll. The opening camera fits rings.
- Canonical body identity and observer filtering still select artwork. Hidden
  worlds get neither authored maps nor rings. No simulation or save format changes.
- Eleven globe layers load lazily and stay below existing texture cache budgets;
  nine color maps total about 54 MiB decoded, plus Earth's cloud/night layers.
  The worker-produced system-disc cache retains its 16 MiB limit. Ring meshes
  are built once and reused; visual work does not scale with total galaxy size.

## ENGINE LIMITATIONS REMAINING

Far sides are inferred artwork; small baked relief shadows and polar/seam
imperfections can remain. These are color maps, not measured terrain, relief or
normal maps. Earth geography is visually aligned, not a scientific replacement
for the source map. Rings are an artistic band model with depth occlusion;
ring particles, cast ring shadows and scattering are not simulated. Fine ring
edges can alias at small sizes with the current single-sample renderer. Venus has
no UV mode. Other moons and procedural worlds keep their existing surfaces.

Initial globe decoding remains synchronous. Disc workers still decode source
maps per uncached lighting request. This artwork update does not change galaxy
size, late-game simulation, navigation or save-memory limits.

Validation: the native build passed with warnings treated as errors; 23 native
regression checks and all 70 export checks passed. Packaged developer replays at
1280x720 and 1920x1080 opened every Sol planet and Luna in the system and planetary
views, captured both sides after rotation and preserved the ordinary save anchors.
The inspection sequence also left canonical campaign state unchanged. Front and
far-side captures were reviewed for the seven newly supplied bodies.

Detailed validation results are recorded in SolarArtworkValidation.txt in the package.
Image generation used the built-in tool; prompts and source selections are in
SolarArtworkPrompts.json. The supplied originals and previous builds remain intact.
