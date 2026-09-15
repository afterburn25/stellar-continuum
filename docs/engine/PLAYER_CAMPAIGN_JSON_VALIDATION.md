# Player17 JSON validation

- The actual `CampaignStatePersistenceService.Load` fixture has 65 rows: six
  complete successes and 59 expected errors. It proves root duplicate
  rejection, nested duplicate processing order, wrapper checks, Diplomacy
  decode/invariants, Galaxy format/decode/restore, campaign references, and
  deferred Adaptive Research decode order. Added counterexamples cover
  fractional and overflowing civilization IDs, invalid-then-valid collection
  and nested-record duplicates, nested schema/node/funding widths, real native
  diagnostic offsets after whitespace and a long unknown property, deferred
  missing/null Diplomacy collection failures, and unknown numeric enums that
  reach the authoritative validators. Null and missing Diplomacy string fields
  prove the exact source invariant failures. Research null record/list/string
  and dictionary values prove explicit native representability errors after
  ordered decoding, while null or missing `ProjectFunding` proves source
  normalization to an empty owned vector. A null unrepresentable string before
  malformed funding proves that the later decode error wins.
- Actual-source fixture SHA-256:
  `138CDDA12594A77352294FE265F0632FCF9D3CAE9DEEDBAE869D51FE30ED8FCF`.
  Generator `Program.cs` SHA-256:
  `3EAEDA28DC9EA32C2B8D47AABD6319228FACECBBF2A729B8AA46E776E7D5609D`.
- The fixture is generated twice byte-identically and records full source
  fingerprints before and after the run plus input-file fingerprints before
  and after every actual load.
- The native replay compares source exception categories and exact authored
  campaign messages. Native lexer/type diagnostics assert original absolute
  UTF-8 offsets and typed paths separately without fabricating
  System.Text.Json line/column text. Successful rows activate the stable owner,
  recapture the complete Player17 payload, and compare every represented
  subsystem.
- Native encoded output is loaded and recaptured by the actual C# service. The
  current nonempty world and research state round-trip at source value widths.
  Dedicated native cases reject non-finite Diplomacy and research values before
  a JSON library could turn them into `null`.
- The ignored Gate089 ordered-tree refactor passes the original 58-row Gate089
  source matrix and actual C# interoperability verifier unchanged in strict
  Debug and Release builds.

The dictionary interoperability success populates `MetricSignals` and all four
agenda priority maps with lowercase and punctuation-bearing authoritative IDs.
Native encode followed by actual C# `Load`/recapture preserves every key and
value exactly. The Debug and Release dictionary-rich native outputs are
byte-identical with SHA-256
`037E1AC3BF26334D6884304EB83E3A570DB9690A3B3EF66B0A92BB850DDF2935`.

Both Gate092 strict builds use `/W4 /WX /permissive- /fp:precise` and pass the
65-row replay plus actual C# `Load`/recapture. The populated native output is
342452 bytes with SHA-256
`F0D494456482E197D7CEA1B0537AB2AB66882C56BD0A32288CC6DA3985608309`.
Missing arguments, fixture, and research root each exit 1 with exception type,
message, current directory, and absolute paths or missing placeholders.

The success fixture contains two directional contacts, one relationship, four
funded research projects, and the complete nonempty galaxy. The actual-source
recapture confirms those values after native encode and load.
