# Gate 079 massive encounter persistence validation

This bounded gate ports the owned tactical persistence graph, source validation, and detached deep-clone behavior for `MassiveCombatBattleState` and `CampaignMassiveEncounter`. It does not port tactical stepping, attach the encounter to `FreshCampaignState`, implement a galaxy/player save codec, or claim player-save compatibility.

## Source authority

The retained generator is `../massive-encounter-oracle-079/Program.cs`. It executes the current C# validators directly. Clone evidence contains both the exact private `CampaignSaveService.CloneEncounter` method used by detached capture and a public `CampaignSaveService.Save` row against a real fresh campaign and combat bridge. The fixture records all five source-file SHA-256 values; the native replay verifies them before and after every run.

The 40 rows comprise 39 native-representable operations and one explicit source-only null-reference collection-element boundary. They cover exact Guid byte ordering (`Guid.ToByteArray`), legacy initial-count materialization and mutation on later failure, formation/cohort/vessel/loadout validation order, event/salvo identities and counters, encounter identity, duplicate fleet-map category, binding design/profile fallback, inventory conservation, canonical engagement evidence, and nested clone independence. Collection maxima are source constants and production checks, but the fixture deliberately avoids duplicating hundreds of thousands of complete records solely to cross the 200,000 binding limit.

Native textual fields are valid UTF-8. The blank-name implementation matches .NET Unicode whitespace for valid scalar input; ill-formed UTF-8 has no equivalent in the managed `string` API and remains a parser boundary.

## Reproduction

Generate the fixture from the actual source:

```powershell
dotnet run --project ../massive-encounter-oracle-079/MassiveEncounterOracle.csproj -- work/079-massive-encounter-persistence/fixture.json src/Game
```

Build and replay strict native configurations:

```powershell
python work/079-massive-encounter-persistence/build_strict.py Debug
python work/079-massive-encounter-persistence/build_strict.py Release
```

Both configurations use C++ latest, `/W4 /WX /permissive- /fp:precise`, explicit `/Fo` and `/Fd` outputs under the ignored draft. Each reports `39/39 native rows, 1 explicit source-only boundary`. Missing arguments, fixture, and source root return nonzero with exception type, message, working directory, and relevant absolute paths.

## Current evidence

- Fixture: 40 rows, SHA-256 `64140AC12DD0642C64102AE6C7BC0484B878B69B05603C1EB2BFB1095C36DEB7`, byte-repeat identical.
- Debug logs: `build/debug/compile.log`, `build/debug/run.log`, `build/debug/negative.log`.
- Release logs: `build/release/compile.log`, `build/release/run.log`, `build/release/negative.log`.
- Floating comparisons are limited to enumerated persisted `float`/`double` fields. Integral identities, ticks, counters, and enum ordinals are exact.
- `MassivePoint` and all DTO values are owned. Encounter clone tests mutate both source and clone after capture and verify independent nested collections.

## Maintained integration

Registered as `massive_combat_persistence_parity`. The maintained actual-source generator is `tests/Stellar.MassiveEncounterPersistence.ParityGenerator`; Program.cs and fixture bytes are retained. Only the generator project reference and native test helper include path are relocated. Root reviewed production against source and tightened parity ownership, decoding, source fingerprints and failure boundaries. Combined maintained 0.1.36 validation passed 87/87 CTest and 29/29 Python (`work/native-036-testing.log`). The retained maintained .NET generator reproduced this fixture byte-for-byte. Exact committed export follows.

- `core/include/stellar/core/massive_combat_persistence.hpp` SHA-256 `D0D40E358E03C3AC1FFEAC52B65AF70B4B59FAA93E21D707CC8948004D823B55`
- `core/src/massive_combat_persistence.cpp` SHA-256 `4B04DE2A451E635C197915282FB2387CE358DEDE6FB93CB50413AAA4045D6EC6`
- `native-tests/massive_combat_persistence_tests.cpp` SHA-256 `998B856446973343E70C4439494FC341B542D7873970FAE39C32A17EAA313559`
- `native-tests/fixtures/massive-encounter.json` SHA-256 `64140AC12DD0642C64102AE6C7BC0484B878B69B05603C1EB2BFB1095C36DEB7`
- `tests/Stellar.MassiveEncounterPersistence.ParityGenerator/Program.cs` SHA-256 `8E45AFEEC746B120CA312A0D0AB75924E67DFA7F319C9165D4DFFEE5ECEF34C8`
