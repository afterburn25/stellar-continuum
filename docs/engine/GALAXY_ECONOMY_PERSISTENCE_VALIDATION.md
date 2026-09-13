# Validation

- Actual source fixture: 57 rows, plus one source-only capture-alias metadata probe.
- Native ownership: 3 detached capture probes.
- Fixture SHA-256: `0FE35AC4D15C630893C4E3261707311644FAF0577F51B7CA9F7EC3C18709E589`.
- Source SHA-256: `5D3B16323EDEAD7E46A73C903161ACAF2DCF1CE104046D08A739FE8FBC26599E` before and after generation and replay.
- Fixture regeneration is deterministic: repeat SHA matched exactly.
- Strict Debug and Release: `/std:c++latest /W4 /WX /permissive- /fp:precise /Z7`, each passed 57/57 actual-source rows and 3 ownership probes.
- Native errors use distinct data and null-reference types; the replay compares the actual native category and message to the source and does not catch unrelated native exceptions as expected production failures.
- The borrowed civilization Id/species projection is checked before and after every call.
- Final Release failure diagnostics verified exit 1 for missing arguments, missing source root, and missing fixture; each reports exception type/message, working directory, absolute source root, and absolute fixture path or `<missing>`.

## Maintained integration

Registered as `galaxy_economy_persistence_parity`. The retained actual-source generator is `tests/Stellar.GalaxyEconomyPersistence.ParityGenerator`. Program and fixture bytes are unchanged; only the project reference is relocated. Combined maintained validation passed 90/90 CTest and 29/29 Python; the maintained generator reproduced exact fixture bytes. Evidence: `work/native-037-testing.log`.

- `core/include/stellar/core/galaxy_economy_persistence.hpp` SHA-256 `F84A1C16DC98066C3BC5A4BEB20B58C528AFEAC200F50713ABBCF984DF722AB9`
- `core/src/galaxy_economy_persistence.cpp` SHA-256 `7BBDCA4F9D115EBE79C37F9697506746BC51020A5C3AE57E9A42EC24DDBF79F8`
- `native-tests/galaxy_economy_persistence_tests.cpp` SHA-256 `B040B8929144F78BFCC3441FEFEBA56D8DF8E78C0C3F26F83777019B6492F2C5`
- `native-tests/fixtures/galaxy-economy-persistence.json` SHA-256 `0FE35AC4D15C630893C4E3261707311644FAF0577F51B7CA9F7EC3C18709E589`
- `tests/Stellar.GalaxyEconomyPersistence.ParityGenerator/Program.cs` SHA-256 `8FE592F1BCD9EB91865106B589BF775FB89D1A8847DEB6F93071F61BDB710A20`
