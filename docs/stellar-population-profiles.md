# Galaxy stellar population profiles

This module extends the shared C++ stellar generator. It changes spawn probabilities only. Physical properties, planets, art, LOD, hazards, discovery hooks and the central supermassive black hole remain owned by their existing modules.

## Data ownership and presets

`data/stellar/population-v1.json` remains the sole source of the 23 canonical type IDs, baseline weights, and stellar metadata. `data/stellar/population-profiles-v1.json` contains only morphology, activity, regional and variation modifiers. There is no second baseline table.

The supplied preset percentages are game tuning values, not a measured stellar census. Morphology `presetModifiers` are the supplied percentages divided by the authoritative baseline percentages. They are applied at `referenceActivity`. Activity changes use the ratio of requested-state multipliers to reference-state multipliers. This prevents applying Active twice to the supplied Active Irregular preset, or Quiescent twice to the supplied Quiescent Elliptical preset.

| Morphology | Default/reference activity | Preset implemented |
|---|---|---|
| Spiral | Mature | Exact authoritative baseline |
| Barred Spiral | Mature | Exact same global baseline; spatial bar handled by map generation |
| Lenticular | Aging | Supplied Lenticular table |
| Elliptical | Quiescent | Supplied Quiescent Elliptical table |
| Irregular | Active | Supplied Active Irregular table |
| Ring | Mature | Supplied whole-galaxy Ring average |

The supplied rounded percentages total 99.999871%, 99.999876%, 100.000057% and 100.000002% for Lenticular, Elliptical, Irregular and Ring. Normalization removes only that rounding drift. Default Quiescent Elliptical O, Wolf–Rayet and hypergiant entries marked approximately zero are exactly zero. Multiplication and variation preserve these zero eligibilities unless a deliberately selected rejuvenating activity has an explicit configuration floor. Types are never filled in to satisfy a quota.

### Explicit elliptical rejuvenation

The default old Elliptical remains exactly the supplied table. Selecting Active or Starburst is an intentional change to its star-formation history. Sparse `rejuvenationMinimumBaselineMultipliers` configure renewed eligibility for the three otherwise-zero young types:

| Activity | O floor | Wolf–Rayet floor | Hypergiant floor |
|---|---:|---:|---:|
| Active | 0.20 × authoritative baseline | 0.10 × baseline | 0.05 × baseline |
| Starburst | 2.00 × baseline | 2.00 × baseline | 1.00 × baseline |

These are minimum **unnormalized spawn weights**, not object counts or guaranteed discoveries. The code converts them into an effective morphology multiplier before the activity multiplier is applied: `effectiveMorphology = max(presetMorphology, configuredBaselineFloor / activityRatio)`. The same multiplicative regional/variation/calibration pipeline then runs unchanged. No floor applies to Mature, Aging or Quiescent Ellipticals or to other morphology presets. Diagnostics expose both the original and effective morphology factors and the configured floor. Most small rejuvenated galaxies still have no hypergiants.

## Activities

Every activity has explicit per-type multipliers in the profile configuration, using the existing IDs. Mature is 1 for every type. Other activity values are deliberately nonuniform: an A star, an O star and a white dwarf do not share a single young/old multiplier.

| Activity | Spatial star-forming density multiplier | Young variation scale | Young-star suppression relaxed in older regions |
|---|---:|---:|---:|
| Starburst | 1.65 | 1.25 | 85% toward a neutral regional factor |
| Active | 1.25 | 1.00 | 30% toward neutral |
| Mature | 1.00 | 0.75 | None |
| Aging | 0.65 | 0.40 | None |
| Quiescent | 0.30 | 0.15 | None |

Starburst raises short-lived massive stars strongly, with O ×20, B ×8, Wolf–Rayet ×15 and hypergiants ×12 relative to Mature activity. It retains M and K stars, and old remnants fall relatively after normalization. Active provides smaller increases. Aging and Quiescent progressively suppress current massive-star formation and favor white dwarfs, quiet neutron stars and quiescent stellar black holes. These example multipliers are activity factors; the reference-state ratio above still applies to morphology presets.

Broad descriptions and star-formation labels are configuration data. Changing a morphology's activity uses an appropriate activity description instead of continuing to describe a Starburst elliptical as having no ongoing star formation. This descriptive UI API never samples a galaxy and cannot disclose hidden discoveries.

## Regional population placement

Region enum values 0–5 are preserved for compatibility. All twelve regions have explicit per-type factors:

| Enum | Player-facing region | Population tendency |
|---|---|---|
| Disk | Disk | Neutral regional factors |
| Arm | Spiral arm | Young massive stars favored; O/WR ×10, B ×7, A ×3 |
| Bulge | Galactic bulge | M/K, red giants and old remnants favored; O/WR ×0.006 |
| StarForming | Star-forming region | Strong young population; O/WR ×20 |
| Ring | Star-forming ring | Strong young population; O ×25, WR ×24, A ×6 |
| Interior | Older interior | Older/remnant population; O/WR ×0.025 |
| InnerDisk | Inner disk | Older/intermediate disk mix |
| InterArm | Inter-arm region | M/K/G and remnants favored, massive young stars suppressed |
| OuterDisk | Outer disk | General population with modest young-star suppression |
| Halo | Stellar halo | Old stars/remnants, very strong massive-star suppression |
| IrregularClump | Irregular star-formation clump | O/WR ×25, B ×18, A ×5 |
| Bar | Central bar | Older/intermediate population; young stars suppressed unless activity justifies relaxation |

These factors bias **type selection at an already generated spatial position**. The map generator owns morphology geometry: disk/arms/bar, smooth spheroid, lenticular disk/bulge, irregular clumps, or ring/interior/outskirts. It labels each procedural position with one of these regions, then provides actual region counts to the profile module. This layer does not create competing map geometry.

Simply normalizing each local table independently would change galaxy-wide percentages when a map happened to contain more arms or ring sites. `calibrate_stellar_population_profile` uses iterative proportional fitting over the actual region counts to preserve the desired whole-galaxy type probabilities **in expectation**. Regional relative biases remain, but a type-specific calibration factor balances the column marginals. The final formula is:

```
base weight × morphology factor × activity/reference factor
            × bounded seeded variation
            × regional factor × regional calibration factor
```

Each region row normalizes to 1. The count-weighted sum of all region rows equals the global target to absolute error at most `1e-13`. Positive rare entries use double precision with long-double sums and are not rounded to integer quotas. Calibration is deterministic, bounded to 4,096 iterations, and fails explicitly if configured eligibility is impossible or convergence fails. Empty populations remain valid for setup/diagnostics. With one populated region, calibration correctly returns the global target for that region.

For Ring and Irregular maps, young stars are consequently concentrated in ring sites or irregular clumps, rather than uniformly distributing a globally bluer mixture. Zero or one rare object still depends on probabilistic rolls. No object is inserted to fill a missing category.

## Seeded variation and reproducibility

No variation seed means the exact normalized preset, useful for authoring and baseline tests. Actual galaxy generation supplies the galaxy seed:

- Common entries vary by at most ±2.5% before normalization.
- Young entries vary by ±12% times the selected activity scale, at most ±15% for Starburst.
- Ultra-rare entries with baseline weight ≤100 millionths (≤0.01%) have zero direct variation by default. Their differing realized counts come from probabilistic rolls.
- All amplitudes have an additional configured 20% ceiling; the code rejects ceilings above 25%.
- Existing zero weights remain zero unless the selected activity has an explicit rejuvenation floor; variation itself never changes eligibility.

SplitMix64 with fixed integer constants and separate fixed profile/roll domains generates deterministic 53-bit uniform values. It uses neither `std::hash`, implementation-dependent random distributions nor wall-clock time. Canonical enum order fixes random consumption. Same seed, version, options, spatial region labels and region counts produce identical probabilities and stellar type placement. Save infrastructure must persist the selected profile version and resulting data; loading a save must not resample the profile.

The version identifier is `stellar-profiles-v1`. Any later change to generation semantics or tuning that is meant to preserve old-seed reproducibility requires a new generation/profile version and retention of the previous version's data/implementation. The profile configuration is embedded at build time; editing it does not alter already saved physical objects.

## Integration

1. Obtain `stellar_default_population_state(morphology)` when choosing a morphology default. Preserve a user's explicit activity selection.
2. Generate positions and regions; use `stellar_star_forming_density_multiplier` to adjust regional density where appropriate.
3. Count procedural positions by region, excluding measured anchors and the separate central SMBH.
4. Call `calibrate_stellar_population_profile(seed, options, counts)` once per map.
5. Call `sample_stellar_profile(stable_system_seed, plan.region_weights[region])` for each position.
6. Give the returned canonical `StellarObjectType` to the existing physics/art pipeline.
7. Persist profile version, settings, selected region and generated object data through the existing save infrastructure.

Known catalog anchors are handled by the existing generator. They are not included in the calibrated procedural target and should be reported separately. The central SMBH is always excluded from this table and these counts.

The convenience APIs `stellar_profile_weights`, `stellar_region_profile_weights`, `stellar_region_modifiers` and `stellar_population_factors` expose the same configuration for setup, diagnostics and tests. The 30 unseeded global and 360 unseeded regional probability tables are cached immutably after first use, so repeated sampling does not redo JSON lookups. Seeded generation retains the bounded deterministic variation path. Future metallicity/gas/age/merger modifiers can multiply the same pre-normalized factors without new class definitions or physics duplication.

CMake must embed the profile JSON through `stellar_profiles_config.hpp.in`, using newline-separated raw string literals as with the authoritative baseline. Add `stellar_population_profiles.cpp` to `stellar_core`; add `stellar_population_profiles_tests` linked to `stellar_core`. The export must include the profile JSON alongside the authoritative config for auditability.

## Fog of war and diagnostics

The module has no player knowledge or UI dependency. Its broad descriptive APIs expose only selected generation settings. `stellar_profile_diagnostics` is explicitly developer-only: it reports seed, morphology/activity, global targets, all factors, expected/actual counts, effective percentages and per-region tables. It also reports O/WR/hypergiant/magnetar/neutron/pulsar/stellar-BH totals and young-object contrasts inside/outside rings, arms/inter-arm regions, and irregular clumps. The caller must gate this behind developer diagnostics and must not feed it to normal tooltips, search or filters.

## Validation

The isolated MSVC build compiles with `/W4 /WX` and passes:

- Exact supplied presets after rounding normalization and exact Mature Spiral/Barred baseline.
- Normalization for all 360 morphology/activity/region combinations.
- Count-weighted global target preservation for every morphology/activity combination, including single-region and empty-region cases.
- One million independent seed rolls converging to the baseline within six standard deviations plus three counts.
- Quiescent Elliptical zero eligibility for O/WR/hypergiants.
- Explicit Active/Starburst elliptical rejuvenation, unchanged default zeros, diagnostic floor factors, and no guaranteed rare-object count.
- Active Irregular increases, old Lenticular population, and strong Ring/Irregular regional contrasts.
- Identical seed/settings probabilities and type placement, with differing profiles for different seeds.
- Bounded variation, unchanged ultra-rare direct factors and no explosive rare probabilities.
- 1,000 small galaxies naturally permitted to have no hypergiants.
- SMBH exclusion, invalid empty sampling rejection and developer diagnostic contents.

The parent integration additionally owns full map generation, save/reload, UI and rendering regression tests. Passing this module test alone does not certify those integration paths.
