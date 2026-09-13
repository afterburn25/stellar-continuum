# Gate 063 validation

The retained fixture contains 51 actual-source rows from
`AdaptiveResearchOutcomeSnapshot.cs`: eight canonical profile round trips,
four schema 1-4 fallbacks, twenty typed restores, fifteen JSON deserialize
rows, three source null boundaries, and one typed restore against an owned copy
of the catalog with a negative global history capacity. The last row retains
the exact changed UTF-8 file bytes and proves identical source/native changed
directory fingerprints before and after the production call.

The fixture SHA-256 is
`BD42BBD523DCD9593388AD6588AF17C492F04F86BED8F64D12E54D042623C84C`.
The retained generator SHA-256 is
`EA36989FD54BC8B530291904115E553FE89663F9D768357148D6F2D3F28ADE5A`.
The canonical research fingerprint is
`2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.
The changed negative-capacity fingerprint is
`2A14FBF2227AF32BE9B07A7FF1864C26C2E8C12571D57643F0C26D7AA8B60D4D`.

Both strict configurations passed with MSVC `/W4 /WX /permissive- /fp:precise`:

```powershell
python work/063-research-outcome-snapshot/build_strict.py debug
python work/063-research-outcome-snapshot/build_strict.py release
```

Each executable replayed all 51 rows. Object and program-database outputs are
contained under `work/063-research-outcome-snapshot/build-debug` and
`build-release` through explicit `/Fo` and `/Fd` arguments. The retained
managed generator also exits 1 with a full exception, stack, current directory,
research root, and fixture path for missing arguments and a missing research
root; those runs are in `managed-missing-args.log` and
`managed-wrong-root.log` and were invoked through `dotnet run --no-build`.

Successful restores compare civilization, state/materialized/expertise and
per-node/per-project revisions, outcome revision, ordered summaries, ordered
history, deterministic next outcome, and stable serialized bytes where all
numbers are finite. The codec is executed after move construction and move
assignment. Schema 1-4 inputs exercise the maintained nested codecs rather than
duplicating their logic.

Authored schema, catalog, nested-schema, summary, history, and capacity errors
compare exact source categories and messages. Parser implementation boundaries
are recorded separately: string/wide schema values, null root/collections,
wrong collection shapes, and Unicode-spaced numeric enum strings have exact
source exception categories and exact native `AdaptiveResearchSnapshotJsonError`
messages, without claiming the platform parser categories are identical. A
typed negative-infinity summary year restores successfully in both
implementations; later serialization is a source `ArgumentException` and an
exact native nonfinite JSON error. Compound malformed JSON can still differ in
which parser/shape fault is observed first, so these rows do not establish
universal malformed-error precedence or arbitrary numeric byte equivalence.

One typed row records a deliberate integer boundary separately from parity:
the C# source accepts `Int64.MaxValue` as a history sequence because its
unchecked `record.Sequence + 1` wraps before `Math.Max`, while native rejects
before mutation with the exact checked error `Adaptive Research outcome
sequence overflow.` The row asserts both behaviors and does not count the
different results as semantic equivalence.

Maintained CTest integration is added in engine 0.1.29. These are standalone
research codecs; the exported campaign host still advances legacy research.
Campaign schema2/player wrapper17 compatibility is not established by this gate.
