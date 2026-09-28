# Cinematic presentation, mouse orders and timed construction

Branch: `work/cinematic-presentation-rebuild`. Base integration: `13ea62f0058cd1cff3a5317c7b7334b44c231f6a`.

The user requires a polished, playable 100-system foundation: compact mouse navigation, organized planet/ship details, distinctive local space, timed actions and visible orbital construction. Primary reference is **1920×1080**, with an actual reflow at 1280×720 and native 1440p/4K scene rendering. No bottom command toolbar remains.

## Implemented candidate

- Original galaxy/deep-field artwork, nine-slice interface frames, native celestial shaders and deterministic system-specific star/nebula skies. Galaxy camera fits both the image and generated catalog. Galaxy imagery is absent from system and planet views.
- Compact left icon rail, grouped world/moon inspector and an empire outliner that switches to the selected ship or orbital structure. Colony facts and construction controls occupy a scrollable right panel.
- Select exact ship icons and right-click destinations. Existing lane reach, speed, fuel and territorial rules remain authoritative. Scouts and science ships can reposition to completed systems. Colony ships travel without automatically settling; right-clicking a world authorizes settlement. Freighters may reposition when not committed to a collection/delivery run.
- Home-system shipyard, launch complex and asteroid-network sites show real 3D meshes, build stages, costs, upkeep, requirements and minimum completion times. Their output still comes from the maintained infrastructure simulation.
- Infrastructure work is capped at 30 materials/game-day; shipbuilding at 20. Scouting takes two on-site game days; colony establishment takes 30 and sealed outposts 20. Existing science survey and research work remain timed. Resource shortages and operational funding can extend these estimates.
- Module and hub upgrades reserve payment once, retain old output/capacity during work, and persist pending completion through save/load. Zero elapsed time cannot finish normal construction or produce a ship/settlement.

## Validation and crash-safe checks

The shared simulation suite passes **69/69**, core runtime **70/70**, quality **18/18**, and Adaptive Research runtime checks pass. Visual asset validation and its nine regression tests pass. Screenshot contract tests cover the new 1080p/1440p/4K captures as well as 720p.

Final review also reproduced and repaired stale settlement progress when an AI colony ship abandoned an occupied site and selected a replacement route. The regression verifies work-state cleanup and campaign save/load after retargeting.

The three general validation executables previously ran regression groups in CLR module initializers. An assertion there could escape before `Main` began. They now run those same groups through a maintained console runner that reports the original exception, inner stack, assembly path and working directory and exits nonzero. Failed checks continue reporting the remaining groups. This is separate from the previously repaired temporary topology helper; no scratch executable is introduced.

Actual GPU/input review is performed through `tools/ScreenshotCapture.tscn`. Required new cases include organized planet selection, stable distinct local skies, exact ship selection and noninstant travel, 3D orbital models, and reflow/input at 720p, 1080p, 1440p and 4K. Treat its complete manifest and clean log as the release gate; individual screenshots are insufficient. Final results belong on the PR/build record for its exact commit.

## Limits and next art work

Orbital and surface structures are original procedural meshes, not finished authored production models. Orbital projects are still civilization-level facilities anchored visually to the home world/asteroid region; per-colony orbital inventories and arbitrary orbital placement are not implemented. Ships use existing interstellar strategic routes; free in-system AU flight is not implemented. Surface terrain remains a bounded colony area, not a streamed planetary globe. Bitmap galaxy detail is finite; stars, labels and planet lighting render separately at viewport resolution.

Review station silhouettes, surface street composition, action feedback and travel pacing before adding another subsystem. Keep original asset prompts in `CINEMATIC_ASSET_PROVENANCE.md`, and keep player controls consistent in `WINDOWS_DEMO_README.txt` and `CINEMATIC_MAP_AND_SURFACE.md`. Do not stage `.uid`, `.import`, scratch executables, local capture directories or unrelated untracked ship PNGs.
