# Gate 083A validation

The retained actual-source generator is `../campaign-foundation-oracle-083a/Program.cs`. It sets invariant cultures and invokes the private `CampaignSaveService` methods by reflection, with operation calls isolated from JSON projection. Two consecutive generations produced identical bytes. The 35-row fixture SHA-256 is `7ED6F4E7318A8E66EE72008CFF40AADD3AA86B06C222803AA8E99F1092A039EA`.

The native checker pins the ordered seven-file source inventory and verifies every source hash before and after replay. It compares complete decoded input before invocation, unchanged input after invocation, typed result or exact error category/message, exact integers, and floating fields at absolute tolerance `1e-10`. A dedicated regression proves `2^53+1` cannot compare equal to `2^53`. Native ownership probes mutate each source DTO/state after restore/capture and verify nested strings and leadership records remain detached.

Both isolated Debug and Release passed all 35 actual-source rows and the ownership probes using C++ latest, `/W4 /WX /permissive- /fp:precise /Z7`, explicit `/Fo` and `/Fd`, and optimized Release. The final Release executable also returned exit 1 with readable type, message, working-directory, fixture-path, and source-root diagnostics for missing arguments, a missing fixture, and a missing source root. Logs are under `build/debug` and `build/release`.

Final artifact SHA-256 values are:

- public header: `8525A08824495AE368E3288174308BB935C44F9E332DEC258D20B718471A2F32`
- production source: `E5397555BFFC41A7F1412CC6F5AA57EEE03052D68114CA43824595A043E2A87B`
- native checker: `356ACF93D2FA9D4DF7BCEF907DDFA62164C9FAA8A52BB1FCC64661D957B0CA58`
- founding-roster header/source: `9AE9608755E9345AF6C3A3AB04FEFF34B771EA497B163C506F64CAA195D99507` / `0C9149D5BEEA7D72487EBCB49FDAB9B116953522A541C90297B6C1167B199370`
- proposed shared-catalog source: `87CDE9DC40DAD0055F55CDBFF8C971F593DE7676733D1600AF6F711403D75902`
- managed generator/project: `479EBF31833D90495D823970DD36CC3281E873D262B27ECC32513B5181D509D7` / `8D63E07CADA7256175B43CC7FD3425429D8831FD1289F0B9CA44BD85B8F1E321`

The system operations intentionally compose source `ToSystems` with the immediately following `ValidateStellarCatalog`; capture intentionally validates before `ToSystemDtos`. Raw `ToSystems` mapping alone is not claimed as the error boundary. The gate does not validate whole-galaxy identity/reference rules and does not claim galaxy16 or player-save compatibility.

## Maintained integration

Registered as `campaign_foundation_persistence_parity`. Actual-source generator: `tests/Stellar.CampaignFoundationPersistence.ParityGenerator`. Program and fixture bytes are unchanged; only the project reference is relocated. Shared founding-roster extraction also runs through existing civilization and campaign checks. Combined maintained validation passed 90/90 CTest and 29/29 Python; the maintained generator reproduced exact fixture bytes. Evidence: `work/native-037-testing.log`.

- `core/include/stellar/core/campaign_foundation_persistence.hpp` SHA-256 `8525A08824495AE368E3288174308BB935C44F9E332DEC258D20B718471A2F32`
- `core/src/campaign_foundation_persistence.cpp` SHA-256 `E5397555BFFC41A7F1412CC6F5AA57EEE03052D68114CA43824595A043E2A87B`
- `native-tests/campaign_foundation_persistence_tests.cpp` SHA-256 `356ACF93D2FA9D4DF7BCEF907DDFA62164C9FAA8A52BB1FCC64661D957B0CA58`
- `native-tests/fixtures/campaign-foundation-persistence.json` SHA-256 `7ED6F4E7318A8E66EE72008CFF40AADD3AA86B06C222803AA8E99F1092A039EA`
- `tests/Stellar.CampaignFoundationPersistence.ParityGenerator/Program.cs` SHA-256 `479EBF31833D90495D823970DD36CC3281E873D262B27ECC32513B5181D509D7`
