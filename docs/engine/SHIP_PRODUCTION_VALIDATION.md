# Ship-production validation

## Scope

Engine 0.1.10 validates the landed native ship definitions, full fleet value state, shipyard value state, starter-fleet seeding, and shipbuilding commands. The native work retains all earlier catalog, founding, colony, construction, industry, currency, and logistics ports.

The actual-C# oracle case counts are 37 ship-design cases, 70 fleet-state cases, 72 shipyard-state cases, 27 fleet-seeding cases, and 89 shipbuilding cases.

## Fixture regeneration

From the repository root, the landed generators were run against the live `Game.csproj` without rebuilding its project reference:

```powershell
dotnet run --project tests/Stellar.FleetSeeding.ParityGenerator/Stellar.FleetSeeding.ParityGenerator.csproj -p:BuildProjectReferences=false -- work/fleet-seeding-live-regenerated.json
dotnet run --project tests/Stellar.Shipbuilding.ParityGenerator/Stellar.Shipbuilding.ParityGenerator.csproj -p:BuildProjectReferences=false -- work/shipbuilding-live-regenerated.json
```

The regenerated files exactly matched the checked-in native fixtures:

| Fixture | Cases | SHA-256 |
| --- | ---: | --- |
| `native-tests/fixtures/fleet-seeding.json` | 27 | `6DF37141BC6B1CCC528EDC738133DB86C77EA3233E2A409FFDC6CD6F86CE5D6D` |
| `native-tests/fixtures/shipbuilding.json` | 89 | `6B835E2B02F3A1B0FDC124750A618405616DC8AA1CCB7F8B427A12CD9ADB4F59` |

## Windows package validation

The maintained wrapper was run sequentially:

```powershell
C:/Python314/python.exe tools/stellar-export/stellar.py export windows-benchmark
C:/Python314/python.exe tools/stellar-export/stellar.py export windows-development
```

Each export reconfigured and built its native preset, passed 24/24 CTest and 16/16 Python export tests, validated the manifest, completed relocated restricted-PATH checks, and produced a ZIP.

| Package | Manifest provenance | Package and ZIP | Validation record |
| --- | --- | --- | --- |
| Benchmark | engine `0.1.10`; commit `e80e87f90563801166aeb68d8d8b8b9ec6ce79f3`; `sourceDirty: true`; 7 files | `Builds/Windows/StellarContinuum-windows-benchmark-e80e87f9-20260913T031134742468Z` and `.zip` | `Builds/Windows/StellarContinuum-windows-benchmark-e80e87f9-20260913T031134742468Z-validation.json` |
| Debug development | engine `0.1.10`; commit `e80e87f90563801166aeb68d8d8b8b9ec6ce79f3`; `sourceDirty: true`; 8 files | `Builds/Windows/StellarContinuum-windows-development-e80e87f9-20260913T031246682304Z` and `.zip` | `Builds/Windows/StellarContinuum-windows-development-e80e87f9-20260913T031246682304Z-validation.json` |

The logs are `work/native-020-release.log` and `work/native-020-debug.log`.

## Limits

`--seed-colonies` exposes seed defaults and construction state; it does not run full shipyard production. The clean exact-source 0.1.10 benchmark is `Builds/Windows/StellarContinuum-windows-benchmark-d4e74593-20260913T031841630180Z`: commit `d4e745938a94ea98d5386ea02243e999451fa548`, engine 0.1.10, `sourceDirty: false`, 24/24 CTest, and 16/16 Python export checks. Local transit is being integrated for 0.1.11; lane graph traversal, operational reach/refueling, campaign orchestration, research, save-v16, and graphics remain outside this checkpoint.
