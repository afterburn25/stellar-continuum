# Native navigation icon sources

The native navigation rail uses the existing semantic SVG icons from the Godot
presentation layer. Each source stays unchanged in `assets/visual/ui/navigation/`
or `assets/visual/icons/navigation/`. Native runtime PNGs are transparent 256 px
rasters of those sources in `assets/visual/native-navigation/`; they add no new
icon design or generated replacement art.

`export/native-navigation-assets.json` maps every action to its source SVG and
runtime PNG, records both SHA-256 hashes, and records the rasterization method.
The Python exporter and CMake build validate the paths and hashes before packaging
or staging; only the runtime PNGs are shipped for this feature. `NativeNavigationArt`
loads those immutable RGBA images once at startup. The thumbnails remain 256 px
each (3.5 MiB total), preserving transparent symbol padding.

The mapping follows the Godot rail: galaxy, home, inspection, zoom in/out,
economy, research, shipyard, construction, exploration, colonization, logistics,
relations, and settings. Zoom glyphs are already part of the selected SVG and
remain visible without photo overlays.
