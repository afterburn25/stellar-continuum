# Irregular and Spiral widescreen artwork

Requested September 17, 2026. Both supplied pairs now have matching landscape framing. The complete galaxy remains visible. User source files under `C:/Steller Continuum/Assets/Galaxy's/` are unchanged.

Method: built-in imagegen editing, followed by the existing aspect-preserving runtime asset importer. Generated sources are 1672 × 941 (the tool's near-16:9 output); the importer exports both pairs at exactly 1280 × 720. No arms or gas clouds are cropped. Source files, source hashes, original-file hashes, and runtime hashes are preserved in the manifests.

## Saved source assets

- `assets/source/galaxies-16x9/irregular-gas-dust-only.png`
- `assets/source/galaxies-16x9/irregular-stars-included.png`
- `assets/source/galaxies-16x9/spiral-gas-dust-only.png`
- `assets/source/galaxies-16x9/spiral-stars-included.png`

Runtime files are `assets/visual/galaxies/{irregular,spiral}-generic-{gas_dust_only,stars_included}.png`. `export/galaxy-asset-edits.json` records the explicit edit mapping. Reimporting approved originals reapplies those mappings rather than reverting the aspect correction. Density masks and footprint transforms are rebuilt from the edited gas layers.

## Final prompt set

Irregular gas target: original Irregular gas-dust-only image.

> Edit target: the attached irregular galaxy gas-and-dust-only game artwork. Change ONLY framing/canvas to exact 16:9 landscape, preferably 2048 x 1152 pixels. Preserve the irregular galaxy's recognizable bluish gas and dark dust structures, direction, full outline and natural proportions; center it and make it fill the wide frame with a modest black space margin on all sides. Show the entire galaxy without cropping off gas clouds. Expand the surrounding black void if necessary, do not stretch or squash the galaxy. Preserve its soft high-detail cloudy texture and subtle bright cloudy knots. This is the gas/dust layer for a strategy game; NO star points, NO starfield, NO new galaxy, NO extra objects, NO text, NO border or frame. Faithful reframing of the provided artwork.

Spiral gas target: original Spiral gas-dust-only image.

> Edit target: the attached spiral galaxy gas-and-dust-only game artwork. Change ONLY framing/canvas to exact 16:9 landscape, preferably 2048 x 1152 pixels. Preserve this spiral galaxy's recognizable arms, pale ivory cloudy nucleus, bluish gas, dark dust lanes, orientation, full outline and natural proportions; center it within a widescreen frame with a modest black space margin on all sides. Show the whole galaxy without clipping outer arms. Expand surrounding black void as necessary rather than stretching or squashing the galaxy. Preserve the original style, color and fine texture. This is a gas/dust layer for a strategy game; NO star points, NO starfield, NO new galaxy, NO extra objects, NO text, NO border or frame. Faithful reframing of provided artwork.

Stars Included: two separate edits using the following prompt, with `{morphology}` replaced by `irregular` and `spiral`. Image 1 is that morphology's edited gas layer; Image 2 is the corresponding original Stars Included image.

> Use case: precise-object-edit. Image 1 is the EDIT TARGET: a widescreen {morphology} galaxy gas/dust layer. Image 2 is supporting reference for the original star-filled preview appearance. Produce the matching STARS INCLUDED preview for image 1. Output exact 16:9 landscape (2048 x 1152 preferred). Keep image 1's galaxy silhouette, cloudy knots, gas colors, dust lanes, orientation, center, scale, outline and outer black margins fixed. Add the natural dense tiny star points seen in image 2 within that existing luminous structure, plus sparse small stars in the surrounding dark void as in reference 2. Preserve galaxy type, no new galaxy or new structures. The gas/dust image and this star-filled preview are paired game assets and must have the same framing and cloud structure. No captions, no text, no border, no UI, no artificial framing. Full galaxy visible, never crop, squash or stretch.

These edits do not invent population-specific variants; all five explicit population states still use the validated same-morphology generic pair.
