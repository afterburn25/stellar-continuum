# Native navigation photo sources

Every native navigation action has an approved photo source recorded, with its
SHA-256 digest, in `export/native-navigation-assets.json`. The five navigation
actions that open economy, research, construction, relations, and settings use
the approved catalog portraits; the rest use existing solar-system, deep-space,
and ship artwork.

`NativeNavigationArt` decodes each source and constructs one opaque 256px
thumbnail during startup. It keeps fourteen immutable thumbnails (3.5 MiB
total) and returns the cached object on every frame. Zoom button glyph overlays
remain part of the main UI; their photo thumbnails are separate background art.

The build validates every declared source and runtime path against the reviewed
hashes, then stages the source images. There is no raster regeneration step:
the checked-in photos are the shipped assets.

Validate the declaration from the repository root:

```powershell
python tools/stellar-export/test_native_navigation_assets.py
```
