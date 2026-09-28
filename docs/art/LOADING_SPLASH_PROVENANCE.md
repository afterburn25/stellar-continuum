# Contextual loading artwork

The three artworks serve distinct operations: application startup, new galaxy generation,
and saved-game restoration. Each uses a live progress display and a randomly selected
beginner tip. The gameplay seed does not control or depend on tip selection.

## Application startup

The user supplied `897309b4-1156-407c-9155-d6f96cb02a39.png` on 2026-09-11 and requested that it replace the loading artwork while retaining the existing main-menu screen. No creator or license claim is inferred from the supplied file.

The supplied raster included a fixed 61% progress bar. OpenAI's built-in image editing tool removed that baked loading display, retaining the scene, title, and corner inscriptions. The game supplies the actual progress bar, percentage, and loading status.

- Runtime asset: `assets/visual/loading/stellar-loading-splash.png`
- SHA-256: `38344B1243DF70006FC317F0E222A8E9B5F640FB145663CC2F01CD2B74BB9ECC`
- Method: built-in image editing; no fallback API or CLI.
- Existing main-menu image: `assets/visual/loading/stellar-continuum-splash.png`.

Exact edit prompt:

> Edit target: the supplied Stellar Continuum loading-screen artwork. Precise local inpainting only. Remove ONLY the lower center loading UI: words LOADING GAME..., the cyan progress bar and frame, the 61% number, and the small line Preparing menus, audio, and interface. Fill just those marks with seamlessly matching dark blue space/nebula background. Preserve the entire rest of the image as exactly as possible: same scene, crop, aspect ratio, station, Earth, ships, asteroids, all corner slogans and insignia and especially the STELLAR CONTINUUM title and logo. Do not redesign, add objects, move anything, or add any new text/UI. Output a clean game background with room for a real runtime loading bar in its original lower-center position.

## New galaxy generation

- User-supplied source: `46973e5a-1398-4389-b119-f6bc45562365.png`, 2026-09-11.
- Runtime asset: `assets/visual/loading/stellar-galaxy-generation.png`.
- SHA-256: `B6445C53DF9E3B29101FF95F64D2A05A041C2F94C964A3BF69B1C30F57A59F5F`.
- Method: built-in image editing. No creator or license claim is inferred.

Exact edit prompt:

> Precise local edit of supplied Stellar Continuum galaxy-generation loading screen. Remove ONLY lower central overlaid UI: 'Generating New Galaxy...', 'Seeding star systems, civilizations, and stellar phenomena', the cyan progress bar and outline and its tiny decorative flanking arrows. Seamlessly replace those marks with matching dark space/star background. Preserve all other pixels/content as closely as possible: exact galaxy shape and colors, central glow, surrounding galaxies and planets, title logo at top, corner slogans, crop, aspect ratio and composition. No redesign, no extra objects, no new text. This will be the runtime background with a real progress bar added by the game in the same position.

## Saved-game restoration

- User-supplied source: `820c2132-eaf3-4774-9f88-92340d813af0.png`, 2026-09-11.
- Runtime asset: `assets/visual/loading/stellar-save-loading.png`.
- SHA-256: `FACAC0A68F057DE5FBC724D6702EC92FF6564F5D4A1C7202DA6C16C0679E49E9`.
- Method: built-in image editing. No creator or license claim is inferred.

Exact edit prompt:

> Precise local inpainting only on supplied Stellar Continuum saved-game loading artwork. Remove ONLY lower-center overlaid UI: words 'Loading Saved Game...', the cyan progress bar and outline, and words 'Restoring galaxy state, fleets, colonies, and diplomacy'. Replace removed marks with seamless continuation of the dark planetary night side / space backdrop behind them. Preserve the exact rest of the composition as closely as possible: title/logo, Earth day-night boundary and city lights, sunrise, orbital station, ships, moon, asteroids, colors, crop and aspect ratio. Do not redesign or add anything. Output clean artwork without that lower loading UI so the game can draw real progress and a tip in the original lower-center area.
