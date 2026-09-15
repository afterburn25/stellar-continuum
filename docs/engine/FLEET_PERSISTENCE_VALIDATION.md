# Gate 080 validation

The actual-source generator reflectively invokes the private current
`CampaignSaveService.ToFleets` and `ToFleetDtos` methods. Its 24 rows freeze
complete typed input before/after, complete output when returned, and exact
errors. Cases cover full nested tactical state, null/empty routes, both old
in-flight target choices, transit validation and phase guards, format7/8
species and destination-body behavior, old colony population reconstruction,
range/fuel fallbacks, negative and NaN population behavior, combat creation,
and capture failures after first/current fleet mutation.

Fixture SHA-256:

`B770FFC79F4214588854CBF857219BC4D7E43FCB9A9FCAB55F3F7B7E4E997F63`

Generate from the repository root:

```text
dotnet run --project ../fleet-persistence-oracle-080/Stellar.FleetPersistence.ParityGenerator.csproj -c Release -- . work/080-fleet-persistence/actual-source-fixture.json
```

Strict native replay:

```text
C:/Python314/python.exe work/080-fleet-persistence/build_strict.py Debug
C:/Python314/python.exe work/080-fleet-persistence/build_strict.py Release
```

Both configurations pass all 24 actual-source rows, with zero source-only
rows, plus one native detached-ownership probe. Missing arguments, source root,
and fixture probes return 1 with bounded path diagnostics.

## Maintained integration

Registered as `fleet_persistence_parity`. The maintained actual-source generator is `tests/Stellar.FleetPersistence.ParityGenerator`; Program.cs and fixture bytes are retained. Only the generator project reference and native test helper include path are relocated. Root reviewed production against source and tightened parity ownership, decoding, source fingerprints and failure boundaries. Combined maintained 0.1.36 validation passed 87/87 CTest and 29/29 Python (`work/native-036-testing.log`). The retained maintained .NET generator reproduced this fixture byte-for-byte. Exact committed export follows.

- `core/include/stellar/core/fleet_persistence.hpp` SHA-256 `F96F421EA19B08F55489F810BDB7F93B1B51356EB5EE9C5C7321EF05C2381281`
- `core/src/fleet_persistence.cpp` SHA-256 `5D4040EA29671801DE3DFD1A2CBB3CF11A6E9BC649443D39B08B85D2507AFD3C`
- `native-tests/fleet_persistence_tests.cpp` SHA-256 `EE9AB5204E4785B1E6D03FF2FB093BC7BA7FB0C30EFB879C0C7244A6C51739DD`
- `native-tests/fixtures/fleet-persistence.json` SHA-256 `B770FFC79F4214588854CBF857219BC4D7E43FCB9A9FCAB55F3F7B7E4E997F63`
- `tests/Stellar.FleetPersistence.ParityGenerator/Program.cs` SHA-256 `50AAC1E6B8B46BB8457D669132486E2570AA62B8CCBB9743C8F7B2C9688C1E28`
