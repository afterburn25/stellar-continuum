# Planetary management artwork

Original artwork generated with the built-in image-generation tool on 2026-09-13.
The user's reference screenshot guided the visual density and use of color. No
pixels, logos, characters, or interface assets were extracted from that screenshot.

- `colony-panorama-v1.png`: decorative developed temperate colony vista. It is
  labeled as an illustration in the window. Unbuilt Command Centers and other
  environments use the existing orbital backdrop instead.
- `building-portraits-v1.png`: nine equally divided atlas regions, read with
  Godot AtlasTexture; the source image is retained intact. Row-major order:
  generator, science lab, fabricator, trade hub, habitat complex, controlled
  agriculture, water reclamation, grid battery, cargo terminal. Upgrades share
  their functional family's illustration; game text identifies the actual level.

Planet portraits use the same selected-body appearance and credited Sol imagery
as the existing orbital view. See `docs/PLANET_IMAGE_CREDITS.md`. Procedural worlds
use their observer-filtered appearance. Artwork never supplies simulation values.

## Generation prompts

### Colony panorama

Use case: stylized-concept. Asset type: original background artwork for the planetary management window of Stellar Continuum, a serious space strategy game. Create a very wide cinematic 3:1 panorama of an advanced human colony on a temperate Earthlike world: elegant silver and graphite architecture, compact blue glass towers, terraced civic buildings, luminous cyan transit corridors, landscaped emerald parks, a calm deep-blue bay, distant mountain ridges and atmospheric blue sky with warm sunrise highlights. Eye-level aerial overlook, rich physically plausible materials, detailed premium sci-fi game concept art, inviting but believable. Keep city landmarks mainly in the central and right half; left third is quieter shaded water/landscape suitable for a title overlay. Important skyline within the middle half vertically so a short wide banner crop remains compelling. No people close up, no spaceships close up, no planets hanging in the sky. No lettering, no text, no interface, no logo, no watermark. This is decorative colony artwork, not a screenshot or a copy of any existing game's city.

### Building portrait atlas

Use case: stylized-concept. Asset type: ONE square 3 by 3 sprite atlas of nine original sci-fi colony-building portraits for Stellar Continuum strategy game. Exact layout: nine equally sized SQUARE cells, no gutters, no border, no text anywhere, no separators; each scene completely contained in its own cell with generous empty space near all edges so cells can be cropped independently. Each cell shows a distinct compact human facility from a consistent slightly elevated three-quarter cinematic camera, detailed photoreal game concept art with dark navy surroundings, silver/graphite architecture and vivid colored lights. Top row left: fusion power plant with round reactor and golden yellow electrical energy details. Top row center: advanced research laboratory with observatory dome and violet/cyan light. Top row right: industrial fabrication factory with cranes, orange furnaces and copper accents. Middle row left: interstellar commercial trading exchange with elegant gold-lit business towers. Middle row center: residential habitat arcology with blue glass stacked terraces. Middle row right: agriculture complex with luminous emerald glass greenhouses and organized crops. Bottom row left: water reclamation plant with sapphire blue pools and circular treatment tanks. Bottom row center: grid battery bank with yellow green illuminated vertical energy cells. Bottom row right: cargo terminal with teal-lit landing pad, warehouses and stacked containers. No people, no writing, no symbols, no UI, no brands, no watermarks. Each entire facility centered and fully within its cell; uniform scale, attractive sharp detail readable as small game cards.
