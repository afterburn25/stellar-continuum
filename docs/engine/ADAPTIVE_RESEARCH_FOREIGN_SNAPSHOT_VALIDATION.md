# Adaptive Research foreign-technology snapshot validation

Gate 062 ports the source schema-4 snapshot envelope. It composes the schema-3
DTO codec, captures foreign assessments and packages in UTF-16 ordinal order,
and restores foreign metadata directly through a private runtime-support seam.
Restore validates and writes packages before assessments and never replays
package acquisition, evidence creation, tacit-asset creation, or discovery.

The actual C# fixture retains complete schema-4 DTOs and serialized state. Each
successful native row compares the full serialized schema-3/core/expertise,
pressure, agenda, package, and assessment state plus the otherwise transient
foreign-support revision. Typed inputs are encoded before and after native
Restore to prove input ownership. The matrix covers schema1/2/3 top-level
fallback, nested schema0/2 rejection, package-before-assessment failure order,
all package/assessment reference families, numeric and symbolic enum behavior,
and a post-restore foreign mutation. JSON converter wording is treated as a
parser boundary; authored snapshot errors are compared exactly.

The nonfinite boundary is counted separately: typed Restore accepts an infinite
last-assessment year as the source does, while serialization rejects it with
the exact source ArgumentException text. Debug and Release use C++23,
`/W4 /WX`, explicit `/Fo` and `/Fd`, and isolated ignored output directories.

The final retained fixture contains 25 source rows. Debug and Release both
passed all 25 plus the separately asserted nonfinite boundary. The research
catalog fingerprint is
`2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.
SHA-256 fingerprints are:

- fixture: `BB5EA35D1CC642BA465504AA90E3B40574EFFFC35FA6DC7A9AC50E5B85EDEA6E`
- managed generator: `40634411E3433CB6923964A4C2BAB2BD059A62BE5C36F4FB707E1B648FB4814E`
- managed project: `E528B25122681575ED2D29315160CFC7AE4F654294CC6E79DD202CE447334AB9`

Managed missing-argument and missing-research-root runs both returned exit 1
with usage or full type/message/CWD/root/fixture diagnostics. Strict transcripts
are retained under `build-debug` and `build-release` as `compile.log` and
`run.log`.

```text
dotnet run --project tests/Stellar.AdaptiveResearchForeignSnapshot.ParityGenerator -c Release -- data/research/v1 work/062-source-regenerated.json
python work/062-research-foreign-snapshot/build_strict.py debug
python work/062-research-foreign-snapshot/build_strict.py release
```

Maintained CTest integration is added in engine 0.1.29. These are standalone
research codecs; the exported campaign host still advances legacy research.
Campaign schema2/player wrapper17 compatibility is not established by this gate.

Exact committed export `de6c750c366d1bbf2fb4423ddf1f9db18da6dcf5` passed all72/72 CTest and20/20 Python checks. Package `Builds/Windows/StellarContinuum-windows-benchmark-de6c750c-20260913T151850838371Z` has `sourceDirty=false`, seven hashed runtime files and all nine relocation/recovery flags true (`work/native-029-clean.log`). Separate-machine certification remains open.
