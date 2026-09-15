# Gate 146: native startup artwork

This candidate restores the four already approved startup images through the
native Engine image path. It creates no artwork and changes no simulation rule.
The provenance and committed hashes remain in `docs/ASSET_MANIFEST.md:49-52`
and `docs/art/LOADING_SPLASH_PROVENANCE.md`.

## Runtime mapping

- Application asset staging uses
  `assets/visual/loading/stellar-loading-splash.png`.
- Entry and species setup keep the existing main-menu backdrop,
  `stellar-continuum-splash.png`, so choosing a species does not semantically
  mislabel the application-loading image.
- Detached new-galaxy work and owner activation use
  `stellar-galaxy-generation.png`.
- Selected-save restoration and owner activation use
  `stellar-save-loading.png`.

Entry records `NewCampaign` or `SavedCampaign` explicitly when a request
starts. Later worker and owner-activation phases update that same request
without guessing from a transient phase. A save load that first appears ready
for owner activation therefore still receives saved-game artwork.

`NativeStartupArtworkAssets` lazily decodes and strongly owns at most these four
fixed images. It accepts no caller-controlled relative asset name. Failed
decodes report the UTF-8 asset path. `startup_artwork_destination` aspect-covers
the drawable viewport and every image command is clipped to it.

The normal entry path draws the application startup image while staging the
four immutable images and keeps it visible for a minimum seven seconds. The
window continues polling and rendering, and Quit remains effective. Menu input
appears only after all four images are ready. Automated smoke uses a zero
minimum dwell while still drawing the boot frame and staging every image; this
keeps validation bounded without pretending generation took longer. Generation
and save restoration are never delayed after their actual work completes.
The boot bar is the minimum of real decoded-asset readiness and elapsed minimum
display time, and reaches 100% only on the final rendered frame before Entry.

The five preserved `MainMenuLayer` gameplay tips remain below loading progress.
A presentation-only clock/counter choice selects one once per operation without
using campaign state or a fallible entropy device; later phases retain it.

Workspace panels become bounded translucent veils when artwork is supplied.
Existing layout, measured text, progress, status, failure, Cancel, and setup
input routing remain in front. The contextual `native_new_game_workspace`
change only adds an optional owned background image parameter; existing callers
without artwork retain the prior solid background.

## Evidence plan

The focused test decodes every real committed image, proves one decode per cache
entry and the combined Engine CPU-plus-estimated-GPU byte budget, validates
aspect-cover at 720p through 4K, and checks a useful missing-file diagnostic.
It then verifies exact Entry/setup/new-generation/save-restoration mapping,
ordered image-before-controls layering, a visible bounded veil, retained live
progress/status/Cancel, and responsive control bounds. The entry translation
unit is compiled to validate the responsive boot loop and contextual API.

`build_focused.py` uses strict `/W4 /WX /permissive-` `/MT /Od` and `/MT /O2
/DNDEBUG` adapter variants against the current preview Engine/platform
libraries and runs each test binary twice. Full graphical Vulkan capture remains
required before promotion.

Final focused evidence is in `build/focused-1789430751374446400`: 3/3 cases
passed twice in both strict variants. Final Vulkan evidence is in
`build/graphical-1789430788589831400`: Boot, Menu, species setup, galaxy
generation, and save restoration were captured at 1280x720 and 1920x1080.
Both runs decoded and uploaded exactly four images, and the preview reached
species setup through the real menu input handler. These captures validate the
candidate workspaces; the combined maintained executable remains the promotion
proof boundary.


The integrated generation screen keeps a moving activity segment when the authoritative worker reports no measurable percentage; it never invents a completed fraction. Save restoration uses the measured fraction whenever available. Gameplay tips sit below this live indicator.

Combined native input validation rendered the real Pelagic race setup and approved new-galaxy loading artwork, then created an independent 250-system campaign and reloaded it unchanged. Startup-session tests now allocate per-run scratch directories, so repeated validation retains all prior fixtures.
