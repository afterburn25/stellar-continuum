<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Sandbox galaxy creation implementation report

September 17, 2026. Native C++ implementation; verified in the actual executable.

## Player flow

Sandbox opens **Choose Galaxy Type**, then **Galaxy Population State**, then
the existing species, seed, galaxy size and civilization configuration screen.
The first page presents exactly six cards in three columns and two rows:
Spiral, Barred Spiral, Elliptical, Lenticular, Irregular and Ring. Next requires
a selection. Back/Next retain choices. Reset clears morphology and restores
Random population and a fresh automatic seed. Story-mode routing is unchanged.

The population dropdown contains Random, Starburst, Active, Mature, Aging and
Quiescent. Descriptions explain each choice. The summary and Copy Setup use
the resolved authoritative configuration; changing seed or morphology updates
Random deterministically. Reproduction wording requires matching generation
settings as well as the seed.

## Artwork and map

All twelve supplied images were classified by normalized filename. Selection
cards use only Stars Included; actual galaxy maps use only the paired Gas-Dust
Only layer behind real generated systems. Six generic pairs cover all thirty
morphology/population combinations through a same-morphology fallback. There
are no supplied population-qualified pairs, ambiguous identities or duplicate
mappings. Fallback use is reported in developer diagnostics.

The complete filename/morphology/population/variant table is in
[GALAXY_ASSET_REPORT.md](GALAXY_ASSET_REPORT.md). The canonical manifest contains
source and runtime dimensions, hashes, paths, masks and footprint transforms.

Both Irregular and Spiral pairs now have matching 16:9 framing. Their runtime
images are exactly 1280 × 720, with the complete galaxy visible. Original user
files remain unchanged. Edited sources, final prompts and provenance are in
[GALAXY_WIDESCREEN_ART.md](GALAXY_WIDESCREEN_ART.md) and
`export/galaxy-asset-edits.json`. Reimporting preserves these explicit edits.

Cached 96 × 96 density masks constrain the existing procedural morphology
sampler. They reject black margins without deciding star types or replacing
population physics. Minimum-distance and central-exclusion rules still apply.
The artwork transform accommodates the measured nearby-star coordinates;
galaxy extent scales with the square root of system count. The full overview
fits the available map area clear of the right navigator and header. Closer
views retain panning and gradual detail transitions.

## Generation identity and persistence

`GalaxyGenerationConfig` is the single resolved configuration used by setup,
generation, saves and diagnostics. Its canonical SHA-256 fingerprint includes
stable morphology ID, requested and resolved population, seed, system count,
civilization settings, species, developer coverage and generator/art/profile
versions plus selected asset IDs. Random and an explicitly selected equivalent
state remain distinguishable configurations.

Random uses a dedicated deterministic stream and configurable morphology
weights. Elliptical and Lenticular favor older populations; Irregular and Ring
favor active populations; Spiral and Barred Spiral favor mixed intermediate
states. Existing base, morphology, population and regional modifiers remain
authoritative for stellar composition, hazards and rare objects.

SMBH eligibility, size threshold and occupancy probabilities are configurable.
Normal galaxies can contain zero or one central SMBH. Absence creates no dummy
entity. Existing discovery rules remain in force. Explicit full-coverage
developer generation forces one eligible central object for QA; this choice is
part of configuration identity. Saved actual positions, objects, occupancy,
population and pairing restore without rerolling. Legacy saves without the
optional canonical configuration remain supported.

## Principal files added

- `core/include/stellar/core/galaxy_configuration.hpp`
- `core/src/galaxy_configuration.cpp`, `galaxy_configuration_data.hpp.in`
- `engine/include/stellar/engine/density_mask.hpp`
- `app/native_client/native_galaxy_creation.inl`
- `data/stellar/galaxy-generation-v2.json`, `galaxy-visuals-v1.json`
- `tools/stellar-export/import_galaxy_assets.py`, `test_galaxy_asset_import.py`
- `export/galaxy-asset-edits.json`
- Four edited source images under `assets/source/galaxies-16x9/`
- Twelve normalized runtime images under `assets/visual/galaxies/`
- `native-tests/galaxy_configuration_tests.cpp`
- This report, the artwork mapping report and widescreen provenance report

## Principal files extended

- Core fresh-campaign generation, persistable options, generation metadata and
  JSON save/capture/restore integration
- Native new-game workspace, startup routing, campaign generation, support
  diagnostics, galaxy backdrop and `main.cpp`
- `CMakeLists.txt`, `cmake/NativeGalaxyArtAssets.cmake` and native test wiring
- `export/native-galaxy-art-assets.json` and `native_galaxy_art_runtime.py`
- Existing native startup, setup, workspace and backdrop tests
- `.gitattributes`: provenance text has explicit CRLF checkout behavior to
  preserve its approved packaged hash
- `docs/ENGINE_CAPABILITIES.md`

Other intentional changes already present in this branch were preserved.

## Verification

| Check | Result | Evidence |
| --- | --- | --- |
| Final complete native regression | 205/205 pass, 205.22 s | `work/galaxy-final-regression.log` |
| Export/package Python checks | 68/68 pass | `work/galaxy-package-tests-final.log` |
| Galaxy asset importer | 3/3 pass | `work/galaxy-widescreen-assets-final.log` |
| All six morphologies × six population selections | Exact repeated generation and save/load pass | `galaxy_configuration` native test |
| 250/500/1000/2500 systems | All six morphologies generate successfully | `galaxy_configuration` native test |
| Nearby stars and generated footprint | No empty-margin placements in tested configurations | `galaxy_configuration` native test |
| Random and SMBH occupancy | Deterministic, variable across seeds; zero/one rules pass | `galaxy_configuration` native test |
| Responsive setup layout | 720p/1080p/1440p/3440×1440/4K | Native workspace/backdrop tests and actual captures |
| Actual native creation | Six morphology runs pass | `work/galaxy-{spiral1080,barred-ultrawide,elliptical4k,lenticular720,irregular1440,ring1080}.log` |
| Actual native reload/navigation | Six map runs pass | `work/galaxy-map-*.log` |
| Fleet controls and illustrated shipyard | Hold/resume, locate and art browse pass | `work/concept-fleet-recovery-reviewed.log` |

Actual creation captures are under `work/galaxy-captures/widescreen-*`.
Reload captures are `work/galaxy-captures/map-*-1080.bmp` plus regional/system
variants. Reload evidence verifies real system markers, undisclosed names,
matching gas artwork, regional background transition, explored-system entry
and an unchanged paused campaign day. These tests do not prove a universal
frame-rate guarantee or an entire campaign playthrough.

### Subsequent background replacement

The user-supplied deep-space background is now the fixed overview layer behind
the main galaxy. `assets/visual/space/deep-field-v3.png` is byte-identical to the
supplied 2944 × 1648 original. Source provenance and packaged hash are recorded
in `docs/engine/NATIVE_GALAXY_ART_SOURCES.md`. The established overview-to-regional
fade is retained; the image is absent inside solar systems.

The existing asset preparation service now reserves a separate 20 MiB maximum
for this full-resolution background, preserving the existing 8 MiB bound for
other map images and 32 MiB shared queue limit. CMake watches the galaxy manifest
so changed asset declarations update existing build trees correctly. No seed,
galaxy contents or save data changed.

Follow-up verification passed 2/2 preparation/backdrop tests, 68/68 export tests,
all six 1080p galaxy reload/zoom/system-entry scenarios and a 4K Spiral scenario.
These runs are logged as `work/galaxy-background-*.log`; actual captures are
`work/galaxy-captures/background-v3-*-1080.bmp` and
`work/galaxy-captures/background-v3-spiral-4k.bmp` with regional/system variants.
The earlier complete 205-test regression predates this background follow-up.

## ENGINE CAPABILITIES ADDED / EXTENDED

Reusable bounded density-mask sampling; canonical versioned configuration and
deterministic fingerprints; whole-pair asset resolution and diagnostics;
configurable central occupancy; optional save metadata; reusable artwork fit
inside a supplied content rectangle. The capability registry documents APIs,
consumers, tests, persistence/performance impact and future reuse.

## ENGINE LIMITATIONS REMAINING

The supplied artwork has six generic pairs, not thirty population-specific
pairs. This is an explicit supported fallback, while population changes actual
generation. Exact reproduction assumes matching generator/profile/art versions
and all generation settings.

The wider engine batch still has separate authoritative planetary-region,
multi-yard construction, developer-QA and release-package gates recorded in
`docs/ENGINE_CAPABILITIES.md`. This report completes the tested galaxy-creation
work; it does not certify those other features or a new release package.
