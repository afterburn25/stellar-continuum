# Native dependency provenance

- `nlohmann/json.hpp`: JSON for Modern C++ 3.12.0, MIT. Pinned upstream release https://github.com/nlohmann/json/releases/tag/v3.12.0 ; single-header SHA-256 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`, checked against the published release hash. Header vendored unchanged to allow offline builds; `nlohmann/LICENSE.MIT` accompanies redistribution.
- Core's `legacy_random.cpp` adapts explicitly seeded .NET 8 compatibility behavior from https://github.com/dotnet/runtime/blob/v8.0.0/src/libraries/System.Private.CoreLib/src/System/Random.Net5CompatImpl.cs . `dotnet/LICENSE.TXT` accompanies this MIT-licensed adaptation. It adds no .NET runtime dependency; C# fixtures verify the actual installed .NET 8 results.
- The existing derived HYG JSON remains under its original CC BY-SA 4.0 terms and attribution; see `data/astronomy/README.md` and the JSON metadata. Exports copy both data and attribution. No measured positions or names are modified during migration.

- `SDL3/runtime-lock.json`: SDL 3.4.16, zlib license, pinned official Windows VC archive and individual runtime/import-library/license hashes. `cmake/PinnedSDL3.cmake` downloads this dependency only for the opt-in native client. Exports include only the reviewed x64 SDL3 DLL and license, with direct and transitive imports checked. No SDL or Vulkan SDK is packaged. See [the upstream release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16) and `SDL3/LICENSE.txt`.

Future dependencies require pinned version, provenance, license and runtime export integration. Do not copy development SDKs into player packages.
