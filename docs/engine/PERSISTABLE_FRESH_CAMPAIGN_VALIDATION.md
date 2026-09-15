# Validation

- The retained fixture contains 8 actual-source rows: 5 successful canonical
  FullGalaxy campaigns and 3 expected configuration failures.
- Every successful row compares the complete returned source world through the
  maintained fresh-campaign comparison helpers, plus every retained generation
  metadata field and both named/geometric core projections.
- Native replay creates every successful campaign twice, verifies source
  settings before/after equality, checks owned timestamp/species lifetimes, and
  requires exact 8/5/3 row accounting.
- The fixture pins all four actual source files before and after generation and is
  itself pinned by SHA-256 in the native replay.
- Two independent managed runs reproduced byte-identical fixture SHA-256
  `38C23A52A8D0D8AB4DFFE8CF40110CE1B3305238FB59C87E9FE8450C9602C9B9`.
- Strict validation uses `/std:c++latest /W4 /WX /permissive- /fp:precise /Z7`
  in Debug and Release; both configurations pass all 8 rows (5 success and 3
  expected failure).
- Missing-argument, missing-source-root, and missing-fixture probes each exit 1
  and report the exception type/message, working directory, absolute supplied
  paths, and placeholders for absent arguments.

## Engine 0.1.39 maintained validation

`persistable_fresh_campaign_parity` passed in the combined 97/97 native suite; 29/29 packaging checks passed (`work/native-039-testing.log`). The maintained generator is `tests/Stellar.PersistableFreshCampaign.ParityGenerator`. Its Program.cs is byte-identical to the reviewed oracle; only the project reference and native helper includes were relocated.

- Retained fixture SHA-256: `38C23A52A8D0D8AB4DFFE8CF40110CE1B3305238FB59C87E9FE8450C9602C9B9`
- Maintained Program.cs SHA-256: `11D57E0E7C62EB0F3E7C3F07AAB8D70EACA309FD41FC00D1E5F604B7E61DFF2D`
