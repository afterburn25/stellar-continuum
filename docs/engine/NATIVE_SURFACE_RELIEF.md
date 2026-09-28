# Native world-space terrain relief

The native surface layers a neutral slope-shading mask over its approved terrain
artwork. Heights come from Core's existing `surface_terrain_height`, also used by
construction geometry. This selectively adapts the useful hillshade concept from
Devin f0567489 without replacing the texture with 88de7e38's procedural palette.
It changes no environment facts, placement rules, costs, timers or saves.

The immutable 1024x1024 RGBA mask covers the canonical -512 to +512 metre surface
area. A shared background job reserves exactly 4 MiB for its output; the owner
retains one image or one pending ticket. Camera position, zoom and display size do
not enter its identity. The view projects the same world-space image for every
camera frame, clips it to the terrain panel and submits it before roads, buildings
and UI. Alpha is at most 54/255, the flat hub apron is transparent, and patch edges
fade out to avoid a rectangular shading seam. The existing ground texture and
neutral colour treatment remain visible underneath.

Identity binds campaign generation, colony and body. Close, campaign discard or
identity replacement cancels pending work and releases the retained mask. Canceled
work remains accounted for by the shared queue until its worker releases it; no
worker reads campaign state. Saturation retries admission, while preparation
failures latch instead of looping. A failed layer keeps the surface usable with
an explicit reopen-to-retry notice and a terminal diagnostic containing the error.
No queue means no synchronous fallback generation.

The graphical replay waits for the mask to be ready, exercises the established
surface camera/construction paths and verifies one prepared image. It captures
paired enabled/disabled relief frames without changing the artwork, facilities or
interface. Export checks require matching per-capture dimensions, visible pixel
changes inside the terrain and none outside. Existing building-art comparisons,
operating-state inspection, cost previews, cancellation/refunds, management and
complete Player17 persistence checks remain required.

This is a top-down visual improvement. It does not implement a 3D heightfield,
terrain collision, atmospheric descent, biome simulation or dynamic shadows from
buildings. It also does not establish sustained 60 FPS or clean-machine readiness.
