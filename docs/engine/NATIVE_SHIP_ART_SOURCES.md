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

Only these six images and this source note are declared by
`export/native-ship-art-assets.json`; packaging verifies their paths and
SHA-256 fingerprints.
