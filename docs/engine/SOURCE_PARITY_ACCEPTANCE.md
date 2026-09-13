# Source parity acceptance

Migration evidence must execute the actual retained C# authority and replay the corresponding typed C++ operation. A fixture row, a source-only null observation, a forwarded dependency case and a native safety probe are different evidence categories. Report their counts separately. Do not accept a consumer that merely counts records or tests hardcoded native outputs while ignoring the recorded operations.

## Exception capture and state

Arrange source worlds, resolve fixture references, validate command kinds and construct typed arguments before the expected-operation try block. Capture only the production invocation and its typed result. Normalize, enumerate for serialization, encode, fingerprint and assert afterward. Apply the same boundary in the native consumer: JSON access or harness lookup failures must never satisfy an expected production exception.

Replay every command in order. Compare complete returned records, exact messages and relevant callback observations after each command. Stateful cases also compare the world left by failures and subsequent retry behavior. Freeze mutable source objects at the time of each observation. For read-only ports, compare the native input to the source input as well as before/after native fingerprints; describe exactly which fields a reduced fingerprint covers. Const views establish a nonmutation boundary but do not prove that the consumer decoded the right input.

Integers, IDs and ticks compare exactly; never convert them to double for a relative-tolerance comparison. State the floating-point comparison used. Pin source culture to InvariantCulture for reproducible formatting, and preserve custom .NET number formatting where it is player-visible. Undefined C++ conversions require explicit checked boundaries with exact errors and documented partial-state behavior, rather than an invented source-parity claim.

Use the existing `detail::legacy_custom_fixed` helper for .NET custom fixed-point messages instead of adding a second formatter based on binary rounding or shortest decimal output. It preserves the source's decimal working precision, midpoint rounding, padding and named non-finite values. Separately inspect `Math.Min`/`Math.Max` operand order when non-finite inputs can reach an operation: ordinary `std::min`/`std::max` do not universally propagate NaN like the source.

## Composition and ownership

Source constructors and native configurations must wire equivalent dependencies. In particular, supplying a custom shipbuilding capability to the C# shipbuilding service does not automatically supply it to its strategic input builder. Compare the explicit composed configurations actually being exercised.

When multiple services share a source policy object, native adapters must share one callable instance. Test this with a mutable counter captured by value inside the callable, while recording observations externally. A counter stored only in an external shared log cannot detect accidental independent callable copies. Test move/lifetime boundaries separately from C# object identity. Owned native plan values must not be compared to source reference identity as though equality implied caching.

Compile the public headers together as integration proceeds. Isolated translation units can hide namespace/type collisions. Recreate borrowed views after vector growth; retain authoritative runtime caches across calls. Do not eagerly materialize interface responses when the source queries them on demand: this changes callback order, unlock visibility and failure mutation boundaries.

Prepared test callables must own their string/context arguments. Construct optional views inside the callable from its captured owner at invocation time; capturing an owner and a view into the original local variable still leaves a dangling view. Include a non-SSO context regression.

Public `string_view` and span arguments can themselves point into the state being changed. Copy arguments needed after mutation before rebuilding/removing any referenced collection. Include direct alias-input regressions. Move/lifetime probes must execute methods that actually use borrowed dependencies; testing a scalar setter alone does not prove that moved evaluators or view builders remain valid.

## Evidence and packaging

Retain generators under `tests/` only after review; keep scratch oracles outside the repository because the game project can glob their C# files. Put native executable/object/PDB outputs in ignored build directories. Run strict Debug/Release checks appropriate to each port and one coordinated maintained build for the combined milestone. Verify the actual log's test counts and completion, then export from the exact committed source. A clean package requires its manifest's source commit and sourceDirty=false; an earlier dirty build is not that evidence. Restricted-PATH relocation and initialization benchmarks do not establish separate-machine, player-save, graphical or FPS parity.

Retained C# oracle entry points must catch unexpected loading, setup and output exceptions at the outermost boundary, print the full exception (including inner exceptions and stack), working directory and relevant input/output paths, and return nonzero. Run them through `dotnet run` or the established test invocation; missing data must not launch a Windows CLR error dialog. Expected-operation catches remain narrow inside that outer diagnostic boundary.

When promoting an isolated draft over an existing source or header, write the exact approved bytes with a current modification timestamp. Windows Copy-Item can preserve a scratch file timestamp older than an already-built object, causing Ninja to reuse stale code during an incremental check. Gate 055 demonstrated this with the new state-identity accessor: source time 11:43 UTC, object time 11:58 UTC. Refresh promoted file timestamps or use the maintained clean build; verify the relevant source recompiles. Retain an initial stale-object failure separately and never count the stale attempt as passing evidence.

### Gates057–058 / engine0.1.27

The maintained combined Windows testing build passed67/67 CTest and20/20 Python (`work/native-027-testing.log`). Gate057 foreign technology compares complete core/support before and after29 commands,17 malformed loads,8 low-level operations and native lifetime/alias probes; one duplicate-property parser mismatch is explicit source-only evidence. Fixture SHA `00F7D1AE332F8049E469CB84E9BC594A78D4012671ADE8E2BE585A11B43EF0A1`. Gate058 outcomes retains82 actual-source rows across all9 outcome kinds, portable SHA/UTF8 rolls, readiness, side discoveries, history, typed invalid commands and catalog boundaries. Fixture SHA `DF9B002792450465A6437B1A08D9FEB4647F27512BBFA53352AB130810A78195`. The1e400 parser-category difference and guarded native integer-overflow boundary remain documented; these are not universal JSON/runtime equivalence claims. Both gates passed isolated strict Debug and Release. Scope and retained evidence: `ADAPTIVE_RESEARCH_FOREIGN_TECHNOLOGY_VALIDATION.md` and `ADAPTIVE_RESEARCH_OUTCOMES_VALIDATION.md`.

Discovery, stable owner composition and schema3 support recovery remain isolated next gates. `CAMPAIGN_PERSISTENCE_MIGRATION_CONTRACT.md` clarifies player wrapper17 versus inner galaxy16; neither these gates nor standalone research codecs establish campaign save compatibility.

### Gates059-061 / engine0.1.28

Foreign discovery retains 18 commands and 14 catalog cases (SHA `8DF43CBE4B49B82D2CACEC3B5989498702ABEAA433544BD4FB17A9DB23992F47`), delegates factual changes before bounded graph awareness and preserves partial failure. The owning facade proves one complete catalog/runtime graph (fixture SHA `9DF23BE6FB32F90AF2D14ED867103FAD69D6DE0C2C4F27A5AA29A3501E2C470C`), stable addresses for all 11 getters, support survival across owner moves and isolation of copied/displaced states. Schema3 retains 53 actual-source rows (SHA `6F196AF14BCC96BBBC107BDEBAA0B36F6F57A2B0B99FA5B717711781A5157E40`), including seven explicit null-record boundaries. Corrected successful typed replay compares serialized data, transient revisions and next behavior; nonfinite capture invokes native code, and authored nested schema errors are exact. The DTO-only internal seams avoid repeated Restore/Capture during nested decoding. Null-reference and compound malformed-parser precedence limits remain documented.

Initial maintained validation passed 70/70 CTest plus20/20 Python. Final isolated Debug/Release passed53/81/73 V3/V2/V1 source rows; all five affected maintained tests passed after final corrections. Logs: `work/native-028-testing.log`, `work/native-028-final-focused.log`. Exact committed export follows. `DIPLOMACY_CAMPAIGN_INTEGRATION_CONTRACT.md` records the actual player substep order and observer-safe composition required next; it is a design/source review, not a native Diplomacy completion claim.
