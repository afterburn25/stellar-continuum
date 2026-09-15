# Gate 074 validation

Authority:

- `src/Game/Simulation/Combat/FleetCombatPower.cs`
- `src/Game/Simulation/Combat/Massive/MassiveCombatOutcome.cs`, limited to `MassiveCombatPowerCalculator.PerShipPower`
- `src/Game/Presentation/Main.MassiveCombat.cs`, limited to `HasCombatScanner`

The managed generator runs with invariant current and UI cultures, catches top-level failures, and writes UTF-8 without a BOM. Two independent runs produced identical fixture SHA-256 `D146FDCAD77E15D20ED1505DEE40111CD9793D3E330B4C87CAA27502859FA1A6`. The combined source authority fingerprint stored in the fixture is `904DA29FE59D531036AA95D9998395F07DB00752A1E65185B421AE0CCEE42954`.

Strict native replay commands:

```text
ctest --preset windows-testing -R fleet_combat_intelligence_parity --output-on-failure
python tools/stellar-export/stellar.py build windows-testing
```

Both configurations compile as C++23 with `/W4 /WX /permissive- /fp:precise /Z7`, explicit per-configuration `/Fo` and `/Fd` paths, and replay all 39 native rows. The one source-only row is reported separately. The fixture parser, argument decoding, operation validation, result projection, and state comparison are outside production-operation catches.

The native executable returns 1 with exception type, message, current directory, and fixture path where available for missing arguments and missing fixtures. The managed generator returns 1 with a usage message for missing arguments and has a top-level diagnostic catch.

This gate adds an owned `FleetPowerObservation` DTO and an appended `FreshCampaignState::combat_intelligence` field. It makes no player-save compatibility claim and adds no combat formation, aftermath, or campaign-host rules.

## Maintained integration

The retained generator is `tests/Stellar.FleetCombatIntelligence.ParityGenerator/Program.cs`, invoked with `dotnet run` through its repository project. Its bytes are retained exactly from the actual-source oracle. The maintained native consumer is `native-tests/fleet_combat_intelligence_tests.cpp`; canonical production is `core/src/fleet_combat_intelligence.cpp`. Source hashes and explicit row accounting are checked by the consumer. Root source and production review preceded promotion.

Review corrected C# float-selector / double-accumulator Sum behavior and Math.Max NaN propagation. Observation eviction matches .NET record double equality, including NaN and signed zero, using ordered lookup rather than a quadratic removal scan. Fleet membership rejection preserves prior records; scanner filtering stably takes the first2,048 qualifying IDs from2,050 visible fleets in the retained source case. Per-ship power is the only MassiveCombatOutcome calculator ported here; no formation/aftermath or player-save codec claim.

- `native-tests/fixtures/fleet-combat-intelligence.json` SHA-256 `D146FDCAD77E15D20ED1505DEE40111CD9793D3E330B4C87CAA27502859FA1A6`
- `tests/Stellar.FleetCombatIntelligence.ParityGenerator/Program.cs` SHA-256 `9F60D5FBAA252162D2B77EB9F24D9710AC9F2C5D049605D01F3B65779E07D289`
- `core/src/fleet_combat_intelligence.cpp` SHA-256 `55B0FD53BD9585FFE1AD1E5947E03EF2634C0A8758ECFA63C6AC3032826E659A`
- `core/include/stellar/core/fleet_combat_intelligence.hpp` SHA-256 `11FF19F43F65EC2B8AFFC4BC282D4BE85B81131C14C991564812E1838FBF9994`
- `native-tests/fleet_combat_intelligence_tests.cpp` SHA-256 `9EAA27576D0B1B8BC5D1F8DC7EA913FC5A1E0D574D6D8B030834032E0D351510`
