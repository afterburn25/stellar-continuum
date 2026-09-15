# Stellar Continuum native Windows icon

Stellar Engine embeds the game's silver/cyan galaxy emblem with its golden core
into both native Windows executables: `stellar-continuum.exe` and
`stellar-continuum-native.exe`.

The C++ window platform selects the embedded large and small icon resources before
SDL video initialization. The game therefore supplies its own running-window and
executable icons to Windows, including the sources used by the taskbar and Task
Manager. Hosts without the game icon resource keep their existing default.

## Assets

- `assets/visual/branding/stellar-continuum-v1.ico`: 16, 20, 24, 32, 40, 48,
  64, 96, 128 and 256 pixel images in one Windows ICO.
- `assets/visual/branding/stellar-continuum-icon-v1.png`: 256-pixel RGBA image,
  also available in the Stellar Engine asset library.
- `assets/visual/branding/icon-provenance-v1.json`: exact image-generation prompt,
  source logo/master hashes and delivery hashes.

The original title logo was the reference for a new compact, text-free emblem
made with the built-in OpenAI image tool. Pillow performs only delivery resizing
and ICO encoding; the generated transparency is preserved.

## Native integration

Resource ID `STELLAR_APPLICATION_ICON_ID` is shared through
`engine/include/stellar/engine/windows_resource_ids.h`.
Both Windows version-resource templates include the ICO from the source tree.
Windows resource compilation embeds every size at link time, so the application
does not depend on an external icon file or another engine to display its icon.

`engine/src/native_map_platform.cpp` sets
`SDL_HINT_WINDOWS_INTRESOURCE_ICON` and
`SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL` before initializing SDL, as required
by the pinned SDL3 headers. It first checks that the hosting executable contains
the resource, preserving icon behavior in platform test programs and other hosts.

Use the native CMake build with `STELLAR_BUILD_NATIVE_CLIENT=ON`.
The normal Stellar Engine export pipeline packages the resulting binaries with
their embedded resources. The general ICO and PNG remain reusable by the engine
editor and future native game launchers.

## Validation

- Native Windows CMake/MSVC build completed for both executables.
- Native platform, text-measurement and headless smoke checks passed (3/3).
- Windows extracted 16, 32, 48, 64, 128 and 256 pixel executable icons;
  each matched the corresponding ICO image exactly.
- The launched native game supplied the correct 32-pixel running-window icon;
  the headless executable also supplied the correct associated icon.
- The relocated native package launched with Vulkan under a restricted PATH,
  created a 500-system campaign, saved it and reloaded it successfully.

These checks verify the executable and window icon sources Windows uses.
Task Manager itself was not visually inspected. This is a branding change,
so verification does not establish full gameplay parity with the original game.

## Scope

This is native Windows application branding. It does not change simulation,
campaign saves, research balance or gameplay conversion status. No Godot project
or export settings are part of this change.
