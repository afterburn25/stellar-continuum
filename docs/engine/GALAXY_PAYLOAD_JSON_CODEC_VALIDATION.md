# Validation

- Actual source generator: 58 rows, repeated byte-identically.
- Source categories: 16 System.Text.Json deserialize errors and 4 serialize
  errors, captured separately.
- Native replay: 27 successful rows and 31 named phase failures, with exact row
  accounting and complete DTO comparisons.
- Coverage includes BOM, Unicode and HTML escaping, case sensitivity, ordered
  duplicates, integer widths, direct Single rounding, underflow, non-finite
  write rejection, required properties, nullable representability,
  DateTimeOffset normalization and range checks, malformed input, and trailing
  input. The matrix also fixes exact depth 64/65 behavior for empty containers
  and scalar leaves, fresh replacement by
  duplicate Galaxy objects, saturated huge-exponent handling including signed
  zero, and explicit Diplomacy/AdaptiveResearch exclusions.
- Native output passes actual System.Text.Json deserialization, canonical value
  comparison, CampaignSaveService restore, and complete galaxy recapture.
- Debug and Release use `/std:c++latest /W4 /WX /permissive- /fp:precise /Z7`.

## Frozen evidence

- Combined maintained release validation: 100/100 CTest and 29/29 Python in
  `work/native-040-combined-testing.log`; the initial harness failure remains
  in `work/native-040-testing.log`.

- Fixture SHA-256: `BE0C842BC3D6473EBA58FA36EFB3D55AB18CB5CD1C95344A027DBC1C1BA691F8`.
- Actual-source generator `Program.cs` SHA-256:
  `4C06E9C3F50FC0EB8085D4C07B91722438BEA61B56A656484B070DB5BDC1986D`.
- Reproduce the managed fixture from the repository root with
  `dotnet run --project tests/Stellar.GalaxyJsonCodec.ParityGenerator/Stellar.GalaxyJsonCodec.ParityGenerator.csproj -- src/Game native-tests/fixtures/galaxy-payload-persistence.json native-tests/fixtures/galaxy-payload-json.json`.
- CTest invokes `stellar_galaxy_payload_json_tests` with the fixture path and
  `${CMAKE_BINARY_DIR}/galaxy-payload-json-native-output.json`. Verify that
  native output through the actual source entry point with
  `dotnet run --project tests/Stellar.GalaxyJsonCodec.ParityGenerator/Stellar.GalaxyJsonCodec.ParityGenerator.csproj -- verify-native <native-output> native-tests/fixtures/galaxy-payload-json.json <interop-report>`.
