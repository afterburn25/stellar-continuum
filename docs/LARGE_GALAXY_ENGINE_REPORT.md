<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Larger campaigns and stationary star-map background

## ENGINE CAPABILITIES ADDED / EXTENDED

The supported campaign size contract now includes 5,000 and 10,000 systems,
alongside 250, 500, 1,000 and 2,500. Core generation, native setup and territory
input share `full_galaxy_system_counts` and `maximum_full_galaxy_system_count`
from `galaxy_catalog.hpp`. Both new sizes have visible, selectable setup buttons.
The existing radius formula preserves average spatial density as size grows.
The larger-size central-black-hole occupation multiplier retains the 2,500-system
saturation value; other population and phenomenon rules continue to use their
authoritative configuration.

Engine `SpatialRegionIndex::query` now chooses between direct local-cell lookup
and scanning occupied cells. This accelerates incremental stellar placement and
local nebula queries without allowing large empty overview queries to perform
unbounded cell traversal. Candidate results retain stable index ordering; Core
still applies its original exact spacing predicate. Spatial RNG draws are
explicitly sequenced so compiler expression ordering cannot swap the arm and
jitter samples.

Core generation indexes planetary compatibility, diversity targets and the first
body in each system once, instead of repeatedly scanning the full body catalog.
The same indexes preserve original tie-breaking and first-body order. Procedural
names retain the original stream through 5,000 names, then use deterministic
numbered names instead of exhausting the finite syllable pool. Larger campaigns
also retain the balanced planetary architecture.

`InterstellarLaneNetwork` retains the exact connected backbone and three-neighbor
rules. Its backbone uses compact ascending-ID storage, neighbors use a partial
sort, edges have indexed duplicate detection, and shortest paths use a priority
queue ordered by distance and system ID. Existing approximate tie rules, route
permission filters, cache policy and owner-thread rules remain in force.

`NativeGalaxyStarMarkerRenderer::append_neutral_batch` uses the existing Engine
triangle-mesh contract to batch neutral markers and their contrast discs in one
small texture atlas. It preserves source pixels, tint, alpha, selection, clipping
and primitive order. Batches split before 65,536 vertices and only combine
adjacent compatible commands. Ten thousand neutral markers require four meshes
instead of alternating individual image/circle commands; no catalog stars are
omitted. The atlas has a separate slot within the existing 14-resource budget.

The supplied `Star Background.png` is copied unchanged to
`assets/visual/space/star-background.png` and verified by the export manifest.
`NativeGalaxyBackdrop` keeps the original `deep-field-v3.png` and its original
brightness in the zoomed-out galaxy overview. A separate `star_background`
asset slot draws the supplied image first in the zoomed-in regional star map,
with a constant neutral brightness tint. The existing overview transition fades
between the two backgrounds; zooming back out restores the original artwork.
Only viewport dimensions determine the star field's aspect-preserving cover
rectangle. One pixel of overscan prevents seams; ultrawide and tall windows crop
the source instead of exposing edges or stretching stars. The old decorative
parallax stars are removed. Nebulae, fog, routes, systems, selection marks and
labels remain above this layer. The background has no effect on simulation,
discovery or camera movement, and does not enter system/planet views.

No save schema change is needed. Existing campaigns load their saved authority.
Existing supported-size generation must pass the pre-change deterministic
payload comparison as well as the established legacy regression fixtures.

## Verification

The full native build passed, followed by **209/209 native regression tests**
(234.22 seconds) and **88/88 export/runtime tests** (16.778 seconds).
Targeted background and marker checks also pass,
including separate overview/regional image identity, restored overview after
zooming out, unchanged pixel preparation, async queue capacity/cancellation,
oversized-source rejection for both backgrounds, layer order, and fixed cover
at 1280 x 720, 3440 x 1440, 800 x 1200 and 3840 x 2160. Marker checks cover all
10,000 stars, mesh bounds, tint/alpha/source pixels and clipping boundaries.

The new 2,500/5,000/10,000 campaign checks generate real authoritative worlds,
round-trip their complete galaxy payloads and traverse the lane graph. The
2,500 case pins the pre-optimization canonical payload SHA-256:
`9b9433d691ad9a1fa5583b3070fd76ecb8d3195285272aaa8d4caacccf233b8c`.
Existing C# lane/parity fixtures remain in the regression suite. Configuration
tests generate both larger sizes for all six morphologies.

Isolated pre/final optimization measurements on this host, seed 8057:

| Systems | Generate (ms) | JSON round-trip (ms) | Lanes (ms) | Three route trees (ms) |
| --- | ---: | ---: | ---: | ---: |
| 2,500 before | 423.514 | 1358.15 | 2258.06 | 35.321 |
| 2,500 after | 333.611 | 1408.12 | 222.70 | 2.148 |
| 5,000 after | 1106.72 | 3289.48 | 1380.72 | 8.013 |
| 10,000 after | 2310.34 | 8343.23 | 5380.88 | 16.749 |

These timings are observations, not thresholds. The 10,000-system world contains
71,189 bodies, 18,135 lanes and an 88,102,257-byte galaxy payload. Its canonical
payload hash is `5f652a295c00cfbfd391c7e379088dc2eff8c4ddb51c2227a744257053f6be7f`.

Actual native UI smoke created and saved a new 10,000-system Pelagic campaign
with seed 8057; the six size buttons fit the 720p setup. At 1080p, the corrected
overview reports 10,000 catalog markers (3 known and 9,997 unknown), the original
deep field and zero regional star backgrounds. Wheel zoom switches to the new
star field behind the regional nebulae, with no overview images, leaked unknown
names or label/HUD overlaps. System entry contains neither map background.
The paused captures preserve simulation day and knowledge.

The corrected 240-frame overview sample averaged **21.858 ms**, p95 **29.976 ms**;
the earlier unbatched sample was **39.099 ms**, p95 **43.602 ms**. This is a host
comparison, not a controlled hardware benchmark: background artwork also
changed, and the earlier sample overlapped some regression activity. Scene
construction remains a visible cost (9.664 ms mean in the corrected sample).

A separate 1,200-frame regional active-play run used the real 8X/resume/pause
controls. It advanced from day 0 to **85.2812404**, completed a mid-run save,
continued advancing afterward, then paused and completed its final save.
Mean frame interval was **17.767 ms**, p95 **31.863 ms**, with simulation/update
mean **3.467 ms** and p95 **4.678 ms**. The roughly 7.7-second mid-run save and
76.667 ms worst update demonstrate remaining large-save costs. This fresh-game
sample does not certify a late game or universal 60 FPS.

The completed active-play save also passed a separate 720p native reload,
overview/wheel-zoom/system-entry check. It retained the paused day-85 campaign,
10,000 systems, known/unknown visibility and the separate map backgrounds.
The source file, repository asset and copied runtime asset have identical
SHA-256 `7e48914134e12b7498499a20202773f4382b88dc5809ee0e945f4d0b12b57406`.

Logs: `work/large-galaxy-{baseline,targeted,new-10000,corrected-1080,active-10000}.log`,
`work/star-background-correction-{build,checks}.log`,
`work/large-galaxy-final-regression.log`, `work/large-galaxy-final-export-checks.log`
and `work/large-galaxy-reload-10000.log`.

Reviewed native captures:

- [Restored galaxy overview](../work/large-galaxy-captures/corrected-10000-1080.png)
- [Stationary regional star-map background](../work/large-galaxy-captures/corrected-10000-1080-regional.png)
- [Separate system view](../work/large-galaxy-captures/corrected-10000-1080-system.png)
- [10,000-system setup](../work/large-galaxy-captures/new-10000-setup.png)
- [Active 8X campaign after save and pause](../work/large-galaxy-captures/active-10000.png)
- [Paused 720p reload at day 85](../work/large-galaxy-captures/reload-10000-720-regional.png)

## ENGINE LIMITATIONS REMAINING

- This extension supports up to 10,000 systems. It does not enable or certify
  100,000-system campaigns. The exact lane backbone still takes quadratic work;
  replacing its topology would require a versioned generation contract.
- Save files still materialize JSON and authoritative planetary catalogs in
  memory. Loading and saving larger worlds therefore require more memory and
  time; streaming persistence is not implemented here.
- The subsequent [territory/save scaling follow-up](TERRITORY_AND_SAVE_SCALING_REPORT.md)
  removes the 4,096 visible-anchor/claim limits and accelerates save encoding.
  Long late-game campaigns and all hardware configurations remain uncertified
  by these fresh-campaign and synthetic presentation checks.
- Phenomenon images remain 2D decals, with three local visual overlaps and three
  full-detail map regions. Cold artwork preparation, exact contour distances,
  volumetric gas and remaining gameplay modifier consumers are separate work.

The accelerated spatial query and indexed route traversal can be reused by
other spatial overlays and immutable navigation catalogs. Authoritative
simulation remains in Core; Engine has no campaign-specific dependencies.
