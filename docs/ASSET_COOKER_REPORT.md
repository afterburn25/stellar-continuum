<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Cooked release and visual verification — 2026-09-20

The portable preview boots with cooked content only. Its two launchers use
separate player/developer saves. The faint star-map-style system background,
18 supplied Sol moons, nested paths, stable axes and slow cosmetic rotation are
included. The complete requested optimization program remains unfinished; this
report distinguishes working infrastructure from remaining optimization.

## Measured disk sizes

All figures below are bytes, not estimates. The development audit includes old
builds, caches and releases; it is not a fair shipping-size baseline.

| Measurement | Bytes |
| --- | ---: |
| Development tree at initial audit | 99,115,717,672 |
| Initial `assets/` subtree (7,170 files) | 3,934,330,796 |
| Previous Sol Visual Fixes ZIP | 3,831,726,801 |
| Inputs admitted by the final cook | 3,283,252,694 |
| Cooked logical payload before exact deduplication | 4,274,839,218 |
| Unique cooked payload | 4,256,229,870 |
| Complete portable folder including executable/index/licenses | 4,285,563,340 |
| Final ZIP | 4,251,805,450 |

The final ZIP is **420,078,649 bytes larger** than the previous ZIP (about 11%).
The new format does not yet achieve a distribution-size reduction. Complete mip
chains and conservative lossless exceptions outweigh compression savings here.
ZIP saves only about 0.8% on the already-compressed portable folder. Quality was
not reduced to conceal this result.

Artifact: `D:/StellarContinuum/Downloads/StellarContinuum-Faint-Skies-Moons-Dev-20260920.zip`.
All 20 indexed files inside the 21-entry ZIP match their checksums. ZIP SHA-256:
`f5a033a1d5f5f5edac982b5db81aa9622460c88171cc746f14bf01f96b4f94d1`.

The source audit scanned 330,065 files and found 5,928 exact duplicate groups
accounting for 36,517,225,851 duplicate asset bytes across the entire development
tree. Those large audit totals include old copies and are not cooker savings.
The **actual final cook deduplicates 15,610 chunks, saving 18,609,348 bytes**.

## Included and excluded content

- 3,737 stable runtime records; 40,327 indexed chunk references validated.
- 1,413 explicit source-only records and ten deprecated Sol photographs/legacy
  Earth-map records are excluded. Source files remain intact.
- Runtime allowlists also exclude rejected art before cooker discovery. Existing
  curation audits record 198 rejected skies, 119 giant images and 28 ring images.
  These are audit entries, not a claimed count of all unique rejected files.
- No source tree, Git data, source models, original master library, QA captures,
  saves, test binaries, compiler intermediates or PDBs are inside the ZIP.
- PDBs and matching release metadata remain separately under
  `work/cooker/symbols/0.1.13-alpha`.

The seven logical packages are Core, Celestial, Backgrounds, VFX, Ships, UI and
Audio. Their generation suffix and checksums are in `release-manifest.json` and
`package-files.json`. `cooked-only.marker` makes missing packages fatal rather
than silently loading loose source art. The final ZIP contains both `Play
Game.cmd` and `Developer Game.cmd`.

## Texture and content policy

| Final records | Format |
| ---: | --- |
| 1,824 | BC7 color |
| 724 | BC5 normals |
| 1,091 | Lossless RGBA8 |
| 98 | Definitions, fonts, licenses or compressed audio bytes |

BC4 is implemented and fixture-tested; this release has no standalone BC4
record. Of the RGBA8 results, 991 are quality-gate fallbacks; UI policy accounts
for the other lossless records. HDR/BC6H is not implemented and no HDR input is
admitted. Windows package codecs select the smallest of XPRESS, LZMS, or a
reversible RGBA channel-delta under either codec, per chunk; this report's
package predates LZMS and used XPRESS only. GPU format tags remain distinct
from package compression tags, with decoded fallback on unsupported graphics
hardware.

Planets/moons ship prepared material maps, with normalized normal-map mips and
linear-light, alpha-aware color mips through 1x1. Existing requests select mip
tails before disk reads; close Sol moons retain 2048-wide materials. The system
material cache stays bounded at 96 MiB with at most two full-detail system
materials. Rings ship their radial renderer input, preserving sensitive bands
losslessly. Shared materials retain per-object scale, opacity and orientation.

High/Ultra backgrounds retain native pixels. The sole approved faint starfield
is identical to the star map reference, stored losslessly. Sol has no local
cloud layer. Other systems vary the reference's framing, with only faint sky
art eligible. Region metadata and physical galaxy phenomena remain independent.

VFX retain approved pairing and full canvases. The 256/512 aliases resolve to
mips in one 1024 product instead of independent files. Full-canvas cropping is
still unfinished: the current flare attachment and projection path needs
origin-aware UV integration before a crop can preserve anchors and faint wisps.

Current mesh geometry uses existing procedural LODs; there is no imported
vertex/index-buffer cooker. Music is compressed on disk and decoded to bounded
PCM for the current track; true incremental decoding is still unfinished.

## Validation evidence

The final folder passed native startup/new campaign/audio, saved Sol system,
and player planet-screen scenarios using a working Vulkan graphics device.
Every run reported zero registry failures and `source_fallback=false`.
Isolated temporary saves protect the user's existing saves. All final package
chunks passed checksum validation. Archive entry checksums are recorded in
`work/cooker/archive-validation.json`.

Relevant regression suites passed: asset cooker/integrity, compressed GPU
textures, adaptive research catalog parity, galaxy backdrop, system background,
native moons, system view/workspace, planet materials/appearance, planetary
screen, stellar orbits, colony entry and save/persistence. Optional cooked-root
backdrop tests also check exact source pixels and queue budget/reclamation.
The background suite exercises 48,000 deterministic profiles and GPU layering.
Moon coverage includes migration, all supplied materials, tidal facing,
retrograde Triton, Uranian tilt, Charon's barycenter and selected-body tracking
across large time steps. Native family captures cover six parent families.

Fourteen source/cooked 1:1 comparisons were inspected. The sampled Earth,
Mercury, ring, flare, CME, nebula, starfield, galaxy, ship and UI images have zero
pixel error. BC7 samples measured visible channel RMSE of 0.819 (gas giant),
1.066 (ice giant), 1.043 (Europa) and 0.835 (G-class star), on a 0–255 scale.
These samples are quality evidence, not a claim that every texture was manually
reviewed. Results and images are under `work/cooker/quality-final/`.

The final incremental cook reused all 3,737 cache products, recooked zero and
completed validation/publication in 105.708 seconds. Fixture tests additionally
verify clean cooking, serial/parallel determinism, dependency invalidation,
cache repair, state filtering, runtime-raster imports, strict lookup and
corrupt/missing/truncated packages. Remote CI has not run; the checked-in manual
workflow requires an explicitly labelled Windows GPU runner.

## Performance measurements

`work/cooker/benchmark-comparison.json` records loose versus cooked startup,
system and planetary replays, process peak RAM, p95 frame times, texture counts
and cache counters. These are single local replays with warm OS caches; the
source runs overlapped cooking. They are not controlled performance claims.
Startup includes the chosen scenario rather than a separately instrumented
transition. Process memory rises with cooked images retaining decoded pixels
alongside mip data; this release does not claim a RAM reduction.

The existing image/scene byte counters combine CPU owners and GPU allocations;
the 2D counter conservatively doubles owner bytes. They are **not VRAM**. Total
driver VRAM and independent per-transition load timings remain unmeasured.

The following pairs show **loose / final cooked** results. MiB means 1,048,576
bytes. Texture counts are cached image plus scene texture entries at capture;
different transient cache eviction can change them.

| Scenario | Startup (ms) | Peak process RAM (MiB) | Frame p95 (ms) | Combined cache accounting (MiB) | Texture entries |
| --- | ---: | ---: | ---: | ---: | ---: |
| New player galaxy | 4,967 / 3,160 | 380.3 / 477.5 | 16.89 / 16.98 | 191.0 / 181.5 | 21 / 11 |
| Saved Sol system | 2,795 / 2,578 | 265.7 / 289.7 | 17.07 / 17.11 | 72.0 / 83.2 | 37 / 37 |
| Planet screen | 2,249 / 2,215 | 270.4 / 294.5 | 17.02 / 16.88 | 78.8 / 95.4 | 28 / 28 |

The largest remaining cooked individual assets are Ice dust layer 3
(14,595,520 bytes), H II region 1 (13,692,173), H II region 4 (13,643,323), Mixed
Nebula Background 2 (13,304,029) and Faint Nebula 2 (13,261,490). Their complete
mips and lossless quality make them useful targets for measured optimization.

## Remaining engine work

Origin-aware VFX cropping, incremental music decoding, imported model cooking,
HDR formats, automatic VRAM budgets and general asset-residency inspection are
unfinished. Avoiding duplicate decoded/cooked CPU storage is another concrete
memory opportunity. Mods/updater/hot reload remain optional future work.

Moons use deterministic mean two-body elements and readable chart spacing,
not observing-date ephemerides or n-body perturbations. Hidden hemispheres are
approximate wraps. Eclipse rendering supports one analytic blocker without
finite-star penumbra or combined ring/moon shadows. Only one faint sky image is
currently approved. See the moon and starfield reports for those feature limits.

Implementation ownership, APIs and future reuse are recorded in
`ENGINE_CAPABILITIES.md`; build/classification/cache commands are documented in
`ASSET_COOKER.md`. The portable game is usable, while the complete optimization
request remains open in the areas listed above.
