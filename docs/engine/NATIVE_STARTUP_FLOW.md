# Native campaign startup

The C++ client opens a startup menu before creating a campaign. New Game opens a separate mode-selection screen with large independent artwork cards: Story Campaign remains unavailable and Sandbox opens configuration with four existing species portraits, biographies and canonical environmental tolerances. Species selection is the first configuration decision. The player then selects 250, 500, 1,000 or 2,500 systems; 0, 3, 5, 8 or 12 rival empires; None, Rare or Standard ancient empires; and a signed 64-bit numeric seed. Those rival choices are translated to the core seeder's inclusive pre-warp civilization counts of 1, 4, 6, 9 and 13; the ancient choices map directly to 0, 1 and 2. A fresh numeric seed is supplied for each new setup and can be edited or randomized. The configured campaign preserves its species, system count, civilization counts and seed in generation metadata.

The preserved C# client also hashes memorable text seeds after Unicode FormKC normalization. Native startup intentionally accepts numeric seeds only until it has that exact normalization-and-SHA256 compatibility path; accepting text without it would make a shared seed describe different galaxies.

## Simulation ownership

`NativeNewCampaignSetupController` validates and freezes the input. The generation worker loads the catalog and research definitions and seeds detached campaign data. It never creates the integrated simulation on a worker. `NativeStartupSessionController::service` activates the integrated runtime and its thread-bound lane/logistics services on the UI owner thread, then transfers the completed session exactly once. Campaign time starts after activation; generation delay is not simulated campaign time.

The loading path similarly parses a selected Player17 save off the UI thread and activates it through `NativeCampaignSession::create_loaded`. A failed selected load never falls back to a new campaign. Terminal failures, cancellation and consumed results remain stable across repeated service calls. Unknown exceptions and launch failures produce diagnostic failure states rather than escaping worker threads.

## Progress and recovery

Generation reports real named phases and an indeterminate busy display. The canonical seeding stage has no fractional progress callback; this implementation does not invent percentages. Save loading displays finite, monotonically increasing progress from its loader. Cancellation discards worker results at safe phase boundaries; an in-progress canonical seed can finish before discard. Startup controls continue polling window, focus and minimize events.

New Campaign chooses an unused save name when activation completes. The configured default is used if absent, otherwise a numbered `-native-N.player17.json` sibling is selected. Startup itself performs no save writes. Load Campaign lists the configured native save and its numbered siblings, up to the sixteen most recently modified slots; unrelated files and backup files are excluded. The scrollable list clips its text and hit areas above the fixed Back/Load controls. Existing in-campaign Escape save/load behavior is retained.

## Artwork and layout

The four existing project species images are declared in `export/native-species-assets.json`, verified during CMake and export, and accompanied by scoped credits. Images use the Engine's ordered UI layer and shared immutable cache. Portraits are contained within their bounds; measured species facts scroll separately from the fixed portrait/name, seed, size selectors and Create action. Canonical environment ranges remain unchanged; their display intersects physical nonnegative limits. These are habitat tolerances, not invented flat species bonuses.

Current startup UI is functional and responsive at720p/1080p, with layout tests through4K. The main menu leaves the approved artwork full-bleed and unframed; only its title and actions are overlaid. The approved cinematic startup/menu/loading art, richer screen transitions and audio are separate presentation work. This feature does not claim final visual quality or sustained60FPS.

## Maintained evidence

Five CTest registrations cover input preparation, detached generation, species setup layout, session ownership/recovery and startup routing. Tests include actual galaxy sizes, seed bounds, cancellation, worker failures, wrong-thread use, repeated activation, malformed loads, Unicode save names and sixteen-slot720p clipping.

`native_new_game_runtime.py` performs two actual relocated Vulkan launches with restricted PATH and a different working directory. A maintained valid Player17 fixture supplies only the preexisting save to protect. Real UI input chooses Pelagic,250systems and seed143250; no simulation resources or capabilities are injected. The resulting independent save and metadata must match the selected inputs. An ordinary paused1080p reload must preserve the complete payload exceptSavedAtUtc. Six BMP sidecars retain the main menu, mode cards, setup, generation, new-campaign and reload evidence.

The32Python validator tests reject missing input flags, changed choices, spoofed paths, overwritten original saves, missing or altered payloads, invalid BMPs, absent Vulkan/save evidence and paused-state changes. UTF-8 decoding is explicit; Unicode diagnostic paths must bind to real isolated files. Combined0.1.54 validation passed143CTest,245Python tests and28actualVulkan launches across all native interaction families. The 054 local export subsequently exposed a reused scratch-fixture failure; 055 isolates each test invocation. Final clean-export evidence is recorded in PR325 and issue324.
