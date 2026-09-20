# Native startup artwork sources

These are the existing approved Stellar Continuum images, reused unchanged by the C++ client.

- `stellar-continuum-splash.png`: original cinematic main-menu artwork generated for this project with OpenAI image generation on 2026-09-09; see repository ASSET_MANIFEST.md.
- `stellar-loading-splash.png`: user-supplied application-loading scene, edited with OpenAI image editing on 2026-09-11 to remove its baked loading UI.
- `stellar-galaxy-generation.png`: user-supplied new-galaxy scene, edited with the same tool to remove its baked loading UI.
- `stellar-save-loading.png`: user-supplied saved-game loading scene, edited with the same tool to remove its baked loading UI.

No external creator or license is inferred for the user-supplied images. Original source filenames, exact edit prompts and image fingerprints are retained in `docs/art/LOADING_SPLASH_PROVENANCE.md`. The application renders live controls, loading status and progress separately; no image contains a fake operational progress bar. This native integration does not modify the artwork.

The recovered `stellar-continuum-title-v1.png` is the exact approved generated title image (SHA-256 `9040a67106ccd463fb81af97b7d07ead79559347e80763bbe028aea2277eb124`), recovered from the generation archive recorded by `assets/visual/branding/icon-provenance-v1.json`. The existing `campaign-galaxy-four-arm-v1.png` supplies the Sandbox card without loading-screen text.

These nine PNGs and this scoped source note are included by `export/native-startup-art-assets.json`. SHA-256 verification fails packaging if an approved asset is missing or altered.

The bottom view switch uses the following images supplied by the user on 2026-09-17, copied byte-for-byte. Galaxy view is shown inside a solar system; system view is shown on the star map.

- `assets/visual/hud/galaxy-view.png`: `afterburn25._small_galaxy_icon_--v_8.2_cae5f9e9-1a0d-4866-bd67-f8bac4a79eea_2.png`.
- `assets/visual/hud/system-view.png`: `afterburn25._small_solar_system_icon_--v_8.2_9cf1fd2f-5215-4d01-abbd-fa98d81a2aa3_3.png`.
