# Validation

- Actual-source fixture contains 28 native replay rows and pins ten directly used source files before and after generation.
- Fixture SHA-256: `24D852F6E0E6D8224C4DAADBE658AE5B1D9577C72304EEE757240E7B1C0A0221`.
- Two independent runs produced byte-identical fixtures.
- Strict Debug and Release compiled cleanly with `/std:c++latest /W4 /WX /permissive- /fp:precise /Z7`; both replayed all 28 rows.
- Missing arguments, missing source root, and missing fixture probes each exited 1 with exception type/message, current directory, and absolute source/fixture paths or missing-argument placeholders.

## Maintained integration

Registered as `legacy_campaign_recovery_parity`; actual-source generator `tests/Stellar.LegacyCampaignRecovery.ParityGenerator`. Program and fixture bytes are unchanged. Only the project reference and native helper include paths are relocated. Combined maintained validation passed 94/94 CTest and 29/29 Python checks (`work/native-038-testing.log`). The maintained generator reproduced the sealed fixture byte-for-byte.

- `core/include/stellar/core/legacy_campaign_recovery.hpp` SHA-256 `C07C54AA31E0EFA82A15CB001A17B66AF5742E32769FE42628E04D6844B41B28`
- `core/src/legacy_campaign_recovery.cpp` SHA-256 `0506939E57701607315A12674F4B780848733A7A29E0013F08CEE64B1ACE01BF`
- `native-tests/legacy_campaign_recovery_tests.cpp` SHA-256 `F2B30FFCDA7A96500951C915B640309F0220849E334D5BF531B4F5C7658DF99F`
- `native-tests/fixtures/legacy-campaign-recovery.json` SHA-256 `24D852F6E0E6D8224C4DAADBE658AE5B1D9577C72304EEE757240E7B1C0A0221`
- `tests/Stellar.LegacyCampaignRecovery.ParityGenerator/Program.cs` SHA-256 `02EB610A4F19A27768EA64569A6E469F27DCB78BA4559494EC7F9E9C2C230622`
