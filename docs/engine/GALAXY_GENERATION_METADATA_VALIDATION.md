# Gate 078 validation

The retained fixture contains 22 actual-source rows. Twenty-one invoke the
private source validators directly to preserve exact validation order and
errors. The eighteenth creates a deterministic 250-system campaign and invokes
the real `CampaignSaveService.CapturePayload` path, retaining the bounded
generation-metadata and landmark projection. It covers optional legacy
absence, every retained field, independent metadata/state landmark presence,
the strict-inside overlap rule, invalid key/numbers/radius, seed and count
agreement, nonnegative counts, landmark disagreement, and first-error order.

The generator pins the reviewed source bytes. Two independent fixture runs
were byte-identical. Fixture SHA-256:

`D411FB95DA948B43C64096D391070D042488AD57F3978473B1529FDACA7DCFB9`

Generate from the repository root:

```text
dotnet run --project ../galaxy-metadata-persistence-oracle-078/Stellar.GalaxyMetadataPersistence.ParityGenerator.csproj -c Release -- . work/078-galaxy-metadata-persistence/actual-source-fixture.json
```

Strict native replay:

```text
C:/Python314/python.exe work/078-galaxy-metadata-persistence/build_strict.py Debug
C:/Python314/python.exe work/078-galaxy-metadata-persistence/build_strict.py Release
```

Both configurations compile as C++23 with `/W4 /WX /permissive- /fp:precise
/utf-8 /Z7` and pass all 22 rows. Typed float decoding explicitly handles the
managed named nonfinite values and normalizes expected floats to native
single-width values before comparison. The native missing-argument,
missing-root, and missing-fixture probes, and the managed missing-argument
probe, all return 1 with bounded diagnostics.

## Maintained integration

Registered as `galaxy_generation_metadata_parity`. The maintained actual-source generator is `tests/Stellar.GalaxyMetadataPersistence.ParityGenerator`; Program.cs and fixture bytes are retained. Only the generator project reference and native test helper include path are relocated. Root reviewed production against source and tightened parity ownership, decoding, source fingerprints and failure boundaries. Combined maintained 0.1.36 validation passed 87/87 CTest and 29/29 Python (`work/native-036-testing.log`). The retained maintained .NET generator reproduced this fixture byte-for-byte. Exact committed export follows.

- `core/include/stellar/core/galaxy_generation_metadata.hpp` SHA-256 `1129AEFB00CCC0F5179A2732132681D9615160AAD7A7C04F2C75222F5B37775C`
- `core/src/galaxy_generation_metadata.cpp` SHA-256 `B409FD21886AB6D27C42044E117B9107F31F741E2E8A694E60D2864C16E6EC9B`
- `native-tests/galaxy_generation_metadata_tests.cpp` SHA-256 `C58D7666505ED09D50B8DD596DB2C665D07EBC0AA99F0D9BA04F1BD5E6540A12`
- `native-tests/fixtures/galaxy-metadata-persistence.json` SHA-256 `D411FB95DA948B43C64096D391070D042488AD57F3978473B1529FDACA7DCFB9`
- `tests/Stellar.GalaxyMetadataPersistence.ParityGenerator/Program.cs` SHA-256 `4CC08CA6D5CB57A2AFFFB2416B4626A2664BEB335C73AC4D3429F51A7293FD7A`

Root additionally rejects unknown fixture operations before the expected-error boundary. This changes only the maintained replay, not production or the actual-source fixture.
