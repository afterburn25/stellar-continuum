# Gate 042 validation

From repository root, regenerate the source fixture and run the maintained native
comparison with:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchCatalog.ParityGenerator -c Release -- data/research/v1 work/adaptive-catalog-regenerated.json
ctest --preset windows-testing -R adaptive_research_catalog_parity --output-on-failure
```

The generator explicitly pins both current culture and UI culture to invariant
culture before invoking the retained source. Root review added the missing culture
assignment and regenerated the fixture with the same SHA-256 recorded below.
The normal Windows build compiles the native test first.

The reproducible `stellar-adaptive-research-catalog-oracle-v1` fixture contains
103 actual-source cases and three source-only observations. It projects every
canonical node and ordered catalog collection, plus generated-copy parse,
defaulting, rounding, duplicate, metadata, numeric, and reference failures. Its
SHA-256 is
`D609FB34574E01E876DDFC513D5924282B59F9312A4263D679C268F4B1F6EF41`.

The strict MSVC C++23 Release and Debug builds use `/W4 /WX /permissive-
/fp:precise /utf-8` and explicit draft-local `/Fo` and `/Fd` outputs. Both replay
all 103 cases successfully. Release compile/replay log SHA-256 values are
`9149AB763A68F9121C8B67828B13EC5A818825C2D40FE2AA27BF1226873B4C06`
and `4CA913E0A69C2F1FB13B66FEA435C6845D6848FD1C8F3BABA7C20DE4917A1BE8`.
Debug compile/replay values are
`305F52B67ECA628EAFE43F1C8B8E4D5054EE22772927A618DD311EEAACE6303F`
and `0658721A8B3939E983CD781071AF8B2E74B8B996C870D2F12035C70C5DAA4687`.

Native boundary probes verify move-only catalog ownership, deliberate moved-from
reassignment, transparent nonallocating `string_view` lookup, all five wake-index
queries, and exact empty/whitespace path rejection. Borrowed references and spans
are documented as invalid after catalog move, reassignment, or destruction.

The maintained `windows-testing` build passed all 51 CTest tests, including
`adaptive_research_catalog_parity`, and all 20 Python recovery and package-integrity
tests. The captured log is `work/native-042-adaptive-catalog-testing.log`, with
SHA-256
`4FEF982A5BD7CEF1D6452AD82231AD5CD2C20E34A6DAB05FFF8FF1FA12642A26`.

This gate covers immutable catalog data, directory loading, reference validation,
ordered collections, and indexes. It does not implement adaptive research state,
progression, gameplay, campaign integration, or export packaging, and it does not
change the canonical research data. Semantic catalog errors are compared exactly
after fixture-root normalization. JSON parser, JSON value-access, and numeric-format
diagnostics retain the actual source category and message in the fixture while the
native replay requires the corresponding explicit category and a nonempty native
diagnostic, because those implementation-specific messages cannot be identical
across System.Text.Json and nlohmann/json.
