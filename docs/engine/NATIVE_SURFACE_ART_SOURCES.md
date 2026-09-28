# Native surface art source

The native client packages one existing approved source image unchanged:

- `assets/visual/surface/temperate-ground-albedo-v1.png`: seamless, top-down
  soil/grass/gravel albedo detail, originally generated for this project with
  OpenAI's built-in image generation tool on 2026-09-10. Its source prompt and
  production status are recorded in `docs/ASSET_MANIFEST.md`.

The image is a generic repeating ground detail. It does not identify a colony's
climate, biology, geography, or exact planet surface. Native surface rendering
uses a neutral, desaturated tint when its observer-safe view has no environment
classification; it does not claim full 3D terrain or planet reconstruction.

Only this image and this source note are declared by
`export/native-surface-art-assets.json`. Packaging verifies their exact paths
and SHA-256 fingerprints. The client keeps at most one decoded source image,
bounded to 16 MiB, through the shared image-preparation queue.
