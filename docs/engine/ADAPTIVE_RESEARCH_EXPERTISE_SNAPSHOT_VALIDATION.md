# Adaptive Research expertise snapshot validation

Gate 054 ports the standalone schema 2 Adaptive Research snapshot envelope from
`AdaptiveResearchExpertiseSnapshot.cs`. It captures the maintained schema 1 core
payload plus sparse field competence, institutions, and tacit assets. It remains
separate from the campaign save envelope.

The retained actual-source generator is
`tests/Stellar.AdaptiveResearchExpertiseSnapshot.ParityGenerator` after
promotion. From the repository root, regenerate the fixture with:

```powershell
dotnet run --project tests/Stellar.AdaptiveResearchExpertiseSnapshot.ParityGenerator/Stellar.AdaptiveResearchExpertiseSnapshot.ParityGenerator.csproj -c Release -- data/research/v1 native-tests/fixtures/adaptive-research-expertise-snapshot.json
```

The isolated draft uses the sibling oracle at
`../research-expertise-snapshot-oracle-054` and writes
`work/054-research-expertise-snapshot/actual-source-fixture.json`.

The fixture contains 81 actual C# production rows. They cover all eight
canonical starting profiles, active and paused projects, schema 1 fallback,
full restored core and expertise state with revision counters, field,
institution and tacit validation order, exact mapped typed-restore errors,
lab-capacity mismatch behavior,
unsorted and duplicate saved physical facility capabilities, enum string and
integer inputs, missing/default/null members, Int32 boundaries, UTF-16 ordinal
asset ordering, enum `+1` and comma-separated strings, bounded invariant
general-number diagnostics, and non-finite serialization rejection. Every production call
records unchanged canonical input fingerprints.

The retained fixture SHA-256 is
`B5F4158028850936EFE7C9C02E1B45AA88EC0E92B4ABC08D4533914ABE3D2329`.
Its canonical research-data fingerprint is
`2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.

Run the isolated strict native configurations with outputs, object files, and
program databases contained under the ignored draft directory:

```powershell
python work/054-research-expertise-snapshot/build_strict.py debug
python work/054-research-expertise-snapshot/build_strict.py release
```

Both strict configurations pass all 81 rows. The retained managed runner was
also invoked through `dotnet run` with no arguments and with a missing research
root; each printed the full exception and stack, current working directory,
research root, and fixture path, then exited with status 1 without a popup.

The source codec deserializes the complete schema 2 record before `Restore`.
The native codec intentionally reuses the maintained schema 1 parser and can
therefore report a schema 1 semantic error before a malformed later expertise
member. Two mixed-fault rows retain this error-order boundary; the validation
does not claim universal malformed-payload diagnostic precedence.

The native replay preserves exception types. Source `InvalidDataException`
maps to `AdaptiveResearchSnapshotError`; JSON conversion,
`InvalidOperationException`, `FormatException`, `NullReferenceException`, and
the null-core `ArgumentNullException` boundary map to
`AdaptiveResearchSnapshotJsonError`. Source `ArgumentException` and
`ArgumentOutOfRangeException` raised by expertise services map to
`std::invalid_argument` and `std::out_of_range`, with exact demonstrated
messages for typed restore calls.

The C# serializer also permits null values for record parameters declared as
non-null strings and can retain some of them in runtime state. The native DTOs
own non-null `std::string` values, reject those source-only null inputs with a
typed JSON error, and retain separate fixture rows proving each rejection.
Successful
payloads compare exact bytes where all strings are ASCII. The non-ASCII ordinal
row compares parsed JSON semantics because System.Text.Json escapes those code
points while nlohmann/json emits UTF-8.

The invariant general-number diagnostic helper is bounded to the retained
values (`1E+300`, `-1E-08`, `1000000`, `1000000000000000`,
`10000000000000000`, `-0.0001`, and `-1E-05`). This gate does not claim
arbitrary numeric serialization-byte equivalence beyond the successful fixture
payloads.

Schema 1 fallback delegates directly to the maintained schema 1 codec. Its
documented malformed-input limitations remain unchanged, and fallback does not
reconstruct expertise or recalculate project readiness.

The combined maintained engine 0.1.25 build passed 63/63 CTest and 20/20 Python checks in `work/native-054-recovery-testing.log`. The snapshot replay target is Windows-only because its retained fixture hashing uses BCrypt; production snapshot code has no Windows dependency. Exact committed package verification follows separately.

The exact clean 0.1.25 export of `aa3eaa834bd9c8570bde4d70b4265f47da1a5d2a` passed the combined 63/20 checks and all retained relocated checks. Package `StellarContinuum-windows-benchmark-aa3eaa83-20260913T120628476660Z` records `sourceDirty: false` and seven sealed runtime files; see `work/native-025-clean.log`.
