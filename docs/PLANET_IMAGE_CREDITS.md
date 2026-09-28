# Planet image credits and limits

The approved `earth.jpg` source is retained unchanged. The map and cloud files beside it are
equirectangular layers; they are not substitutes for that approved source-disc image.

| Asset | Pixels | Projection used by game | Provenance / limitation |
| --- | ---: | --- | --- |
| `venus.jpg` | 1096 x 1096 | Source disc | NASA/JPL-Caltech's reprocessed Mariner 10 view, PIA23791 figure 2. It shows Venus's global cloud layer in a false-colour composite made from orange and ultraviolet filters. The runtime samples only the documented disc bounds, never treating this single orbital view as a global map. |
| `mercury.jpg` | 2147 x 2147 | Source disc | Existing source-disc asset; no full-surface map is claimed. |
| `mars.jpg` | 1440 x 720 | Equirectangular | Existing map-sized asset. |
| `jupiter.jpg`, `saturn.jpg`, `neptune.jpg` | 720 x 360 | Equirectangular | Existing map-sized assets. Their limited resolution remains visible in close inspection. |
| `uranus.jpg` | 1024 x 512 | Source disc | Existing source-disc asset; no global texture is inferred. |
| `moon.jpg` | 1913 x 1911 | Source disc | Existing source-disc asset; `moon-map.jpg` is the separate equirectangular map. |

Primary replacement source: [NASA Science: Venus from Mariner 10, PIA23791](https://science.nasa.gov/photojournal/venus-from-mariner-10/).
NASA identifies it as a modern reprocessing of Mariner 10 data by JPL, and states that its clouds
are at roughly 60 km altitude. The earlier Magellan-derived texture was removed because radar
surface data does not represent Venus's opaque visible cloud deck in orbital presentation.
