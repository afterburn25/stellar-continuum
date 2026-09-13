# Gate 083B validation

The retained actual-source generator is `../campaign-planetary-oracle-083b/Program.cs`. It sets invariant cultures and invokes the private `CampaignSaveService` planetary methods through reflection. Typed method return values are projected only after the invocation catch. Two consecutive generations produced identical bytes. The 32-row fixture SHA-256 is `4C8170428B5E2E05AEDF0EF9C1C6D148E7AA79448531CA4E6DDB2932D43BB1AE`.

The fixture records complete DTO/body/system input before and after every call, result or exact outer error, and the inner exception type/message for wrapped physical validation failures. It pins three source files in order with their byte fingerprints. Cases cover collection presence, null elements, detached valid ordering, duplicate IDs, body/environment enums, every physical validation family, system membership, cycles, parent shape and identity, capture ordering, capture validation, and empty capture.

The native checker verifies the fixture and source fingerprints before and after replay, exact integer comparison including a `2^53+1` regression, floating fields at absolute tolerance `1e-10`, full native input before and after each operation, complete results/errors, and owned capture/restore strings. A native-only truncated UTF-8 probe records the valid-Unicode source boundary while proving the shared blank-name helper does not read beyond its input.

Final isolated Debug and Release builds both passed all 32 rows and the ownership/boundary probes under C++ latest, `/W4 /WX /permissive- /fp:precise /Z7`, explicit `/Fo` and `/Fd`, with optimized Release. The Release executable returned exit 1 with useful type, message, working-directory, fixture-path, and source-root diagnostics for missing arguments, fixture, and source root. The managed generator likewise returned exit 1 without creating output for a missing source root. Logs are under `build/debug`, `build/release`, and `managed-missing-source.log`.

Final SHA-256 values are:

- public header: `9260AC797767F96E22ED0A68525656E37697157DC540BCD02446C817FEA338BD`
- production source: `A1E599E5435E1F19102F3AA35324B50773C7A5EDD3181280F49838121E7672AB`
- native checker: `42ACC7B4F7F977C14C06A91F93B2C252A3456BFC6B2B7DACBA0504F5363F53ED`
- proposed shared planetary catalog source: `F3BE7D1FEA7A6E63ECCBB7B6B953822EA0053DCF9BC7E0BA445388A5EDBEA0E0`
- managed generator/project: `9FDEA4E9E9874C45ABD3F19F5C048B9671A65381C05BAEF483B5F0AAEB73ADE1` / `9497942863CA2DB9117C1DE233872906F49D7D5AD054AECBC1079351F34E3305`

## Maintained integration

Registered as `planetary_body_persistence_parity`; actual-source generator `tests/Stellar.PlanetaryBodyPersistence.ParityGenerator`. Program and fixture bytes are unchanged. Only the project reference and native helper include paths are relocated. Combined maintained validation passed 94/94 CTest and 29/29 Python checks (`work/native-038-testing.log`). The maintained generator reproduced the sealed fixture byte-for-byte.

- `core/include/stellar/core/planetary_body_persistence.hpp` SHA-256 `9260AC797767F96E22ED0A68525656E37697157DC540BCD02446C817FEA338BD`
- `core/src/planetary_body_persistence.cpp` SHA-256 `A1E599E5435E1F19102F3AA35324B50773C7A5EDD3181280F49838121E7672AB`
- `native-tests/planetary_body_persistence_tests.cpp` SHA-256 `42ACC7B4F7F977C14C06A91F93B2C252A3456BFC6B2B7DACBA0504F5363F53ED`
- `native-tests/fixtures/planetary-body-persistence.json` SHA-256 `4C8170428B5E2E05AEDF0EF9C1C6D148E7AA79448531CA4E6DDB2932D43BB1AE`
- `tests/Stellar.PlanetaryBodyPersistence.ParityGenerator/Program.cs` SHA-256 `9FDEA4E9E9874C45ABD3F19F5C048B9671A65381C05BAEF483B5F0AAEB73ADE1`

Maintained source has only its extra final blank line removed; the fixture and generator remain byte-identical.
