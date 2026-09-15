# Native startup artwork sources

These are the existing approved Stellar Continuum images, reused unchanged by the C++ client.

- `stellar-continuum-splash.png`: original cinematic main-menu artwork generated for this project with OpenAI image generation on 2026-09-09; see repository ASSET_MANIFEST.md.
- `stellar-loading-splash.png`: user-supplied application-loading scene, edited with OpenAI image editing on 2026-09-11 to remove its baked loading UI.
- `stellar-galaxy-generation.png`: user-supplied new-galaxy scene, edited with the same tool to remove its baked loading UI.
- `stellar-save-loading.png`: user-supplied saved-game loading scene, edited with the same tool to remove its baked loading UI.

No external creator or license is inferred for the user-supplied images. Original source filenames, exact edit prompts and image fingerprints are retained in `docs/art/LOADING_SPLASH_PROVENANCE.md`. The application renders live controls, loading status and progress separately; no image contains a fake operational progress bar. This native integration does not modify the artwork.

Only these four PNGs and this scoped source note are included by `export/native-startup-art-assets.json`. SHA-256 verification fails packaging if an approved asset is missing or altered.
