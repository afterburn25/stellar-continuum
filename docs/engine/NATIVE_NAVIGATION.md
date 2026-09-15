# Native navigation rail

The native C++ client uses four approved icons for Research, Shipyard,
Construction and Relations. The compact rail stays at the left while the
chosen workspace opens. Hover labels name each destination, and the current
workspace has an active accent. Clicking its icon again closes it.

`NativeUiLayout` owns the exact drawn and hit-tested rectangles. The shared
`native_navigation_content_left` gutter reserves room in research, production,
relations, colony, surface and system layouts. Workspace content must remain
outside that gutter; changing the rail requires checking those layouts together.
Pause/play and speed stay in the top strip.

Navigation is routed before workspace input and consumes its pointer gesture.
The pause menu, settings and confirmation dialogs keep input ownership while
open. An icon switches presentation only: it does not issue a research,
production, diplomatic or travel command. Core owns all gameplay state.

The four 256 x 256 transparent PNGs come from the approved SVG designs. They
are decoded once using the existing Engine image loader, share stable image
identity and occupy 1 MiB in total. Build and export require the exact reviewed
paths and source/runtime hashes. See `NATIVE_NAVIGATION_ART_SOURCES.md` for
regeneration and provenance. No SVG renderer or Python package is shipped.

The maintained `--navigation-smoke <capture.bmp>` path uses normal native
pointer input to exercise workspace switching. It opens a real surface
placement confirmation, proves that a hidden rail click cannot dismiss it or
open Research, cancels the preview and checks four normal workspace switches.
The complete canonical Player17 payload remains unchanged. A pause-menu click
is checked separately. Older surface/system shortcuts and duplicate navigation
branches were removed after review found that they bypassed the modal guard.
Workspace/layout CTests cover
the shared gutter and matching hit rectangles, and the image CTest verifies
transparency, distinct artwork, action mapping and stable allocation. This
does not establish full interface parity or a sustained frame-rate target.

## Validation, 2026-09-15

The final MSVC native client and image/layout/workspace targets build. Ten
unique focused CTests pass: seven affected workspaces, UI layout, native client
input and navigation art. Layout checks retain 640 x 360 and 1280 x 1080
regressions alongside 720p, 1080p, 1440p and 4K. Seventy-three Python checks
pass across navigation assets, navigation runtime and client/export validation.
Regenerating the PNGs with the pinned tool reproduces the reviewed hashes.

Final relocated Vulkan navigation runs at 1280 x 720 and 1920 x 1080 each
report four valid switches, real menu/modal blocking, unchanged treasury,
unchanged complete canonical payload and retained pause/day. The fresh save
and paused reload match in full except for `SavedAtUtc`. Both actual captures
were inspected. The maintained exporter requires those separate, correctly
sized captures and checks their complete nonuniform BMP payloads; absent,
duplicated, malformed, false and spoofed proof is rejected.

Final neighboring runtime checks also pass: two research launches, two system
launches and three surface launches. Representative final captures at 720p and
1080p were inspected. Research funding/progression and paused reload remain
valid; surface placement, cancellation/refund, construction progression and
reload remain valid. The clipped galaxy selection label exposed through the
rail gutter is now hidden when a workspace is open.

Local evidence (not a sealed download):

- `work/native-navigation-build.log`, `work/native-navigation-tests.log`
- `work/native-navigation-modal-tests.log`, `work/native-navigation-input-tests.log`
- `work/native-navigation-python-tests.log`
- `work/native-navigation-runtime.json`, `work/native-navigation-neighbor-runtime.json`
- `work/native-audio-validation/package-navigation-fresh.bmp`
- `work/native-audio-validation/package-navigation-loaded.bmp`

Run `validate_native_navigation_export(package, stellar.build_environment())`
from the export tooling to repeat the two relocated checks. The normal native
export invokes it automatically. Broader UI polish, full 3D and broad-hardware
performance remain separate milestones.
