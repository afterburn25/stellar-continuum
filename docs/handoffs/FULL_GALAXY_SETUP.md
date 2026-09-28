# Full galaxy setup and map repair — 0.1.7 Alpha

Base: integration 4b25aba8. Integration branch: work/stellar-scale-and-core.

## Player behavior

New Player campaigns use the versioned Full galaxy profile. Setup offers Small 250,
Medium 500 (default), Large 1,000 and Huge 2,500 systems; seed/randomize; four species;
0/3/5/8/12 rival empires; None/Rare/Standard ancient empires; Rare/Uncommon/Common
habitable worlds; and Low/Standard/High anomalies. Confirmation captures one immutable
metadata record. Copy setup includes every selection, and defaults restore all choices.
The bounded setup scroll keeps Generate visible at 720p; 1080p remains the design reference.

Existing saves retain their systems, names, distances, discovery and generator identity.
The previous nearby-only 500 profile remains explicitly supported. A new campaign is
required for the wider population; loading never silently regenerates an existing galaxy.

## Generation rules and limits

All presets retain the nearest 96 classified HYG catalogue systems, exact source identities
and measured three-dimensional positions. Remaining systems have seeded names and approximate
physical positions in clustered spiral regions. Generated positions never claim HYG provenance.

The 500-system primary-class allocation is M375/K50/G25/F10/A5/hot-blue1/giant5/white-dwarf24/
neutron2/pulsar1/black-hole1/protostar1. Other sizes use largest-remainder rounding, preserving
at least one of each rare class. This is a science-informed gameplay distribution, not a
measured Milky Way census or an assertion that all catalogue classifications are error-free.
Pulsar is appended to the persisted enum and uses neutron-star radiation/planet rules.
Compact stellar class and gameplay archetype agree.

Radius is 128 × sqrt(systemCount / 500) light-years: about 91/128/181/286 ly for
250/500/1,000/2,500 systems. Sol remains at measured catalogue origin (0,0,0); the generated
centre is offset by 52% of the radius. This deliberately compact game galaxy maintains
population density rather than claiming to represent the real Milky Way's physical diameter.
The protected
centre is 14% of radius and has no ordinary systems, with its secret landmark masked by
unlabelled pale fog until the established access/discovery gates allow it. This star-free
playable core is a user-directed game rule; the real Galactic centre contains stars.

Each generated system has a nearest neighbour 3.5–8.5 light-years away and at least two
actual lane neighbours within the established opening-range ceiling of 340 light-years.
Generated clusters are checked against one another to avoid overcrowding. No invented
stars are placed inside the measured local neighbourhood. Representative nearest-neighbour
medians are 5.57/5.35/5.05/4.87 ly as population increases; 32 extra seed/preset combinations
exercise bounded packing. The generator identity is full-galaxy-compact-v1.
Each non-ancient starting civilization receives two viable nearby expansion worlds.
Longer backbone lanes connect regions. Existing propulsion and observer rules still govern
travel; this change does not unlock the whole galaxy at campaign start.

Reference context: [NASA red-dwarf neighbourhood fraction](https://science.nasa.gov/universe/exoplanets/small-stars-are-a-big-deal/),
[NASA stellar categories](https://science.nasa.gov/universe/stars/types/),
[NASA Milky Way scale](https://imagine.gsfc.nasa.gov/science/featured_science/milkyway/).
[Quasars are active galactic nuclei](https://science.nasa.gov/mission/webb/science-overview/science-explainers/what-are-active-galactic-nuclei/),
so they are not generated as ordinary stellar systems.

## Presentation and integration repairs

The camera and dust share a cached frame around the actual generated centre. Full-galaxy
overview blending follows that frame's fitted scale; regional zoom shows local stars/nebulae,
not resolved background galaxies. Measured coordinates use the established 14× presentation
multiplier, which never changes travel distances. Home and fleet centring apply it consistently.
Camera origin/zoom cancellation is computed in double precision before converting to screen
pixels, retaining subpixel mouse alignment around distant systems at 192× regional zoom.

Stellar cores now grow with zoom and class. Radial light textures have padded transparent
edges and linear filtering without mipmaps; galaxy dust fades on its elliptical extent.
GPU reproduction found that stretched 60×2 light textures selected averaged low mip levels,
giving their entire rectangles nonzero alpha (Glow edge alpha 0.051, identical to its centre).
The repaired native probe checks Glow/Bloom/Core at four sizes including 34×1.24 rays:
all edges now have zero alpha while centres remain visible. Source-alpha checks alone had
missed this failure. The maintained star-edges capture also runs in nearby-catalog validation.
Fog uses one cached, padded, softly filtered observer mask instead of independently drawn
rectangular cells, eliminating cell seams while retaining unexplored-system dimming/privacy.
Radius/class lookups and galaxy framing are cached rather than rescanned for each star.
System-view suns and actual simulation sizes are unchanged by the map marker scale.
Selecting any star shows a DISTANCE FROM HOMEWORLD fact in ly/pc, including public distance
for unknown stars without disclosing their names or survey facts. Human references use Sol;
other species use their own home system. Actual 3D depth is retained (Sirius about 8.6 ly).
The selected-system name and distance also appear immediately at the top of the right-hand
overview, without requiring the player to open the detailed inspection page.

## Scientist voice repair

A real 0.1.6 game log showed the chief scientist using US Microsoft Zira. The installed neural
manifest depended on installer-host absolute resource paths. Pack discovery now prefers
validated pack-local resources, newly installed manifests use relative paths, and logs retain
the reason for neural rejection. The scientist requires the British female bf_emma backend;
wrong-accent SAPI/cache playback is rejected with captions and diagnostics if the pack is absent.
Other allowed profiles retain their existing fallback. Voices remain synthetic.
The four female production roles retain distinct sources: scientist bf_emma, narrator
bf_isabella, commander af_kore and diplomat af_bella. Casting/audition scripts preserve
this selection. Python pack validation also resolves resources relative to the pack,
so installation checks cannot reintroduce the caller-working-directory defect.

Local neural proof synthesized the exact reconnaissance telemetry line with bf_emma, producing
156,000 PCM samples, and verified cache reuse and a copied-host manifest with unavailable old
paths. Proof: work/stellar-voice-neural-proof/work/voice-core-proof/neural-scientist-british.wav
in the parent workspace. Neural models remain an optional installed pack, not committed assets.

## Validation

Maintained Core runtime, quality and voice checks cover generation/determinism, exact counts,
legacy save round trips, option rejection, species starts, core exclusion, observer privacy,
camera precision, padded alpha, cache/backend identity and real British speech.
Native screenshot and performance receipts are recorded under this branch's work directory;
only completed receipts and fresh hosted checks authorize integration/release acceptance.
Do not substitute earlier 0.1.6 receipts for this revision.

Earlier completed local receipts before the final compact-density/filter repairs:
- Core runtime 88/88; quality 20/20; actual installed neural voice 12/12.
- Full galaxy 500 native map/unknown privacy/zoom at 720p and 1080p: exit 0,
  work/full-galaxy-final; legacy nearby profile also previously exited 0.
- Species and setup controls, seed entry/copy/defaults, immutable confirmation and
  nonhuman generation at 720p/1080p: exit 0, work/galaxy-options-final.
- Huge 2,500-system native performance: exit 0, all 12 views meet 60 FPS/p95 target
  on RTX 3080 Ti at 2560×1440. Overview improved from 38.7 to 140.4 FPS by batching
  subpixel stars into two same-texture quads. One 104 ms outlier remains in that
  overview sample; this is not a guarantee of hitch-free performance or all hardware.
  Receipt work/huge-galaxy-performance-batched, source bb9896b1 plus the committed
  overview batching diff. Detailed regional stars retain the close-up rendering.
- Fresh hosted packaging/regressions and native live British scientist playback
  remain release gates; record final receipts in the pull request.

Final compact-density/filter evidence is recorded in work/compact-map-accepted (exit 0) and
work/compact-map-distance-final (720p/1080p,
Wolf 359 at regional/intermediate/192× zoom and unknown-entry privacy),
work/compact-integrated-core.log, work/compact-integrated-quality.log and the final PR
acceptance comment. Do not treat a receipt as passing without its completion/exit result.
The compact Huge native benchmark at 2bba9ffb exited 0: all 12 views passed, overview
143.4 FPS / p95 7.06 ms, lowest view 125.6 FPS. Construction and shipyard each had a
single approximately 100 ms outlier; this is not a guarantee of universally hitch-free play.
Receipt work/compact-huge-performance, RTX 3080 Ti / 2560×1440.
