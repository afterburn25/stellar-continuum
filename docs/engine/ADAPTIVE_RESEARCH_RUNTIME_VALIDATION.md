# Adaptive Research runtime kernel validation

This maintained port implements the isolated command kernel from the actual
`AdaptiveResearchRuntime.cs`. `AdaptiveResearchRuntimeContent` owns the base,
applicability, facility, and progress catalogs. Loading uses four explicit
statements in source order. Each runtime holds a
`shared_ptr<const AdaptiveResearchRuntimeContent>` in heap-stable storage, then
constructs its eligibility evaluator and view builder against that content.
Several campaign runtimes can therefore share one immutable content version
without reloading it. Runtime moves do not move the content or either borrowed
helper.

The runtime preserves source mutation and event order. Project and node values
are copied before state-writer calls, initial project IDs are captured before
advancement, implication traversal is breadth-first, and candidate wakeups use
the catalog indexes in their existing order. Stage grants emit deployment
permissions without adding them to the state's deployed-event set.

The retained actual-source fixture contains 63 command outcomes across 20
independent scenarios. It covers lifecycle commands, multi-stage completion,
requirements changing after a facility removal, pressure/evidence/trait and
capability wakeups, duplicate suppression, contextual validation, paused
capacity behavior, hypothesis support/disproof, bounded candidate review,
rejections, and exceptional inputs. Every recorded source and native production
call records the canonical JSON byte fingerprint before and after invocation.

The fixture SHA-256 is
`38D088D46286B5DBD6536CA0BE55544AAF60BB4E9B9532DF959FF0E222A17837`.
It contains 49 successful invocations, including four rejected command
results, and 14 exact exceptional outcomes. All 14 runtime event types occur in
the retained replay.

The fixture uses approved writer-equivalent reflection only to establish node
and project states that a campaign would ordinarily supply. Setup occurs
outside each production-operation catch. Every recorded operation calls the
actual public C# runtime and catches only that operation. Results include the
complete before/after state, ordered owned events or blockers, and exact source
exception category/message.

Canonical capability implications are scope-compatible and their targets are
loader-validated, so current data cannot provoke a later implication failure.
The implementation deliberately performs no rollback if a later queued grant
throws, matching the source's partial-mutation behavior. Actual-source rows
record accepted NaN and positive-infinite allocations, negative-infinite
rejection, huge finite formatting, midpoint formatting, and NaN stage-progress
advancement, and an overflow-derived research budget; native behavior matches
those rows. Native revision increments
remain guarded and throw before signed overflow, which is a separately tested
safety boundary rather than a claim about wrapped source state.

Native-only regressions pass IDs and contexts borrowed directly from node,
project, trait, pressure, evidence, and facility state caches into mutating
runtime calls. They also execute move construction and move assignment, and
execute a second shared-content runtime after another owner is destroyed.

This is a runtime-kernel milestone only. It does not include expertise,
funding, outcomes, agendas, foreign technology, deployment execution, campaign
save envelopes, sidecars, or save-v16 compatibility.

The maintained Windows build passed 58/58 CTest and 20/20 Python checks after
this kernel was promoted; see `work/native-049-runtime-testing.log`. The runtime
parity harness uses Windows CNG for its exact input fingerprints and is registered
only on Windows. The production research library does not depend on CNG.

Regenerate and run the maintained validation from the repository root:

```powershell
dotnet run --project tests\Stellar.AdaptiveResearchRuntime.ParityGenerator\Stellar.AdaptiveResearchRuntime.ParityGenerator.csproj --configuration Release -- data\research\v1 native-tests\fixtures\adaptive-research-runtime.json
.tools/build-tools/Scripts/ctest.exe --test-dir build-native/testing -R adaptive_research_runtime_parity --output-on-failure
```

The isolated review builds also passed strict Debug and Release with `/W4 /WX
/permissive- /fp:precise /utf-8`; their `/Fo` objects and `/Fd` compiler databases
were contained in `work/049-research-runtime`. The retained source generator and
maintained CTest target above are the reproducible repository entry points.
