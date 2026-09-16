# Native presentation restoration

This pass repairs presentation lost during the C++ conversion. Core simulation,
research unlocks, strategic speed multipliers and Player17 persistence remain
authoritative. This is not a claim that every legacy screen has reached parity.

## Player-facing changes

- The main menu leaves the cinematic background unframed. New Game opens a
  separate pair of large image cards. Story Campaign is marked Coming Soon;
  Sandbox opens species selection and generation settings.
- Every fresh setup receives a random numeric seed. Players may replace or
  randomize it. The four galaxy sizes remain available, and rival/ancient
  civilization selectors now pass reviewed counts to the existing seeder.
- Fourteen compact navigation destinations use existing photographic artwork,
  consistent hover/active framing and matching pointer hit areas. Home and Map
  leave a workspace coherently; system zoom affects the visible system camera.
  Exploration selects a vessel and never silently issues a mission.
- Play/Pause and speed remain separate controls. Detailed system star rendering
  also applies to stars whose classification is unknown to the observer.
  Forest-green lane arrows have orange hover and labels outside their wide base.
- Research has eight subject categories plus All, search across known names and
  explanations, illustrated cards, a selected portrait, visible purpose/benefits,
  canonical research work and monetary estimates. The graph pans and zooms;
  inspector details scroll above a fixed action button.

## Existing artwork reused

The portable catalog comes from `work/module-research-thumbnails` commit
`411910f1` (content definitions pinned there to `3aeeb9a4`). All 99 original
illustrations and their 128/512-pixel deliveries are retained under
`assets/visual/catalog`, with their original generation/render provenance.
The current 370 research bindings share 21 family illustrations. The other
78 images illustrate station/alien modules. Those images do not add functional
modules or promote the 42 reserved research bindings into playable technology.

The native loader reads only current research bindings. The workspace resolves
images only for observer-projected nodes. Thumbnails share immutable image
identity by family; selected portraits use a 12-entry cache. Missing or malformed
declared assets fail build/package checks. Export includes the current research
images and explicit navigation dependencies, not the entire concept library.
`export/native-research-assets.json` pins catalog, provenance and image hashes.

## Remaining differences

Native seed entry accepts signed numeric seeds. The preserved client also
supports normalized text seeds; its exact normalization/hash contract has not
been ported. Galaxy-shape, density and other advanced generation controls must
be connected to canonical C++ options before becoming selectable. Richer
construction, diplomacy and surface presentation remains migration work.
This pass does not establish a sustained 60 FPS performance guarantee.

## Validation

Validated on Windows with MSVC and the native SDL3/Vulkan renderer:

- Complete native build passed. Final serial CTest run: **186/186 passed** in
  132.62 seconds (`work/ui-restoration-ctest-final.log`).
- Export/packaging Python suite: **598 checks; 581 passed, 17 optional skips**
  (`work/ui-restoration-python-final.log`). Missing/tampered artwork and Windows
  checkout line-ending checks are included.
- Relocated research, navigation, system and startup validators passed at 720p
  and 1080p. They exercise real input, research work, modal guards, camera input,
  independent new-game generation and paused save recovery. Startup audio passed.
  Results and captures are retained in `work/ui-restoration-runtime/`.
- Actual menu, separate mode cards, species setup, research and system BMPs were
  visually reviewed. Research titles fit, artwork remains contained and the
  primary action stays above the inspector's lower edge. The new-game validator
  requires menu and mode captures in addition to its prior four images.
- Known-only artwork lookup, immutable family caching, portrait cache bounds,
  navigation hit areas through 4K, graph pan/zoom and inspector scrolling have
  maintained native checks. A short 60 Hz smoke is not a sustained FPS claim.

Earlier combined testing exposed three UI/default/test-assumption failures,
which were corrected, plus an audio asset-read timeout and a platform test exit
`0xc000041d`. Audio/platform passed isolated reruns and the final complete serial
run. Their initial intermittent cause is unconfirmed; no suppression or audio
disable was added. Preserve that history if either failure recurs.

Game version is `0.1.8-alpha`; engine version remains `0.1.58`. The initial local runtime
under `work/ui-restoration-runtime/package` is an **unsealed validation package**.
It is not the delivered ZIP. The later complete export below supplies the download.

### Alpha 0.1.8 export version boundary

The first full export of `6ceef6ff` passed the 186 native tests and preceding
runtime checks, then correctly stopped at diplomacy's strict save comparison.
Its preserved Player17 fixture carries `GameVersion: 0.1.7-alpha`; a new save
correctly carries the running `0.1.8-alpha`. After existing fixture serialization
normalization, the only changed roots were Diplomacy, GameVersion and SavedAtUtc.

The validator now takes the exact expected version from the packaged
`Configuration/runtime-config.json`, checks saved metadata against it, and keeps
all unrelated-state checks and exact paused-reload comparisons. Missing/malformed
version configuration and incorrect versions still fail. The real 720p diplomacy
acceptance and 1080p paused reload passed this corrected check. No game rules or
save schema changed. The original failed export retains `EXPORT_FAILED.txt`; it
is not reused as a distributable package.

### Successful native Alpha delivery

The fresh official export of clean commit
`16b230b7f968acd320755f588df17c5843ae224f` passed all 186 native tests,
466 exporter Python tests and the complete relocated runtime sequence, including
diplomacy progress and exact paused reload. The official manifest validator
passed. All 182 manifest file hashes plus the manifest match the delivered ZIP;
it has no failure marker. Full graphical parity remains false.

- Official export prefix:
  `Builds/Windows/StellarContinuum-windows-native-preview-16b230b7-20260916T183840391372Z`
- Delivery: `D:/StellarContinuum/Downloads/StellarContinuum-Cpp-0.1.8-alpha-16b230b7.zip`
- Size: 82,904,435 bytes.
- SHA-256: `9c8c88e6c27cfbd7ced90667a609a786e89c05d071e3f5d97a2bd57fdf86ee30`.
- Launch after extraction: `stellar-continuum-native.exe`.
- Build log: `work/alpha-018-export-final.log`; validation JSON is beside the
  official export directory. The copy's hash equals the official archive.

C: reported zero free bytes, so delivery is on D:. No user files or saves were
deleted. Standard game saves/settings still use Windows LocalAppData, and the
player needs free space there for normal saving.
