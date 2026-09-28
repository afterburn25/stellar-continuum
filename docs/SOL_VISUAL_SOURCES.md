# Sol planetary appearance sources

Acquired and checked 2026-09-08. The files under `assets/visual/sol/` are original
downloaded NASA image bytes, renamed for stable runtime lookup. No AI imagery,
pixel recoloring, cropping, or resizing was applied to the source files.
Runtime projection and lighting are presentation effects, not new scientific data.

The [NASA 3D Resources repository](https://github.com/nasa/NASA-3D-Resources)
describes its assets as free and without copyright. NASA's
[media usage guidelines](https://www.nasa.gov/nasa-brand-center/images-and-media/)
request source acknowledgement and prohibit implying endorsement. No NASA logo,
person or endorsement is used. These images are credited in this document;
they do not imply NASA endorses Stellar Continuum.

## Sources and interpretation

| Runtime file | Original source | Projection and limitations |
| --- | --- | --- |
| `mercury.jpg` | [Mercury Globe: 0°N, 0°E — PIA15160](https://science.nasa.gov/photojournal/mercury-globe-0n-0e/) | Gray orthographic MESSENGER mosaic, 2147×2147. Full globe; not a longitude/latitude map. Credit NASA/Johns Hopkins University Applied Physics Laboratory/Carnegie Institution of Washington. |
| `venus.jpg` | [Venus from Mariner 10 — PIA23791](https://science.nasa.gov/photojournal/venus-from-mariner-10/) | 1096×1096 NASA/JPL-Caltech reprocessed Mariner 10 false-colour cloud composite (figure 2). Sample the source disc only; it is an orbital cloud view, not a global surface map. Credit NASA/JPL-Caltech. |
| `earth.jpg` | [NASA 3D Resources — Earth (B)](https://github.com/nasa/NASA-3D-Resources/blob/master/Images%20and%20Textures/Earth%20(B)/Earth%20(B).jpg) | 4095×4095 original Earth photograph with black padding. The globe shows recognizable continents, ocean and clouds. It is not an equirectangular map. Credit NASA. |
| `mars.jpg` | [NASA 3D Resources — Mars](https://github.com/nasa/NASA-3D-Resources/blob/master/Images%20and%20Textures/Mars/Mars.jpg) | 1440×720 planetary visualization color map, projected as equirectangular. Credit NASA. |
| `jupiter.jpg` | [NASA 3D Resources — Jupiter](https://github.com/nasa/NASA-3D-Resources/blob/master/Images%20and%20Textures/Jupiter/Jupiter.jpg) | 720×360 visualization color map; atmospheric bands and storm detail. Equirectangular. Credit NASA. |
| `saturn.jpg` | [NASA 3D Resources — Saturn](https://github.com/nasa/NASA-3D-Resources/blob/master/Images%20and%20Textures/Saturn/Saturn.jpg) | 720×360 atmospheric color map, equirectangular. The runtime separately illustrates rings; they are not contained in this map. Credit NASA. |
| `uranus.jpg` | [NASA SVS — Uranus in True and False Color](https://svs.gsfc.nasa.gov/30356) | 1024×512 two-disc Voyager 2 image. Sample the LEFT disc only: NASA identifies it as processed to approximate human-eye color. The right disc is false color and must never be sampled. Credit NASA/JPL. |
| `neptune.jpg` | [NASA 3D Resources — Neptune](https://github.com/nasa/NASA-3D-Resources/blob/master/Images%20and%20Textures/Neptune/Neptune.jpg) | 720×360 NASA visualization color map, equirectangular. Color calibration is not documented by this asset entry; do not describe its saturation as a precise human-eye color measurement. Credit NASA. |
| `moon.jpg` | [NASA 3D Resources — Moon](https://github.com/nasa/NASA-3D-Resources/blob/master/Images%20and%20Textures/Moon/Moon.jpg) | 1913×1911 globe photograph with black margin, not an equirectangular map. Credit NASA. |

The legacy NASA 3D map entries do not provide a complete per-image photometric
calibration history. They are attributed visualization textures, not claims of
exact telescope or human-eye color. Globe photographs show one observed
hemisphere and must not be presented as complete surface maps.

Venus uses NASA/JPL-Caltech's reprocessed Mariner 10 cloud observation because it
represents the opaque visible cloud deck in orbital presentation. The earlier
MESSENGER comparison image and Magellan-derived radar surface map were replaced;
the runtime does not infer a complete surface map from this single source disc.

## Direct download URLs

- Mercury: <https://assets.science.nasa.gov/dynamicimage/assets/science/psd/photojournal/pia/pia15/pia15160/PIA15160.jpg?crop=faces%2Cfocalpoint&fit=clip&h=2147&w=2147>
- Venus: <https://science.nasa.gov/photojournal/venus-from-mariner-10/> (PIA23791, figure 2; downloaded rendition recorded by the integrity manifest below)
- Earth: <https://raw.githubusercontent.com/nasa/NASA-3D-Resources/master/Images%20and%20Textures/Earth%20%28B%29/Earth%20%28B%29.jpg>
- Mars: <https://raw.githubusercontent.com/nasa/NASA-3D-Resources/master/Images%20and%20Textures/Mars/Mars.jpg>
- Jupiter: <https://raw.githubusercontent.com/nasa/NASA-3D-Resources/master/Images%20and%20Textures/Jupiter/Jupiter.jpg>
- Saturn: <https://raw.githubusercontent.com/nasa/NASA-3D-Resources/master/Images%20and%20Textures/Saturn/Saturn.jpg>
- Uranus: <https://svs.gsfc.nasa.gov/vis/a030000/a030300/a030356/uranus-voyager2_print.jpg>
- Neptune: <https://raw.githubusercontent.com/nasa/NASA-3D-Resources/master/Images%20and%20Textures/Neptune/Neptune.jpg>
- Moon: <https://raw.githubusercontent.com/nasa/NASA-3D-Resources/master/Images%20and%20Textures/Moon/Moon.jpg>

NASA's image service supplies the original selected published rendition above;
no additional client-side image processing was performed. SHA-256 values below
identify the exact downloaded rendition independently of mutable source URLs.
The NASA GitHub tree inspected during acquisition was
`11ebb4ee043715aefbba6aeec8a61746fad67fa7`.

## SHA-256 integrity manifest

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `earth.jpg` | 2510007 | `12f6c594f6acc769598ae3cb908e9020f7973f2a0b73f7fb75bf3df737fb7b31` |
| `jupiter.jpg` | 225878 | `ac2d387f8ea56a59f45dea37993c779a730f49d7d9fdbc8d36c307a351b1f1a1` |
| `mars.jpg` | 972896 | `12ec6bf02ebd42a246edc778cb2ce8c595b4d9f0892badf0bfff8abb2303c780` |
| `mercury.jpg` | 718073 | `4c13eaf876575b39ed1581f5ded9d71b8070418e4c1c978719ffaad7bd52d53e` |
| `moon.jpg` | 2957499 | `1318334a5089fb186d39996f60677b5f5b42cde74158ceff97b3e2283bfd41d4` |
| `neptune.jpg` | 210528 | `e9bb2b7bcfb98d3f1adc6f2e20e833a00c11e9677238d8a00ae7431214ca70aa` |
| `saturn.jpg` | 275735 | `d959a239e3d87630daf08e2a688dcbf315fe01b1fd474e55522770108f40aca7` |
| `uranus.jpg` | 52184 | `749f4854d1a79c4e6ae7bdce7f1118d5b41d75b7e3a8d95d9a4dd82157a43829` |
| `venus.jpg` | 65525 | `9deaf7392cd41dcd77f2e6fc61a12641af3a2dc1a52ecfd652388a65bd225dbc` |

## Runtime sampling contract

Normalized coordinates are relative to the complete original file. The two
radii are fractions of image width and height, respectively. These are practical
disc framing estimates, not astronomical measurements.

| Globe | Center x,y | Radius x,y |
| --- | --- | --- |
| Earth | 0.5280, 0.5836 | 0.1851, 0.1968 |
| Mercury | 0.500, 0.500 | 0.454, 0.454 |
| Uranus left disc | 0.250, 0.501 | 0.176, 0.352 |
| Venus source disc | 0.500, 0.500 | 0.414, 0.426 |
| Moon | approximately 0.529, 0.503 | approximately 0.400, 0.399 |

The four 2:1 maps use longitude/latitude sampling. Globe sources use their
documented disc framing. A globe source must never be longitude-wrapped.
