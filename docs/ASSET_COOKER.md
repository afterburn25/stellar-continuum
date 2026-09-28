# Native asset cooking and portable builds

The Windows C++ cooker produces a separate runtime library from the reviewed
native asset allowlists. It never rewrites or deletes master art. Runtime
packages contain prepared material maps, mip chains, definitions, fonts and
compressed audio, rather than a second copy of the master PNG/SVG library.

## Add or classify content

Use the existing `export/native-*-assets.json` catalog for its owning system.
`path` plus `sha256` describes a prepared runtime input. An import with both
`source` and `runtimePath` and `runtimeSha256` cooks the reviewed runtime raster;
the master reference is retained only in the source catalog. This distinction
matters for SVG navigation icons. All expected hashes must match.

Optional `export/cooker-assets.json` entries override discovery by stable `id`.
Each has `source`, `state`, optional `sha256`, `category`, `package`, `aliases`
and `dependencies`. Sources are safe relative paths. IDs are independent of
source paths and use case-insensitive lookup. Changing a source directory while
retaining its ID does not change persisted game appearance identifiers.

Supported states: `ACCEPTED_RUNTIME`, `SOURCE_ONLY`, `REJECTED`, `QA_ONLY`,
`EDITOR_ONLY`, `DEBUG_ONLY`, `DEPRECATED`. Release admits accepted content only;
QA also admits QA content; development additionally admits editor/debug content.
Unreferenced files are never admitted merely because they exist in a folder.
Previously rejected planet and sky pools remain outside their native allowlists.

## Build and inspect

From a Visual Studio x64 developer PowerShell, after configuring the normal
`windows-native-preview` CMake preset:

```powershell
cmake --build build-native/preview --target StellarCooker --parallel 4
build-native/preview/cooker-tools/StellarCooker.exe --root . --profile release --output work/cooker/release --report work/cooker/release-report.json --threads 4 --validate
tools/build-cooked-game.ps1 -OutputDirectory work/cooker/release -VerifyLaunch
```

The packaging script builds the client/cooker, cooks, checks every chunk, copies
the executable and SDL dependency, separates symbols, writes checksums, launchers
and release/size/validation manifests. `-ArchivePath` creates the final ZIP.
`-SkipBuild`/`-SkipCook` support already verified outputs. Do not use them when
their inputs have changed. `-PreviousReport` accepts a previous size report and
reports package growth over 10%. Reports and symbols stay outside the game.

The game folder contains `Content/*.stpak`, its index, two launchers, licenses,
the executable and SDL3. Developer and normal launchers use separate save paths.
The `cooked-only.marker` makes a missing index fatal instead of enabling source
fallback. The game resolves content relative to its executable, not the cwd.

```powershell
StellarCooker --audit --root . --report work/cooker/baseline-audit.json
StellarCooker --validate-only --output work/cooker/release --report work/cooker/validation.json
StellarCooker --root . --clean --profile release --output work/cooker/release --report work/cooker/clean-report.json
tools/test-cooked-game.ps1 -PackageDirectory work/cooker/release
tools/test-cooked-game.ps1 -PackageDirectory work/cooker/release -Scenario system -SaveSource path/to/campaign.dev17.json
```

The launch check writes only to a new isolated report/save folder, requires a
working native graphics device, checks the screenshot and successful exit, and
asserts the mounted registry reports zero read failures and no source fallback.
Its startup scenario also verifies packaged audio. Never point smoke automation
at a player's active save. A manual CI workflow uses an explicitly labelled
Windows graphics runner; it has not been run remotely by this change.

## Formats, quality and streaming

Color maps use BC7, normal XY maps BC5, eligible single-channel masks BC4.
UI and images failing conservative quality gates use lossless RGBA8. Color mip
filtering is linear-light and premultiplied-alpha aware; normal mips renormalize.
Complete mip chains end at 1x1. Color error is measured over both black and white
backgrounds, including alpha; invisible RGB does not dominate the decision.
Existing multi-channel properties remain packed; no arbitrary recoloring occurs.

Base image resolution is retained. Planet/moon requests select the required
mip tail before disk reads; close system materials remain bounded by the existing
96 MiB CPU cache and at most two 2048-wide system materials. Stars and VFX use
their established preparation queues. Eruption 256/512 aliases select lower
mips from one 1024 product instead of shipping three independent PNGs. Rings
ship their annular renderer's radial input only. High/Ultra skies preserve native
resolution; the selected faint sky is lossless. UI sources remain lossless.

The GPU checks BC support and uploads supplied blocks/mips directly. Unsupported
devices decode to RGBA. Format tags keep the renderer separate from the Windows
cook target. XPRESS compression is used only with useful savings; lossless RGBA
may use a reversible four-channel delta predictor before XPRESS. The manifest
identifies the codec. No JPEG recompression is introduced.

## Determinism, integrity and diagnostics

Source SHA-256, recursive dependencies, platform/profile, cooker version and
implementation/codec fingerprints determine cache keys. Corrupt metadata or
payload caches recook. Threads claim independent products; ordered publication
makes serial/parallel outputs identical. Exact duplicate mip chunks share one
package offset. Variant pairing remains in the authoritative VFX definitions.

Seven logical groups currently ship: Core, Celestial, VFX, Backgrounds, Ships,
UI, Audio. Packages use `STPAK001`; the checksummed CBOR index uses `STMNF001`.
Each chunk has offset, stored/raw lengths, codec and SHA. Mount checks headers,
sizes, mip shapes, aliases and dependencies; reads verify decompressed hashes.
Generation files publish before the index. Packaging archives superseded
generations outside the shipping folder. A future updater can compare hashes;
an updater/mod override system is not implemented here.

Registry records are immutable, reads have independent file handles, and bounded
diagnostic counters/failure history are synchronized. Mount before starting
workers. Developer diagnostics lists assets, formats, dimensions and packages.
Support bundles include manifest/package versions, generation names and recent
load failures. Smoke logs include CPU process peak (test runner), image/scene
texture allocations, render targets and registry read counts. These allocation
counts are not a measurement of every byte reserved by the graphics driver.
In particular the current image/scene cache counters include CPU owner data as
well as GPU allocations; the 2D counter conservatively doubles owner bytes.
They must not be reported as VRAM. Process peak RAM is measured independently.

## Evidence and remaining scope

See [the measured release report](ASSET_COOKER_REPORT.md) for the current package,
size increase, visual verification and unfinished optimization work.

`engine_asset_cooker` covers SHA vectors, codec round trips, alpha, mip selection,
dependency invalidation, cache repair, deterministic publication, deduplication,
state filtering, master/runtime imports, strict lookup and corrupt/truncated/
missing packages. GPU tests compare BC7/BC5 to RGBA, including alpha and mip use.
`stellar_cooked_texture_review` produces fourteen source/cooked 1:1 comparisons
and numeric error results. Local build evidence lives under `work/cooker/`.

The source audit includes build copies, old releases and caches; its total is
not the shipping baseline. Compare actual final ZIP and cooked folder sizes,
not that large development total. Complete mip chains and lossless exceptions
can make a cooked package larger than its source PNGs. Do not claim size savings
without the generated measurements.

The broader requested optimization scope is still unfinished in these areas:

- VFX crop fields are present but currently preserve the full canvas. Cropping
  requires origin-aware UVs through the existing flare attachment/projection
  path; removing borders without that integration would shift surface anchors
  and risk clipping faint wisps. No such unsafe crop is applied.
- Music ships compressed, but the audio decoder still creates bounded PCM for
  the current track. True incremental compressed-track decoding remains to add.
- No imported model-buffer cooker or HDR/BC6H encoder is implemented; current
  native 3D meshes use existing procedural LODs, and no HDR inputs are shipped.
- Cache bounds and per-mip reads are implemented; automatic VRAM-budget querying
  and general per-asset residency inspection are not. Full hot reload and public
  mod tooling remain future work.

These limitations do not prevent the present native game from using its cooked
content, but the complete 100-section optimization request is not declared done.
