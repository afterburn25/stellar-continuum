# Gate 076 Adaptive Research export validation

The draft adds one sorted declaration of the 70 canonical JSON files under `data/research/v1`. CMake and the Windows exporter both reject wrong schema/value kinds, empty arrays, unsafe or case-duplicate paths, control/list/generator-expression characters, linked files or ancestors, and declaration drift. Only declared JSON is staged and installed under executable-relative `Data/research/v1`; every copied byte is listed in `requiredRuntimeFiles` and sealed by the existing complete build manifest.

The proposal preserves the astronomy catalog, benchmark modes, runtime configuration, version fields, and source-dirty calculation. Runtime documentation identifies the adaptive mode as diagnostic output rather than a player save or full gameplay/rendering claim.

## Completed checks

- `python -m unittest discover -s work/076-adaptive-research-export/tests -v`: 8/8 focused draft tests passed.
- Proposed maintained `ResearchRuntimePackaging`: 5/5 passed, including actual byte copy/sealing, canonical drift, exact JSON kinds, path/list injection cases, and an actual linked-file rejection.
- CMake script-mode declaration replay: accepted exactly 70 files.
- Gate 077 Debug and Release host replays initialized the real runtime from packaged bytes, produced byte-identical repeat diagnostics, and failed cleanly for missing and corrupt research data.

The declaration is 597,689 source bytes and has SHA-256 `4046C72EBAACDFB6DF0E411C7C78C43207980C5AEBDF54E5DFC94E9CAC53F76D`.

Final relocated restricted-`PATH` execution and complete manifest validation run after the reviewed Gate 075/076/077 patches are composed in the maintained exporter.

Maintained 0.1.35 validation passed 84/84 CTest and 28/28 Python. The three retained NativeRecovery tests cover deterministic full Adaptive campaign output, real research state, asset-root resolution, missing/corrupt research, output/pending preservation and argument boundaries. The maintained `dotnet run` source generator reproduced the 15-row fixture exactly (`work/075-maintained-generator.log`). Clean committed export is the subsequent gate.
