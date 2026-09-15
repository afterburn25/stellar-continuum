# Native ship artwork sources

The C++ client reuses these existing approved Stellar Continuum ship
portraits unchanged:

- `assets/visual/ships/pathfinder-scout.jpg`: Pathfinder Scout portrait.
- `assets/visual/ships/deep-space-science-vessel.jpg`: Deep Space Science
  Vessel portrait.
- `assets/visual/ships/patrol-corvette.jpg`: Patrol Corvette portrait.
- `assets/visual/ships/interstellar-colony-ship.jpg`: Interstellar Colony
  Ship portrait.
- `assets/visual/ships/resource-outpost-ship.png`: Resource Outpost Ship
  portrait.
- `assets/visual/ships/interstellar-bulk-freighter.png`: Interstellar Bulk
  Freighter portrait.

All six are original first-generation project images generated with
OpenAI's built-in image generation tool on 2026-09-09/2026-09-10 as a
coherent text-free hard-science-fiction fleet family; provenance is
recorded in `docs/ASSET_MANIFEST.md`. The native client decodes each file
once into a bounded thumbnail cache for fleet and shipyard presentation;
authoritative design identity, role resolution and fleet state never come
from the pixels.

These six portraits, the tactical derivative below, and this source note are declared by
`export/native-ship-art-assets.json`; packaging verifies their paths and
SHA-256 fingerprints.

## Tactical corvette derivative

`assets/visual/ships/patrol-corvette-tactical-v1.png` is an original project
sprite generated on 2026-09-15 with OpenAI's built-in image generation tool,
using the existing patrol-corvette portrait as its design reference. The
full prompt is preserved in `docs/engine/NATIVE_TACTICAL_ART_PROMPT.md`.
The 1254 by 1254 RGBA image has a transparent background, a nose pointing
right and inactive thruster nozzles. It is a top-down 2D representation,
not a 3D hull. The original portrait remains unchanged.

The tactical layer decodes one shared full-resolution image (6,290,064 CPU
bytes) and submits at most 32 rotated instances. The source's alpha is
preserved; background JPEG portraits are never pasted into the battlefield.
Only observer-owned, explicitly identified patrol corvettes may use it.
