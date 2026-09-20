<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Giant worlds, separate rings and Pluto — 2026-09-20

The runtime now uses the reviewed replacement giant library and independently
generated ring identities. Emerald Cloud is disabled following the user's
instruction to exclude its blue/gray images that resemble Dark Cyclone. Original
source files are untouched. The portable package contains only approved current
planet maps, including the separate replacement Pluto material.

## Completion inventory

| Requested item | Implemented result |
|---|---|
| 1. Old classes replaced | Nine retired giant subclass mappings are removed from generation. The historical mappings and 131 asset records remain in `deprecated-giant-art-v1.json` for migration. Their 106 unique prepared materials are excluded from the new package; offline checkout copies are retained. |
| 2. Migration handling | Core `migrate_giant_appearance` deterministically selects approved replacement art using the saved seed and explicit subclass mapping. Existing ID, orbit, mass and radius stay intact. Migration is idempotent. |
| 3. Gas Giant subclasses | Cream Band, Amber Storm, Blue Mist and Dark Cyclone enabled. Emerald Cloud remains defined but disabled with an empty accepted pool. |
| 4. Ice Giant subclasses | Cyan Haze, Deep Azure, Frost Veil, Teal Storm and Dark Polar enabled. |
| 5–7. Giant audit counts | 269 inspected; 150 accepted; 119 rejected, including the entire 28-image Emerald pool. Every record includes source identity, hash, visual review and rejection/preparation information. |
| 8–9. Ring audit counts | Ten actual ring families, 190 images inspected; 162 accepted and 28 rejected. Every approved ring is mapped to a separate radial/angular material. |
| 10. Generation weights | Conditional weights remain within the existing Gas/Ice Giant base-class distribution; adding subclasses does not multiply giant prevalence. Details below. |
| 11. Orbital placement | Eligibility uses shared class mass, radius, pressure, atmosphere retention, temperature and orbital-zone rules. Giant history and snow-line metadata are tracked separately from image colors. |
| 12. Ice Giant restrictions | Ordinary Ice Giants favor positions beyond the luminosity-scaled snow line. The rare inward-migration branch records its exception; temperature/retention constraints still apply. Legacy suspicious inner worlds are exposed by diagnostics rather than silently inventing a formation history. |
| 13. Ring occurrence | Configurable 25% gas, 30% ice, 0.5% terrestrial, 1.5% super-Earth, 10% cracked, 15% mini-Neptune and 5% hot-Jupiter base chances, followed by bounded physical/history modifiers. |
| 14. Composition | Ten independently weighted families carry material density, reflectivity and composition; narrow/faint families dominate faint systems and catastrophe increases fragmented-family weight. |
| 15. Temperature | Periapsis irradiation is checked. Bright/broad exposed ice has a 175 K ceiling, mixed ice/dust 190 K; current refractory families 1800 K. Invalid forced choices fail before changing the developer world. |
| 16. 3D planets | Shared oblate sphere meshes, prepared albedo/normal/property maps, actual stellar directions/colors and up to three lights. A bounded rotating-fluid estimate supplies giant oblateness. No extra clouds are painted over the approved giant image. |
| 17. Axis and orientation | Recorded source band roll is corrected into planet texture coordinates. Saved tilt, node, phase and rotation direction determine the physical pose. Slow visual spin is independent of strategic speed; established tidal locks remain. |
| 18. 3D rings | Separate closed annular slabs with front/back surfaces and inner/outer edges, actual saved finite thickness, plane normal, radii, optical depth and material. Geometry is perspective-projected and depth-tested. |
| 19. Warping/morphing | Source ellipses are rectified into radial/angular coordinates in the C++ preparation pipeline. The runtime fits that material to bounded host-relative radii, preserving gaps and irregular occupancy. Stable rings stay planar; arbitrary live warping is intentionally absent. |
| 20. Scale fitting | Inner edge starts outside the planet; outer edge is at most 96% of the fluid Roche estimate and 4.5 host radii. Width depends on significance. Invalid geometry is rejected. |
| 21. Planet shadow on rings | Analytic oblate-ellipsoid occlusion follows the actual host pose and light direction. |
| 22. Ring shadow on planet | Analytic ray/annulus intersection samples the same ring opacity map; gaps transmit light and dense bands block more. |
| 23. System View | Shared canonical planet/ring assembly, with 64/128/512 ring segments at increasing LOD and bounded close material allocations. |
| 24. Planet Window | The same appearance record, material loader and assembly are used with the window's camera orientation, preserving identity and ring plane. |
| 25. Save migration | Optional versioned giant/ring metadata round-trips in the existing planet appearance payload; old art maps through the frozen migration table. Known Sol identities are preserved. |
| 26. QA tools | Developer Planet Index opens the Giant Planet & Ring Test Panel. It changes subclass, image, rings, ring family/variant, tilt, camera, star direction/spectrum and distance, and can isolate each mutual shadow. The laboratory uses Core commands and a persisted developer record, not gameplay-body mutations. |
| 27. Validation | Eleven final focused CTest suites passed; 15 packaging tests passed. See validation below. |
| 28. Regeneration requests | Supply a genuinely green Emerald pool if that subclass is wanted. Frost Veil has only six accepted images; it would benefit most from additional clean, complete discs. Replace rejected cropped, baked-ring, heavy-shadow or malformed sphere images using their per-file reasons. No new images are required for the enabled classes to work. |

## Approved pools

| Giant subclass | Accepted | Rejected | Conditional configured weight |
|---|---:|---:|---:|
| Cream Band | 23 | 5 | 26 |
| Amber Storm | 25 | 3 | 25 |
| Blue Mist | 21 | 7 | 20 |
| Emerald Cloud | 0 | 28 | 17, disabled |
| Dark Cyclone | 17 | 3 | 12 |
| Cyan Haze | 17 | 11 | 27 |
| Deep Azure | 13 | 15 | 24 |
| Frost Veil | 6 | 18 | 20 |
| Teal Storm | 14 | 14 | 17 |
| Dark Polar | 14 | 15 | 12 |

Enabled gas weights normalize to 31.3253%, 30.1205%, 24.0964%, 14.4578%
before environmental eligibility. Ice weights total 100 before eligibility.
These are configurable game defaults, not measured astronomical frequencies.

| Ring family | Accepted | Rejected | Base family weight |
|---|---:|---:|---:|
| Bright Ice Ring | 17 | 0 | 3 |
| Broad Dust Ring | 15 | 3 | 7 |
| Broad Ice Ring | 15 | 1 | 4 |
| Broken Fragmented Ring | 18 | 2 | 3 |
| Dark Rocky Ring | 14 | 3 | 13 |
| Dense Banded Ring | 17 | 1 | 9 |
| Faint Debris Ring | 17 | 7 | 23 |
| Mixed Ice and Dust Ring | 18 | 2 | 7 |
| Thin Dust Ring | 18 | 2 | 20 |
| Thin Elegant Ring | 13 | 7 | 15 |

Conditional significance is 40% faint / 35% modest / 20% prominent / 5%
spectacular. This explicit requested split implies 6.25% of all gas giants and
7.5% of all ice giants have prominent-or-spectacular rings at baseline, before
modifiers. That is lower than the brief's separate approximate 8–12% / 10–15%
examples; the explicit significance table is used consistently. Terrestrial
stable rings are limited to faint/modest. Age, mass, known moon count, impact
history and extreme heat modify incidence, with caps of 40% for giants, 15%
cracked, 2% super-Earth and 1% other solid classes. No shepherd moons are invented.

## Pluto and sharpness

The supplied `1f187f5c-c043-4903-b65e-b68d5a9332af_3.png` replaces Pluto via
`sol-pluto-v2`. Its approved source is converted through the same sphere-material
preparation and dynamic-lighting path; the recognizable heart region is retained.

All 162 rings were rebuilt after the user's sharpness concern exposed radial
averaging in the first importer. The corrected importer samples the source into
1024 × 2048 materials. Rings use bounded 8x anisotropy and normal mip filtering,
not block compression or invented sharpening. See [ring detail report](RING_SHARPNESS_REPORT.md).

## Verification and evidence

- `giant_ring_rules`: 60,000 seeded incidence samples, 131 migrated legacy
  records, thermal/Roche bounds, determinism, disabled Emerald rejection,
  transactional developer edits and saved appearance checks.
- `planet_appearance`, `native_planet_materials`, `native_system_workspace`:
  shared classification, material and cross-view regressions.
- `engine_scene3d`, `engine_ring_material`, `native_scene3d_gpu`: closed slab
  normals/winding, narrow-gap preservation, transparency, real Vulkan mutual
  shadowing and anisotropic/minification checks.
- `native_giant_visual`: live-code captures of nine giant subclasses, all ten
  ring families, shallow views, Pluto and the functioning developer panel;
  full developer campaign save round-trip.
- All accepted prepared giant/ring review sheets were inspected, along with
  close and shallow captures. Evidence is in `work/giant-rings`,
  `work/ring-sharpness` and `build-native/preview/giant-test-captures`.
- The full native System View replay passed entry, picking, pan, zoom, follow,
  reset, back, pause/speed retention and save checks. Package verification loads
  an isolated copy of the user's developer campaign and validates asset hashes.
  The final relocated portable build passed System View and Galaxy art replays
  with the user's 1,000-system save copy and all 4,568 current approved asset and
  manifest hashes. The original campaign and packaged starting save are unchanged.

## ENGINE CAPABILITIES ADDED / EXTENDED

Core owns versioned giant/ring identities, eligibility, deterministic generation,
Roche/thermal checks and migration. Engine owns source preparation, oblate/annular
geometry, finite slab thickness, surface material response, analytic mutual
shadows and anisotropic texture sampling. Native views share one assembly and
bounded async material/GPU caches. Registry and architecture documents describe
public interfaces and future reuse.

## ENGINE LIMITATIONS REMAINING

One photograph/render cannot reveal the unseen hemisphere or perfectly recover
severe baked lighting; hidden-side reconstruction and source cleanup are
approximate. Ring shadows use analytic geometry with sampled opacity, not
individual particles, ray-traced multiple scattering or self-gravitating ring
dynamics. The thin slab is physically small and will often be subpixel edge-on.
Differential particle animation and shepherd-moon dynamics are not implemented.
The Roche/oblateness and snow-line rules are bounded game approximations, not a
planetary interior or formation solver. Legacy suspicious orbits remain visible
to diagnostics. Slow cosmetic spin is deliberately independent of game speed.
