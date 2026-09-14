# Gate 090 validation

Combined maintained release validation passed 100/100 CTest and 29/29 Python
(`work/native-040-combined-testing.log`); the initial harness failure is
retained in `work/native-040-testing.log`. The retained fixture SHA-256 is
`258509C53878D404896005B82A69484A8D163260C6F35F430EBF2173482DC672`, and the
maintained generator `Program.cs` SHA-256 is
`C67A28F316D2939DA7CF7D8FF3FA45926D57448CB11736A0FBF0AE6ECC74BD76`.

This isolated gate composes the maintained historical migration helpers into an owned typed restore boundary for galaxy-only formats 1 through 8, 10 and 12. It does not decode JSON, accept campaign wrapper formats, restore research or diplomacy, or claim Player/Developer save compatibility.

The retained actual-source fixture contains 37 rows and is byte-repeatable with SHA-256 `258509C53878D404896005B82A69484A8D163260C6F35F430EBF2173482DC672`. Ten field-complete success rows cover every supported historical version. `SimulationSeconds` is 123.75 while `SimulationDays` is 37.25, proving the format5 threshold assigns the legacy value directly. Further rows exercise version1 civilization short-circuiting, pre3 fleet short-circuiting and empty preservation, colony/economy version4 order and empty reseeding, technology5, construction6 and shipyard7 null and empty thresholds, surface10 reseeding versus surface12 rejection, empty-civilization reseeding, unsupported galaxy9/11, and null systems.

Every native success is compared directly against the complete source-restored runtime world before any current-format capture. The source fixture separately retains the runtime enumeration order of migrated technology and construction sets; their authoritative contents and order are checked directly. The additional capture comparison checks every nested persistence field and the source capture's sorted collection ordering. Inputs are checked before and after the production call; exact outer and inner error evidence is retained. A native deep-copy probe covers restored systems and nested fleet route storage. Nine source fingerprints are verified before and after replay in their authored order. There are no source-only rows.

Regenerate the source fixture from the repository root:

```
dotnet run --project tests/Stellar.LegacyGalaxyPayload.ParityGenerator/Stellar.LegacyGalaxyPayload.ParityGenerator.csproj -- native-tests/fixtures/legacy-galaxy-payload-persistence.json src/Game
```

Run strict native replay after compiler ownership is released:

```
ctest --test-dir build-native/testing -C Debug -R legacy_galaxy_payload_persistence_parity --output-on-failure
```

Debug and Release strict linked replays each pass all 37 rows under `/W4 /WX`. Logs are retained at `build/debug/run.log` and `build/release/run.log`. Native missing-argument, missing-fixture and missing-source-root runs each exit 1 with the exception type, message, working directory and resolved inputs in `build/release`. Managed generation is byte-repeatable and its missing-argument and missing-source-root runs also exit 1 with useful diagnostics.
