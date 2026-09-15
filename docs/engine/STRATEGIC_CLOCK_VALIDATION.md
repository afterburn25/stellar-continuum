# Strategic clock and autosave scheduling parity

Gate 096 ports scalar `SimulationClock`, `CampaignAutosaveScheduler` and developer substep behavior. It does not replace the existing generic engine or tactical native clocks.

`StrategicClock` preserves the six source speed values, pause/resume memory, legacy player `advance`, bounded developer `advance_bounded_frame`, restore semantics, effective multiplier, and backlog behavior. It mirrors the .NET 8 IEEE-754 `Math.Min`/`Math.Max` NaN and signed-zero selection and leaves an invalid speed value accepted by selection until its multiplier is read. The bounded path validates finite frame and budget arguments before checking speed, clamps each request to the bounded request-plus-backlog budget, and retains only the configured backlog bound.

`advance_developer_frame` is the production `PlayableDemoScenario.AdvanceFrame` counterpart. It accepts bounded developer time once, then emits no more than four source-order substeps of at most 0.25 days.

The oracle also probes a legacy-poisoned NaN backlog before developer splitting. Native code rejects the non-finite accepted duration before conversion to an integer step count; fixture replay establishes the source exception category and resulting clock state.

`CampaignAutosaveScheduler` validates policy fields in source order, validates simulation days for every public time operation, schedules success/reset after the full interval, schedules failure after the retry delay, and saturates a due day overflow at `double` maximum. A large time jump remains one due observation until success or failure reschedules it.

The scheduler remains a scalar policy. A later frame owner must consume one pending write before another due check and schedule an asynchronous completion from the current completion day, rather than the save's captured day; this candidate deliberately does not claim that host lifecycle.

The sibling `strategic-clock-oracle-096` project references the actual `Game.csproj`; it hashes the three authoritative C# source files, including the developer scenario policy, and writes operation rows with before/after state, result, and managed exception category. `build_strict.py` regenerates that fixture, compiles Debug or Release with strict warnings, replays it, verifies source fingerprints before and after replay, and checks invalid invocation/source cases.

The maintained generator is `tests/Stellar.StrategicClock.ParityGenerator` and
the fixture is `native-tests/fixtures/strategic-clock.json`. Both independently
generated 40-row fixtures have SHA-256
`65968A5782B8B522979ABC303FDA24B3C802BC8A4BAC712EC11FB5894CC2B593`.
Strict Debug and Release pass all 40 rows; terminal failures for missing
arguments, fixture and source root return nonzero. Logs are retained in
`work/096-strategic-clock/build/{debug,release}`. Native descriptive error
wording is not claimed to match CLR internals. Nonfinite and signed-zero values
are compared losslessly; ordinary double arithmetic uses relative tolerance
1e-10. No player-session IO, calendar display or frame-rate claim is made.

Maintained integration passed `strategic_clock_parity` and the maintained
actual-source generator reproduced the fixture byte for byte
(`work/native-096-integration.log`). The replay explicitly rejects an unexpected
exception even when a successful operation would return no value.
