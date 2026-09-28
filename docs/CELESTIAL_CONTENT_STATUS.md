# Celestial content and Sol status

Audited from this branch's C++ source and JSON catalogs on **2026-09-20**.
Counts below are records in the indicated catalogs, not guessed source-folder
totals or a claim that all historical requests were accepted. Build/test outcome
is recorded separately in the [verification receipt](validation/2026-09-20-development-sync.md).

## Catalog inventory

| Content | Actual inventory | Authority and status |
| --- | --- | --- |
| Galaxy morphologies | 6: spiral, barred spiral, elliptical, lenticular, irregular, ring | `data/stellar/galaxy-generation-v2.json`; Core `galaxy_configuration.cpp`; IMPLEMENTED |
| Population states | 5: Starburst, Active, Mature, Aging, Quiescent; 12 regions | `population-profiles-v1.json`; Core profile and configuration tests; IMPLEMENTED |
| Galaxy art | 12 art entries, 6 density masks, 6 footprint frames | `galaxy-visuals-v1.json`; prepared source/shape alignment; IMPLEMENTED BUT NEEDS POLISH |
| Stellar types | 23 population object records | `population-v1.json`; `stellar_object.cpp`; IMPLEMENTED |
| Stellar art | 25 object records and 33 files | `assets/visual/stellar/manifest.json`; includes representation variants, not 25 population classes |
| Central SMBH | One canonical central-black-hole model with quiet/accreting/jet state support | `stellar_object.cpp`, galaxy persistence and developer coverage; IMPLEMENTED |
| Planet taxonomy | 16 base classes, 66 subclass definitions, 5 equilibrium-temperature zones | `planet-types-v1.json`; `PlanetTypeRecord`; IMPLEMENTED |
| Current accepted planet art | 705 accepted records; 520 rejected records | `planet-art-v1.json`; accepted/rejected pools are disjoint; accepted maps have 4,230 loose export entries (6 per material) |
| Replacement giant audit | 269 inspected records: 150 accepted, 119 rejected | `giant-asset-audit-v1.json`; accepted replacements feed canonical appearance; IMPLEMENTED |
| Deprecated giant identities | 131 records, 9 subclass records and 9 migration mappings | `deprecated-giant-art-v1.json`; migration data, not a second active art pool; DEPRECATED |
| Ring audit | 190 records: 162 accepted, 28 rejected; 10 ring families | `ring-asset-audit-v1.json`, `ring-types-v1.json`; accepted assets become 3D annuli; IMPLEMENTED BUT NEEDS POLISH |
| Supplied major moon art | 18 source audit records; 54 runtime map entries | `moon-asset-audit-v1.json`, `export/native-moon-assets.json`; Earth's pre-existing Moon is additional |
| Small-body fields | 9 types: Rocky, Metallic, Carbonaceous, Mixed, Ice, Debris Disk, Shattered, Cracked Cluster, Planetary Halo; 36 image entries | `small-body-fields-v1.json`, `export/native-small-body-assets.json`; solid-mesh/LOD consumers; IMPLEMENTED BUT NEEDS POLISH |
| Galaxy phenomena | 13 current v2 type records and 3 extra definitions; 48 art records | `phenomena-v2.json`, `phenomenon-art-v1.json`; generation/persistence and art; IMPLEMENTED BUT NEEDS POLISH |
| System skies | 10 category definitions, **1 accepted faint image** | `starfields-v1.json`; audit has 199 records: 1 accepted, 198 rejected, 0 reclassified |
| Stellar eruptions | 204 visual sets; 372 unique prepared textures at each of 256/512/1024 (1,116 PNGs) | `assets/visual/stellar-eruptions/manifest.json`; sets reuse stages, so these are not 204 unique four-image sequences |

Stellar population records include O/B/A/F/G/K/M, brown/white dwarfs, quiet
neutron stars, pulsars, magnetars, giants/supergiants, Wolf-Rayet, hypergiant
and quiescent/accreting/jet black-hole variants. Their tuning weights are game
configuration, not a claimed measured astronomical census.

The original 47-folder request is historical provenance. Current accepted and
rejected registries include later curation/replacements; do not label all 1,175
originally mentioned images as accepted runtime planets. Subclasses retain base
class, valid zones, temperature/atmosphere rules, water/ice/volcanism flags,
generation weights and resolved accepted/rejected image pools. Generation-disabled
or empty subclasses must not be silently filled with rejected artwork.

## Current presentation rules

- One saved `PlanetAppearance` controls system/globe identity, materials, rings,
  individual axis and tidal lock. Sol geography is restricted to canonical Sol
  identities. Atmospheric/climate constraints are Core rules.
- Planets are real shaded globes with slow cosmetic spin. Added generic cloud
  cover over supplied artwork was removed; bare worlds must remain bare. The
  earlier request to stop all rotation was superseded by slow individual-axis
  rotation with tidal locking retained.
- Replacement gas/ice giant art supersedes old giant textures; old identities
  migrate. Emerald images rejected as too similar are not a runtime fallback.
- Rings use prepared 1024×2048 radial/angular material detail and close annular
  geometry, mipmaps and selective anisotropy. Sharpness was visually corrected;
  import quality and viewing angle still need ongoing review.
- Local skies admit the same faint map-reference image. Physical galaxy
  environment metadata remains separate from decorative local clouds. Sol has
  no generated local cloud/nebula overlay. Earlier multi-sky counts and brightness
  reports describe superseded catalogs.
- Flares/prominences/CMEs share saved event identity and an unscaled activity
  clock. Production visuals use sharp curved emissive meshes attached to varied
  stellar surface locations. Image-derived ray-marched flare volume is retired
  from the production path. Space-weather damage is not implied by launch hooks.
- Rocky and ice fields have physical mesh volume, varied and elongated pieces,
  independent slow tumble and analytic belt motion. Ice uses approximate
  environment optics; no multi-bounce/caustic solution is present.

## Sol checklist

| Item | Current state | Evidence / limitation |
| --- | --- | --- |
| Sun | IMPLEMENTED | Sol anchor and G-class stellar metadata/art; shared activity path |
| Eight planets | IMPLEMENTED | `create_sol_catalog` in `core/src/planetary_catalog.cpp` reserves Mercury through Neptune, IDs 1–8 |
| Pluto | IMPLEMENTED | Reserved dwarf-planet ID 10 and `sol-pluto-v2` supplied 3D material; migration rejects conflicting reserved IDs |
| Earth's Moon | IMPLEMENTED | Reserved ID 9, parent Earth ID 3 |
| Eighteen newly supplied moons | IMPLEMENTED | `planetary_satellites.cpp` reserves IDs 11–28 and explicit parents; old saves append missing moons idempotently |
| Jupiter moons | IMPLEMENTED | Io, Europa, Ganymede, Callisto |
| Saturn moons | IMPLEMENTED | Mimas, Enceladus, Tethys, Dione, Rhea, Titan, Iapetus |
| Uranus moons | IMPLEMENTED | Miranda, Ariel, Umbriel, Titania, Oberon |
| Neptune moon | IMPLEMENTED | Triton |
| Pluto moon | IMPLEMENTED | Charon |
| Separate satellite paths | IMPLEMENTED BUT NEEDS POLISH | Shared mean-element Kepler trajectories and parent reference frames; display distances are chart-scaled |
| Pluto–Charon barycenter | IMPLEMENTED | Satellite mass fraction displaces the primary and relative companion in `native_system_view.cpp`; not an N-body solver |
| Triton retrograde | IMPLEMENTED | 157.3° inclination relative to Neptune's frame; tested satellite path/pose |
| Axes and tidal locking | IMPLEMENTED BUT NEEDS POLISH | Shared axis/orthonormal pose, parent-relative locked face, Uranus/Pluto tilted frames; inspect lighting/pose visually after changes |
| Asteroid belt and outer ice belt | IMPLEMENTED BUT NEEDS POLISH | Dedicated Sol fields/orbit paths and dense representative distribution; LOD/culling bound actual mesh count, not every particle is a simulated rock |
| Rings | IMPLEMENTED BUT NEEDS POLISH | Canonical giant/ring appearance and accepted ring material path; schematic scales, finite thickness and approximate shadowing |
| Real orbital data | PARTIALLY IMPLEMENTED | Physical mean satellite elements and analytic periods; reproducible phases, not observing-date ephemerides or perturbation integration |
| Sol clear distant sky | IMPLEMENTED | Canonical profile, faint reference selection and local-nebula suppression |

`sol_moon_definitions()` contains **19 moons including Earth's Moon**. The 18
new files are not the total moon count. Procedural moons have parent relationships
and Kepler periods derived from parent mass; detailed formation/resonance/tidal
evolution and perturbations remain future work.

Primary regression owners: `sol_catalog`, `planet_appearance`, `giant_ring_rules`,
`native_moons`, `native_giant_visual`, `stellar_orbits`, `small_body_fields`,
`native_small_body_renderer`, `stellar_activity`, `native_stellar_eruptions`,
`system_background` and the scene/material tests. Their presence is not a substitute
for the current [test results](validation/2026-09-20-development-sync.md).
