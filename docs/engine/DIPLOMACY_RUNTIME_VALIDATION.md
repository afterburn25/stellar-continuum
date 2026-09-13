# Gate 073 validation

The retained actual-source fixture contains 22 rows. It pins SHA-256 for the
five C# source files, preserves complete operation inputs with before/after
projections, and compares full Diplomacy snapshots plus nested runtime results.

Both strict builds passed with `/W4 /WX /permissive- /fp:precise`: `debug`
and `release` each reported `diplomacy runtime parity: 22 rows passed`.
Additional native checks cover runtime move construction, stable service/state
addresses, a callback retained across the wrapper move, and detached observer
view ownership. Missing arguments and a missing source root both exit 1 through
the top-level typed diagnostic boundary. The managed generator does the same for
missing arguments and an invalid output parent.

The fixture SHA-256 is `11DC3AE99A3D1388E1A4FE97EBB072A86B2AA68AA842C8ACEDE91F460AE68C55`.
Native spans cannot represent a null managed event list. The matrix validates
empty lists and records that null-list exceptions remain outside this native API.

## Maintained integration

The retained generator is `tests/Stellar.DiplomacyRuntime.ParityGenerator/Program.cs`, invoked with `dotnet run` through its repository project. Its bytes are retained exactly from the actual-source oracle. The maintained native consumer is `native-tests/diplomacy_runtime_tests.cpp`; canonical production is `core/src/diplomacy_runtime.cpp`. Source hashes and explicit row accounting are checked by the consumer. Root source and production review preceded promotion.

Root tightened the final replay to pin fixture/schema/count, verify source files before and after, compare decoded native Diplomacy state before invocation, decode runtime arguments outside catches, reject unknown operations, and drive preview fleet/system/order inputs from the retained payload. The preview sequence uses the retained source military-default factory and fixed SetHostile tick/reason; the payload is a relevant projection, not a general campaign save. Native spans exclude null managed event arrays. Borrowed services require stable external state/simulation; outer coordinator moves preserve owned service addresses.

- `native-tests/fixtures/diplomacy-runtime.json` SHA-256 `11DC3AE99A3D1388E1A4FE97EBB072A86B2AA68AA842C8ACEDE91F460AE68C55`
- `tests/Stellar.DiplomacyRuntime.ParityGenerator/Program.cs` SHA-256 `994A6065625C8722C693189B21DBF352C7B8AD2E115B74F342159DDF136F6085`
- `core/src/diplomacy_runtime.cpp` SHA-256 `7B2905D375E48AD8A0E0F1A5CE8DA0A63E32A082399B52840A19122CDDC44464`
- `core/include/stellar/core/diplomacy_runtime.hpp` SHA-256 `1112EDB5B11CEE921EBB4803ED4F65572F249562607D4E873FBD15D446FDA7B0`
- `native-tests/diplomacy_runtime_tests.cpp` SHA-256 `F0D56C58DCEBD1FB194A846C102810203EC1D18F59550A16542A55B011AEF18A`

The first maintained run exposed a test-only schema assertion using an integer instead of the retained named schema string; the assertion was corrected without changing production, generator or fixture bytes.
