<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Stellar artwork close-ups — 2026-09-18

The 33 supplied files already matched the installed artwork by SHA-256. A replay
of a copy of the player's 1,000-system campaign showed Sol using `g-yellow` in
both views. Missing physical metadata was not the cause for that saved Sol.
However, spectral-only older campaigns still selected the procedural star, the
map limited close-up size, and system zoom enlarged the 1,024-pixel sprite to
8,120 pixels across. The latter visibly blurred the supplied surface.

The shared observer resolver now selects supplied artwork from physical identity
or a surveyed legacy spectral class. Galaxy companions use it too. Partial and
unknown surveys cannot acquire a classified asset through this resolver; no
physical values or survey knowledge are invented. Protostars retain their
generic marker because no supplied asset exists for that class.

Detailed artwork now blends in over an 8–24 pixel radius rather than 18–46.
The initial asynchronous fade and four-image close cache remain. Map overview
and neighborhood sizes are unchanged through 64x; higher magnification grows to
twice the previous maximum base radius. A selection rim replaces the translucent
disc that previously tinted over the star. System star display stops at a
350-pixel radius (1,015-pixel sprite), while planet/navigation zoom still reaches
the existing scale of 5. Large wheel deltas are bounded before exponentiation.
The source PNGs are unchanged. No save schema or simulation rules change.

The Development menu now explains the native developer launch procedure instead
of claiming developer tools are unavailable. The Developer Game launcher uses
`--dev-game` to activate developer mode immediately; choose New Game > Sandbox.
The existing `--devtools` plus Ctrl+Shift+F12 route remains available. The chord
alone is ignored in a normal launch. Developer saves remain isolated.

Validation:

- Stellar artwork tests cover all 33 assets, distance pairs, early resolved
  detail, transparency, gradual readiness and bounded non-thrashing caches.
- Galaxy marker tests cover unchanged overview size and enlarged deep zoom.
- System workspace tests cover spectral-only Sol, physical identity precedence,
  survey redaction, selection of the supplied provider without a procedural
  overpaint, and maximum planet zoom with bounded star display.
- The actual Vulkan `--galaxy-art-smoke` replay now additionally captures and
  checks fully resolved 1,024-pixel artwork at maximum map zoom, a close system
  view and maximum system zoom. It also preserves the original overview,
  regional, system, pause, save and background-layer checks.
- Before/after captures and runtime evidence are in `work/star-art-current`.
  The 720p source build and 1080p packaged replay on the copied 1,000-system
  campaign passed and reported `g-yellow` in all three close-up checks. A second
  packaged replay with only Sol's legacy spectral class also passed all three.
  The player's original save and running installation were not modified.
- All 16 related CTest checks passed after rebuilding consumers, including
  system travel, colony entry, body inspection, planetary rendering, startup,
  developer index and fixed simulation. The packaged developer-game replay
  also generated a separate `.dev17.json` campaign and exercised its index,
  simulation controls and diagnostics. See `tests.log`, `packaged.log`,
  `legacy.log` and `developer.log` in the evidence directory.
- After the shortcut report, a packaged `--dev-game --new-game-smoke` replay
  created and saved an isolated developer campaign without the developer-smoke
  activation override or any key chord. Its setup capture shows the developer
  research and coverage options, unchecked by default. Four startup/developer
  regressions passed; see `launcher.log` and `launcher-tests.log`.

The runnable update is staged at
`C:/Steller Continuum/StellarContinuum-StarArtwork-Fix-20260918`. Its Developer
Game shortcut supplies `--dev-game`; no activation key chord is required.
The 305-file package manifest was rehashed and verified. This is an updated
0.1.13-alpha build, not a newly numbered release.

Resolution remains limited by the supplied artwork. This change does not add
new surface detail beyond those images, a physical 3D stellar surface, or new
protostar artwork. System and map sizes remain presentation scales.
