# Asset pipeline, storage and distribution status

Audit 2026-09-20. Engine/code implementation exists; this is not a planned-only
cooker. Detailed contracts: [ASSET_COOKER.md](ASSET_COOKER.md), historical measured
release evidence: [ASSET_COOKER_REPORT.md](ASSET_COOKER_REPORT.md).

| Stage | Status | Actual owner / remaining boundary |
| --- | --- | --- |
| Reviewed imports and rejection records | IMPLEMENTED | `export/native-*-assets.json`, Core catalogs and image audits pin accepted runtime inputs; original user master folders remain offline |
| Discovery / state override | IMPLEMENTED | `engine/src/asset_cooker.cpp`; explicit accepted/source-only/rejected/QA/editor/debug/deprecated states, no recursive ship-everything rule |
| CPU image decode and preparation | IMPLEMENTED | `native_image.cpp`, spherical/ring material preparation; retained-byte estimator and bounded image queue |
| Texture mips and GPU compression | IMPLEMENTED BUT NEEDS POLISH | `texture_cook.cpp`; BC7 color, BC5 normals, tested BC4 support, RGBA8 fallback; alpha-aware color/normalized-normal mips |
| HDR / BC6H | NOT STARTED | No admitted HDR input or implemented BC6H path |
| Dependency fingerprints / incremental cache | IMPLEMENTED | Source hashes, recursive dependencies, cooker implementation key, atomic cache publication |
| Deduplication | IMPLEMENTED | Exact cooked chunk hashes share data while keeping asset/game identity separate |
| Package/index validation | IMPLEMENTED | `.stpak` chunks, `.stmanifest` aliases/checksums/bounds, source-fallback prohibition via cooked marker |
| Runtime streaming | PARTIALLY IMPLEMENTED | Selective mip reads, asynchronous decode and bounded caches; no virtual textures, general residency scheduler or automatic VRAM budgeting |
| Mesh cooker | NOT STARTED | Existing runtime procedural geometry LOD is not an imported mesh/vertex-buffer cooker |
| Audio compression/streaming | PARTIALLY IMPLEMENTED | Compressed disk clips and bounded SDL queues; current-track PCM decode, no true incremental decoder |
| Full VFX canvas crop | PLANNED | Anchor-aware UV integration needed before safe cropping; full canvases preserve faint wisps and attachments |
| Release packages and updates | IMPLEMENTED BUT NEEDS POLISH | Separate full internal target and exact-base changed-files update; see [WINDOWS_INSTALLER.md](WINDOWS_INSTALLER.md) |

## Measured inventory

The audit found 7,187 existing files under `assets/` represented by tracked plus
untracked inventories: **3,759,528,000 bytes**. The machine-readable inventory
is supplied in the [audit data](validation/2026-09-20-development-sync.json).
This deliberately distinguishes loose development files from cooked release data.

The previously validated full target `0.1.14.2-dev` contains 20 managed files
totaling **4,287,821,565 bytes**. Its seven content packages contain **3,737 runtime
asset records and 40,327 chunk references**. That is existing release evidence,
not a fresh recook performed during this documentation task.

The existing release cook report records 1,824 BC7, 724 BC5, 1,091 RGBA8 and 98
byte assets. 991 texture results needed lossless quality fallback. Exact chunk
deduplication saved 18,609,348 bytes (15,610 duplicate chunks). Complete mip chains
and lossless exceptions made the prior full portable ZIP larger than its earlier
uncooked delivery; **size optimization is unfinished**.

The existing `0.1.14.2-dev` update ZIP is **8,889,954 bytes**, with three changed
runtime files and 17 retained files. Changed files are transferred whole: this
is a changed-files installer, not binary patch compression. Future user downloads
must follow that update-only preference, using the exact required baseline.

## GitHub checkout contract

This synchronization adds reviewed native art through **Git LFS**, alongside
source, tests, importer/cooker code, data and manifests. `git lfs pull` is required
to obtain actual PNG bytes. Historical shared commits were not rewritten.
Original supplied master libraries, review PNGs and unused untracked originals
remain local. No `.stpak`, ZIP, executable, debug symbol, screenshot capture,
save or credential is added by this synchronization.

The loose exporter currently validates six maps per accepted planet, including
cloud/thumbnail files that the cooker explicitly excludes from shipping.
Those build/export contract inputs remain versioned until that contract is
changed and tested. They do not enable extra runtime cloud layers.

Do not remove old manifest identities/rejection audit records as if they were
unused image pixels. Save migration and curation need them. Do not infer that
all 66 subclasses or all source folders contain accepted or generation-enabled
art merely from directory presence.
