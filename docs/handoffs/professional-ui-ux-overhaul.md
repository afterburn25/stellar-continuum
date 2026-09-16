# Professional UI/UX overhaul handoff

Branch: `polish/professional-ui-ux-overhaul`

Starting point: `277ac2bb163e0967ffbe66405be3b108697a6594` (`cpp/devin-swe2-native-conversion`)

## Scope completed

- Added the native Deep-Space Instrumentation design-system layer in `app/native_client/native_ui_theme.hpp`.
- Unified native palettes across system, colony and surface views, fleets, shipyard, construction, research, diplomacy, economy, logistics, notifications, settings, startup, new-game setup, settlement, battle and Developer tools.
- Preserved the established galaxy command routing and responsive hit rectangles while making opened workspaces visually identify their subsystem.
- Added clamped contextual tooltips to research programs and fleet outliner rows, with room for other screens to consume the same primitive.
- Improved selected rows with a shape rail in addition to color.
- Added shared bounded progress visualization to survey, colony support, infrastructure, research, fleet fuel and travel, ship construction, surface construction, logistics and settings.
- Restructured construction and fleet inspectors so titles, state, critical metrics, descriptions, requirements and actions no longer read as one undifferentiated text block.
- Added semantic metric cards and shortfall/health treatment to economy and logistics.
- Added category rails and retained-event count to notifications; existing diplomacy action links remain available.
- Improved diplomacy relationship meters, tabs, filters, actions and political-status wording.
- Improved modal action semantics: destructive actions use danger treatment, confirmations use caution or success, and unavailable actions retain labels with explicit disabled treatment.
- Updated `docs/VISUAL_STYLE_GUIDE.md` with the native implementation contract.

## Stable interface contract

New native UI must use `stellar::native_ui` colors, tones and primitives. Do not introduce another local raw palette unless the value represents physically motivated world art rather than interface state. Preserve these hierarchy rules:

1. screen title and current strategic summary;
2. selectable overview or list;
3. dedicated contextual inspector;
4. graphical state where it improves scan speed;
5. action and recovery controls at a stable panel edge.

Selection, warnings and success must not rely on hue alone. Pair them with an accent rail, keyline, label, status chip or meter. Keep normal interface text readable and preserve the established 720p scrolling behavior and hit targets.

## Performance

The shared primitives emit a fixed, small number of existing draw-list commands. No new texture generation, continuous animation, simulation query, persistent cache or unbounded collection was introduced. Command-rail tooltip rendering is limited to one hovered control and clamped to the viewport.

## Verification

Passed strict MSVC syntax checks with `/std:c++latest /EHsc /W4 /WX /Zs` for every modified C++ translation unit, including `main.cpp`, and for `native_ui_layout_tests.cpp`.

The normal CMake configure/build is environment-blocked: CMake cannot create child directories or temporary files under either `C:\Devin\stellar-continuum\build-native` or `C:\Devin\stellar-polish-build`, even after write scope was granted. Existing build evidence was not deleted or altered. A host with working directory creation must run:

```powershell
python tools/stellar-export/stellar.py build windows-native-preview
ctest --test-dir build-native/preview --output-on-failure
```

Then run the maintained graphical smoke/capture workflow at 1280×720 and 1920×1080 at minimum and inspect galaxy, system, colony, surface, fleets, construction, research, diplomacy, economy, logistics, notifications and all settings panels.

## Remaining opportunities

- Add icon rasterization to the native command rail once the existing SVG runtime loader is available to the SDL client.
- Add keyboard focus traversal beyond the existing shortcuts and Escape behavior.
- Extend notification destinations beyond diplomacy when authoritative entity destinations are added to the notification model.
- Perform the final rendered spacing pass and GPU frame-time comparison after the build-directory permission problem is resolved.
