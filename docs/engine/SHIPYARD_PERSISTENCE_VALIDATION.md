# Gate 081 validation

The retained actual-source generator is `../shipyard-persistence-oracle-081/Program.cs`. It sets invariant cultures, invokes the private `CampaignSaveService` shipyard helpers through reflection, unwraps `TargetInvocationException`, and emits complete before, after, result, and typed-error evidence. Run it with:

```powershell
dotnet run --project ..\shipyard-persistence-oracle-081\ShipyardPersistenceOracle.csproj -- work\081-shipyard-persistence\fixture.json src\Game
```

Two consecutive generations produced identical bytes. The 48-row fixture SHA-256 is `A0080BB768A2876AA454C4425C50112191369F2105C545C295377D1BB0814063`. Exactly 47 rows invoke the native typed API; one retained null-reference collection element is explicitly source-only because a native `vector<QueuedShipBuildPersistenceDto>` cannot contain a null value. Native rows include explicit empty restore and capture collections.

The final 48-row checker passed isolated Debug and Release builds with C++ latest, `/W4 /WX /permissive- /fp:precise /Z7`, explicit `/Fo` and `/Fd`, and optimized Release. Both configurations reported `47/47 native rows, 1 explicit source-only boundary passed`. It verifies the fixed fixture digest, ordered four-file source inventory and source hashes before and after replay, exact row accounting, decoded before-state before invocation, full typed input immutability, detached capture output, and exact integer comparison beyond 2^53.

The checker accepts `<fixture> <source-root>`. Missing arguments, fixture, or source root return a nonzero status and report exception type, message, working directory, fixture path, and source root. The isolated build helper records compile, replay, and negative-path output under `build/<configuration>/`.

## Maintained integration

Registered as `shipyard_persistence_parity`. The retained actual-source generator is `tests/Stellar.ShipyardPersistence.ParityGenerator`. Program and fixture bytes are unchanged; only the project reference is relocated. Combined maintained validation passed 90/90 CTest and 29/29 Python; the maintained generator reproduced exact fixture bytes. Evidence: `work/native-037-testing.log`.

- `core/include/stellar/core/shipyard_persistence.hpp` SHA-256 `6949A0968D465F4144458759D974188156DA6A69B77496B8BA40C06B5059C265`
- `core/src/shipyard_persistence.cpp` SHA-256 `9CFCEEA75E0715A207FF74CAE6AA5DB188777405D4A8A17A1FD9A08F369C5625`
- `native-tests/shipyard_persistence_tests.cpp` SHA-256 `CDC2063139514F4635BC00C17A1FCE35A303FD222ECB5C276C591204C81FCE50`
- `native-tests/fixtures/shipyard-persistence.json` SHA-256 `A0080BB768A2876AA454C4425C50112191369F2105C545C295377D1BB0814063`
- `tests/Stellar.ShipyardPersistence.ParityGenerator/Program.cs` SHA-256 `5C4D63950C266F6A62B9CB4029D507F45C354B1AD1354FB7B7695436F9A1AE72`
