<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Sol and giant visual corrections — 2026-09-20

Sol now uses the supplied faint distant-star plate consistently, with no local
generated nebula overlay or cloud absorption. Its asteroid and ice belts contain
2,048 visible representatives each. All gaseous planet classes use the new
reviewed giant artwork, including the four canonical Sol giants and saved worlds.

## Background detail and clear Sol policy

The former display multiplier reduced clear skies to roughly one quarter of the
source brightness. Normal clear-sky exposure is now near unity; Sol uses unity.
High and Ultra preserve source dimensions and RGBA pixels: 20 approved images are
2944 × 1648 and 110 are 1456 × 816. Low and Medium retain their reduced-detail
options. Opt-in bounded cubic magnification preserves small bright features
without sharpening halos; normal filtered minification remains unchanged.

Core `SystemBackgroundProfile::local_nebula` specifies whether a preset admits
local gas imagery and attenuation. Canonical `sol-v1` uses `starfield-055`, no
blend/crop/mirror/rotation, and disables local nebulae. Display names cannot claim
this policy. System View, entry preloading and tactical backgrounds consume it.
Leaving a cloudy system cancels its obsolete composite without retaining a
loading gate or painting it over Sol. Galaxy phenomena and simulation effects
are unchanged. The original plate remains artistic imagery, not an observed
constellation catalog.

## Giant artwork and existing saves

The exclusive new pool contains 150 approved giant images. Gas Giant, Ice Giant,
Hot Jupiter and Mini-Neptune all resolve to this pool. Hot and miniature giant
physical classifications remain intact while using compatible new materials.
The five remaining legacy Mini-Neptune images are rejected from the runtime
pool, leaving 705 approved imported planet materials and 520 rejected records.
The disabled Emerald pool remains excluded.

| Sol body | Replacement material |
|---|---|
| Jupiter | giant-amber-storm-02 |
| Saturn | giant-cream-band-01 |
| Uranus | giant-cyan-haze-03 |
| Neptune | giant-deep-azure-01 |

Migration clears extra cloud-layer opacity even for already-current giant
records. Imported material assembly uses the supplied surface, without a second
generated cloud texture. Existing valid separate ring identities are retained.
Legacy ring conversion still observes material survival rules. Physical mass,
radius, environment and orbital data are preserved.

## Belt density

Canonical Sol belts now provide 4,096 visual representatives in total, up from
1,420. This applies to both fresh and loaded campaigns. The physical populations,
orbital corridors, deterministic seeds and mining ledgers are unchanged. Distant
representatives use bounded batches; resolved rocks use the existing mesh budget
and slow independent tumbling. This changes visual density, not asteroid mass or
the number of independently simulated bodies.

## Validation

- Planet appearance, small-body fields, native small-body rendering, celestial
  appearance and GPU rendering suites passed. The repaired legacy-ring fixture
  and final giant visual/background suites then passed (3/3, 29.64 seconds).
- GPU detail coverage checks increased magnified star-core contrast without
  overshoot, and unchanged minification. All 130 High-quality prepared skies
  retain their original dimensions and pixels. Sky selection exercises 48,000
  profiles and full campaign save reconstruction.
- The cloud regression places canonical Sol inside a generated dark nebula,
  enters from a loaded cloudy system, and verifies a single unattenuated sky
  layer in both system and battle views. It also cancels a pending composite
  and verifies reclaimed shared queue capacity.
- Native captures cover all nine giant subclasses, four Sol replacements, ten
  ring families and before/after belts. Rendered belt representatives increase
  from 1,420 to 4,096. Captures were visually inspected.
- A copy of the user's 1,000-system campaign passed real 1920 × 1080 native
  System View navigation, zoom, focus and save/load replay. All 891 gaseous
  worlds resolve to new giant IDs with zero extra cloud opacity. Sol's local
  sky capture was inspected; no generated cloud is present. Original save is
  preserved. Scene p95 was 4.143 ms in this local replay, not a universal
  performance guarantee.
- The relocated portable build also passed the native replay. All 4,538 approved
  asset/manifest files passed hash and membership verification; all 7,173 physical
  body records remained identical. Fifteen export tests passed. The included
  campaign was unchanged by validation, which used a separate copy.

## ENGINE CAPABILITIES ADDED / EXTENDED

Core owns the clear-preset sky policy, complete giant material migration and
canonical belt visual population. Engine exposes optional bounded cubic texture
magnification. Native consumers share those rules, immutable materials and
existing queue/cache budgets. No save schema or additional simulation thread is
introduced; old appearance/count fields migrate deterministically on load.

## ENGINE LIMITATIONS REMAINING

Sky plates have finite source resolution and are not a navigable all-sky catalog.
Single-image giant lighting recovery and unseen hemispheres remain approximate.
Belts use representative rendering with a bounded close-mesh budget. Ice has
diffuse lighting; advanced reflection/refraction remain unimplemented. Rings
retain analytic geometry/shadows rather than individual particle dynamics.
The separate 18-major-moon integration request remains pending: the present
moon orbits are still schematic. This correction does not claim that work done.
