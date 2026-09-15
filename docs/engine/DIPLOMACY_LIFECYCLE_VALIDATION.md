# Gate 071 validation

The retained actual-source generator invokes the six bounded lifecycle source
files directly. Rows retain owned inputs, before/after input serialization,
before/after source fingerprints, exact authored error types and messages,
owned results, full state snapshots, and observer views.

Coverage includes clock floor/saturation and invalid values, policy validation
order/defaults, contact aging and partial failure, proposal default/override
expiration and partial failure, equal-latest contact selection, mutual
communication validation without one-way partial mutation, agreement access
and ceasefire effects, idempotent termination, source argument order, cadence
reset/lazy/idempotent/delayed/saturating behavior, and maintenance partial
failure with unchanged cadence. The separate native-only duplicate override
boundary and service moves/aliased termination reason are also executed.

The fixture contains 40 actual-source rows. Its SHA-256 is
`D58C8F45255FF7D7897FDD4E57FCAC4A8EF7FA38E599654F44493F4A3A04712B`;
the generator SHA-256 is
`91A90B9B7FD08777AC11457BEB0B5C96BE98886E4A94849B7EEAC3C6746FA005`.
The combined fingerprint of all six source files before and after every call
and each scheduler step is
`EF9E341BA89D7BB94318C74D4FCB88A6E1FD1C717B95800407D521922939963C`.

Strict Debug and Release commands use MSVC `/W4 /WX /permissive- /fp:precise`
and explicit draft-contained `/Fo` and `/Fd` outputs:

```powershell
python work/071-diplomacy-lifecycle/build_strict.py debug
python work/071-diplomacy-lifecycle/build_strict.py release
```

Both Debug and Release replays passed all 40 rows plus the separately classified
native duplicate-key boundary. Native missing-argument and missing-fixture
probes and managed missing-argument and missing-root probes exit 1 through
top-level diagnostics with exception details, working directory, repository
root, and fixture path.

Maintained CTest: `diplomacy_lifecycle_parity`. Retained actual-source generator: `tests/Stellar.DiplomacyLifecycle.ParityGenerator`; use `dotnet run --project` with explicit root/fixture paths. Isolated draft command paths above are evidence, not shipped executables.
