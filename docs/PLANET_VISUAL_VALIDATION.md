# Planet identity validation

Branch: work/planet-classes-art-and-celestial-skies, based on integration 97091aee.

Local platform: Windows x64, Godot 4.7.2 Mono, .NET 8, NVIDIA RTX 3080 Ti, OpenGL compatibility renderer.

## Results

| Check | Result |
|---|---|
| Game Debug and Release compilation | Passed |
| Quality regression suite, including five new identity groups | 25/25 passed |
| Core simulation validation | 72/72 passed |
| Native source gallery | 110 captures, 55 cases at each of 1920×1080 and 1280×720 |
| Gallery scene ownership | 394–403 nodes during repeated class/system switches |
| Source gallery video memory | Approximately 104–237.5 MiB |
| Source gallery process memory | Approximately 470–801 MiB |
| 2,500 systems / 20,000 derived planet identities | 42 ms in the initial local run; 19,798 distinct shader seeds; all 100 variants represented |
| Godot import and startup | Passed; STELLAR_RUNTIME_READY IntegratedMain confirmed |
| Integrated real-input playthrough | Passed: ten Sol bodies, perspective rotation, wheel descent, atmosphere, ground and return to orbit |
| Windows self-contained export | Passed; research data and managed runtime validated |
| Windows executable startup | Exit 0, real integrated runtime-ready and save confirmation |
| Exported Windows atlas | Exit 0; all 110 native captures passed again using the packaged executable |
| Exported atlas resource range | 394–403 nodes; about 146–277 MiB video memory; 543–892 MiB process memory |

These measurements describe synthetic renderer specimens and one local GPU. They are not an FPS guarantee, a universal GPU-memory budget, or a benchmark of a 2,500-system campaign with every gameplay subsystem running.

The screenshot list covers every class in orbit, every solid class on the ground, parent giants/rings from moons, warm and cool stellar light, binary/triple stars, nebula and sparse/inner fields, populated night lighting, unknown worlds and partial surveys. Giant surface controls remain unavailable.

## Review findings addressed

- Replaced excessive derivative-based micro-contrast that made globes and ground grainy.
- Kept atmospheric fog from flattening the entire sky and hiding binary stars.
- Corrected ocean coverage to avoid major continents on global-ocean variants.
- Added geometric scenic variation while preserving all authoritative build-area heights.
- Framed parent planets/rings within the moon-sky view and kept their phase consistent with the light direction.
- Released temporary replacement terrain nodes and flushed capture-owned resources before exit.
- Verified 2D portraits use the same procedural material and preserve unknown-world concealment.

Canonical Solar System image bytes are unchanged. Earth remains visibly Earth in the integrated orbit/descent path. The new classification label does not change its terrestrial physical environment.

## Reproduce

Run the Game.Quality.Validation and Game.Simulation.Validation projects in Release. Build Game.csproj in Debug before running the Godot project.

Set STELLAR_PLANET_CAPTURE_DIR to an empty output directory and run the Godot editor executable with --path . tools/PlanetVisualGallery.tscn. Validate the resulting directory with scripts/validate_planet_gallery.py and its --log argument. The validator checks expected native dimensions, required cases, log completion and bounded scene counts. scripts/build_planet_evidence.py creates inspection sheets without changing the original captures.

For the integrated playthrough, run tools/ScreenshotCapture.tscn with STELLAR_CAPTURE_FOCUS=immersive and STELLAR_SCREENSHOT_DIR set to a disposable output. Use a separate APPDATA/profile so no player campaign is affected.

The exported game supports -- --stellar-planet-atlas to open the gallery before campaign initialization. Export templates intentionally reject arbitrary scene-path overrides, so use this explicit application option.

Representative untouched captures and machine-readable metrics are in docs/evidence/planet-identity. Full native captures and integrated playthrough images remain in the local proof output.

## Remaining art work

ComfyUI image generation was not available. The 200 job specifications are ready, but licensed checkpoint selection, generated master production, native-resolution review, seam correction and approved import remain pending. The delivered runtime is a procedural production candidate, not a claim that the final external photorealistic art library has been completed. Precise orbital ephemerides, magnetic-field auroras and time-dependent eclipses remain outside the current physical model; their profile descriptors must not be mistaken for simulated facts.
