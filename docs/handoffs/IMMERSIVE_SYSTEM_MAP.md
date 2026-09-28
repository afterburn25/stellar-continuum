# Immersive system map handoff

Updated: 2026-09-10. Branch: `work/immersive-system-map`. Integration baseline: `5763a1e` (`integration`). This handoff records the current working tree candidate; no commit or direct integration change was made here.

## Demonstrated presentation

- The solar-system view is a real perspective 3D scene with rotatable camera orientation, zoom, planet picking, moons, orbital infrastructure and station geometry. The camera route supports orbit approach, atmospheric descent and local colony surface entry.
- Planet focus preserves the known system's other orbital bodies and supports Earth atmosphere-to-surface descent when a local colony is available. Hidden/closed systems release their body and infrastructure caches.
- Saturn uses a thin annular mesh with fine ring bands and divisions, an equatorial ring plane sharing the globe's axial tilt, and a focus distance that includes its rings. The user-provided Stockcake image is a visual reference only and is not bundled.
- Orbital facilities have distinct staged shipyard, launch-complex and mining designs, shared by the actual world and inspector preview. Facilities occupy separate stable presentation slots and picking selects the nearest projected site, with an exact shipyard-identity assertion.
- Surface sky companions come only from the active planet's observer-safe moon markers. Their local sky placement and phase are cosmetic because the read model does not provide orbital epochs/distances; they do not add moons to the simulation.
- Galaxy presentation uses a dust/nebula shader field, star-class spectral colors and a navigable overview/region route instead of the previous flat sparse treatment.
- NASA Earth, Moon and cloud maps plus Poly Haven CC0 ground albedo, OpenGL normal and roughness assets are recorded in [`docs/IMMERSIVE_TEXTURE_PROVENANCE.md`](../IMMERSIVE_TEXTURE_PROVENANCE.md).
- Main menu campaign confirmation is intended to be an embedded cinematic modal with explicit safe cancellation; the latest full UI capture remains pending after the current Sol repair.

## Windows/video evidence

The candidate's video settings expose detected Windows resolutions, Windowed/Borderless/Fullscreen, V-Sync, MSAA and 75/100/125% 3D render scale. Fullscreen explicitly uses desktop resolution; the window-size selector is disabled in those modes. RTX 3080 Ti checks exercised 1280×720 and 1920×1080, including Keep/Revert, persistence and the 15-second rollback path (7 checks per resolution, `work/final-video-720` and `work/final-video-1080`). NVIDIA Control Panel is exposed only as an explicit launch action with visible launch errors; the game does not change driver settings or claim DLSS/ray-tracing support.

The native Windows pointer probe passed at 720p, 1080p, 2560×1369, 1440p, 4K, restored 720p and maximized (`work/native-pointer-current`). It posts client messages to its own window and tests actual rail/drawer controls. The responsive repair defers/coalesces size updates and avoids redundant letterbox transforms. The screenshot input helper separately recalculates coordinates after deferred layout and reasserts its synthetic pointer before release so unrelated desktop input cannot cancel a test press.

## Validation

- Simulation validation: 69/69 passed (`work/immersive-validation/simulation.log`).
- Core runtime validation: 70/70 passed (`work/immersive-validation/core-runtime.log`).
- Quality validation: 18/18 passed (`work/immersive-validation/quality.log`), with the existing CA2014 analyzer warning only.
- Immersive 1080p camera journey passed with clean runtime diagnostics (`work/immersive-review-9`): real perspective rotation, 9 Sol bodies retained at Earth focus, wheel descent through orbital/atmospheric/local terrain stages and return without immediate reentry. Subsequent Saturn/sky/station refinements require final combined verification.
- Full UI/screenshot validation is **PENDING** until source freeze. Intermediate runs exposed and repaired actual station overlap, obsolete sprite-type test assumptions, hidden-camera observations in regional assertions and the HUD refresh wait after a completed upgrade. Failed/interrupted/concurrently edited runs are not accepted evidence.

## Boundaries and remaining work

- Colony terrain is representative local terrain. It does not stream Earth geography or provide unrestricted exploration of a full planetary surface.
- Building and station geometry remains procedural/placeholder in places and still needs authored production assets.
- The exact general 30-minute full slice has not been proven as a human play session; only bounded simulation and targeted presentation routes have evidence.
- Keep the existing acceptance requirement for real input/render/save behavior. Do not promote this branch based solely on process exit status or headless startup.

## Packaging and release gates

The supported Windows package flow is defined in `.github/workflows/windows-demo.yml` and `scripts/package_windows_demo.py`: export with the pinned Godot 4.7.2 .NET templates as `Windows Demo`, copy public `data`, validate the PE/resource pack/self-contained runtime contracts, write `BUILD.json`, then run `scripts/smoke_windows_demo.py` only in hosted Windows CI against the actual exported x64 executable. The hosted workflow also performs the Linux export smoke and uploads package/runtime diagnostics.

The ordinary build/smoke gates remain those in `.github/workflows/build.yml`: Release build, explicit Debug assembly for the Godot project runner, pinned Godot editor/runtime smoke, and required positive runtime markers with error-log rejection. Screenshot acceptance is defined in `.github/workflows/screenshots.yml` and `scripts/validate_screenshot_capture.py`; exact-head captures must pass their evidence contract. Final acceptance therefore requires combined simulation/core/quality checks, clean Godot semantic smoke, exact-head UI captures, package integrity, and hosted Windows startup—not only local managed validation.

## Successor milestone

The work was carried into `work/premium-art-visual-identity`. See `docs/PREMIUM_ART_VISUAL_IDENTITY.md` for the final rendering scope, the successful 32-capture / 120-input native candidate review, and remaining quality gates. The earlier full-suite-pending notes above describe intermediate attempts and are superseded by that evidence.
