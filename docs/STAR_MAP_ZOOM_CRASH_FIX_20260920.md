# Installed star-map zoom crash — 2026-09-20

Target release: game **0.1.14.2-dev**, engine **0.1.64**.
Baseline: installed **0.1.14.1-dev**, build
`0.1.14.1-dev-6e758b928d87af02`.

## Reproduction and cause

The user repeated the failing zoom in the installed executable, with redirected
output and a copy of the developer save. It closed while still on the star map.
`work/zoom-crash-20260920/interactive-error.log` ends with:

> Image preparation factory exceeded its reserved output budget.

The cooked package registry reported zero read failures. Flare image preparation
reserved only base-level RGBA pixels. The cooked decoder retains selected mip
levels and, for compressed textures, an RGBA base for CPU consumers. Its retained
output therefore exceeded the reservation when zoom enabled detailed flares.
The loose-source pipeline did not reproduce this packaged-only mismatch.

`stellar_cooked_eruption_tests` reproduced the same exception before the fix
(`regression-before.log`). The fix uses the reusable metadata-only Engine output
estimator and preserves the original decoded images, resolution and full mip
tail. The queue's bounds remain enforced. No artwork recook or shader change.

## Diagnostics

`RuntimeDiagnostics` begins before native application initialization. Existing
console output is mirrored to `%LOCALAPPDATA%\Stellar Continuum\Logs`, independently
of install/save locations. Session headers identify game/engine version and
executable; renderer, presentation mode and drawable size are logged after window
creation. The client supplies last view, selected system, map magnification,
viewport and simulation day. Context writes are limited to once per second;
the latest context is still retained in memory between writes.

Caught exceptions leave dedicated text reports. Windows faults record exception
code, address and thread, and attempt a small minidump using the system DbgHelp
library. C++ termination and worker aborts also retain reports/dumps. Windows
termination reporting uses a preopened report and bounded text without depending
on the normal stream lock. The normal log has a 4 MiB cap, with eight retained
files per extension. All reports remain local. Matching PDBs are archived outside
the shipping payload. Forced process kills/power loss cannot guarantee a dump.

## Update distribution

The full target is assembled internally and validated against its file table.
`build-update-installer.ps1` creates the user-facing update with only files whose
hashes or lengths changed. It pins the complete target inventory plus exact base
identity and included-payload paths into Setup. Omitted installed bytes are
verified before any transaction. Wrong bases and damaged retained content fail
before mutation. Existing rollback, repair, user-data protection and process
interlocks are reused. No automatic online update or binary patching is claimed.

## Verification evidence

- `regression-final.log`: 140 asynchronous cooked class/type/quality zoom cases
  pass, retaining original mip chains. Exact output estimation is also compared
  with real decoded storage for sampled shipped formats, both decode usages and
  three resolution limits; estimating performs no package reads.
- `log-tests-v3.log`: stream mirroring, caught-error/context reports, clean exit,
  retention, Windows fault and worker termination all pass. Both fatal child
  processes generate valid MDMP files.
- Native image-preparation regressions pass.
- `update-tests-v2.log`: partial-manifest validation, exact base, damaged omitted
  file rejection, rollback, repair and existing maintenance tests pass.
- `eruption-final.log`: real Vulkan replay at 1280x720, cooked assets only,
  deep star-map flare, system entry, live return to map, controls, A-class rising
  stage and save payload continuity pass. All 299 package reads succeeded.
  Map/system screenshots were inspected; the authored star surface and flare
  details remain visible. This is a scoped rendering check, not every star seed.

The broader developer-index replay stopped at its separate authored-planet map
assertion (`developer-after-error.log`). It is not counted as passing. The
focused `--eruption-smoke` runs the same live flare replay independently, using
the canonical developer exploration command to satisfy observer visibility.
No planet assertion was weakened and no planet renderer was changed in this fix.

No save schema, simulation behavior, original art or existing save data changes.

## Delivered update and installed verification

- Target build: `0.1.14.2-dev-6c8d95a655cd574b`.
- `StellarContinuum-Update-0.1.14.2-dev.zip`: **8,889,954 bytes**. Only the game
  executable, uninstaller and README are replaced (20,856,924 payload bytes).
  Seventeen unchanged files are retained, including all seven content packages.
- Extracted download passes `--check-package`. The real full-baseline/partial
  update integration test passes full hash verification, no-op repair, corrupted
  executable repair, unchanged-file timestamps and save/mod preservation:
  `work/zoom-crash-20260920/shipping-update-test.log`.
- The extracted Setup window was inspected with the actual installed baseline.
  `--apply-update` exited zero and committed the registered installation while
  preserving its install identity and shortcut options.
- All 31 original NativePreview files (about 1.38 GB) retain their SHA-256 hashes.
  No installed content package or asset-index file was rewritten. The installed
  executable hash matches the tested target release.
- `installed-eruption.log`: the actual installed executable exits zero after
  deep star-map flare rendering, system entry, live return, A-class rising and
  save-payload checks. All 299 reads succeed without source fallback. Its
  automatic local log identifies the installed executable and clean exit.
- `installed-campaign.log`: the installed executable also exits zero after
  loading a copy of the user's existing 1,000-system developer campaign and
  replaying overview/regional/deep-star zoom and system entry. Paused time is
  preserved; all 653 cooked reads succeed. Original saves/settings are rehashed
  after both installed replays and remain identical.

Download SHA-256:
`dc7983b5292eb4b878af3ea64dfd45b627f0cf7cb472610660c0f73f0596a648`.
