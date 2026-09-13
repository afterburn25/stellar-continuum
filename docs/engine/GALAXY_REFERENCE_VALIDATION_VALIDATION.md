# Validation

- Fixture: 56 actual-source/native rows, no source-only rows.
- Fixture SHA-256: `1B3BCDFB7476F0262C54A92E7D1F313B80FE190B37A84F0F96BB2C4B4CFDE630`.
- Deterministic repeat generation produced the identical SHA-256.
- Six actual source files are fingerprinted before and after generation and replay.
- Strict Debug and Release compiled cleanly with `/std:c++latest /W4 /WX /permissive- /fp:precise /Z7`; both replayed all 56 rows.
- Missing arguments, missing source root, and missing fixture probes each exited 1 and reported exception type/message, current directory, absolute source root, and absolute fixture path (with placeholders for missing arguments).

## Maintained integration

Registered as `galaxy_reference_validation_parity`; actual-source generator `tests/Stellar.GalaxyReferenceValidation.ParityGenerator`. Program and fixture bytes are unchanged. Only the project reference and native helper include paths are relocated. Combined maintained validation passed 94/94 CTest and 29/29 Python checks (`work/native-038-testing.log`). The maintained generator reproduced the sealed fixture byte-for-byte.

- `core/include/stellar/core/galaxy_reference_validation.hpp` SHA-256 `E0F7795A6D5E93639E618FCA29B340A520BA1A7EDCB29DD1041E8659ED6148AC`
- `core/src/galaxy_reference_validation.cpp` SHA-256 `822196B6B730C963E2D4CDC3A1B69AA488A7BA63AA5253F11B244E4903B42169`
- `native-tests/galaxy_reference_validation_tests.cpp` SHA-256 `C6A268F2A060AC100C98A9BA3771F0E09F4869915ED99B2F33EA5D880CBB579D`
- `native-tests/fixtures/galaxy-reference-validation.json` SHA-256 `1B3BCDFB7476F0262C54A92E7D1F313B80FE190B37A84F0F96BB2C4B4CFDE630`
- `tests/Stellar.GalaxyReferenceValidation.ParityGenerator/Program.cs` SHA-256 `09B434B99C54E5376E886AD7C87970FACEAF212CF5AA48CA886A7E8AB9AF0FA2`
