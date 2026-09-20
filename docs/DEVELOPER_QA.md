# Native developer QA

This is the current native implementation, not a claim that the entire requested
developer console is complete. See `ENGINE_CAPABILITIES.md` for integration gaps.

## Access and isolation

Open the packaged **Developer Game** launcher, or launch the native client with
`--dev-game`, to start at the menu with **DEV MODE · ISOLATED SAVES** already on.
Choose **New Game > Sandbox** to configure and create a developer campaign.
No keyboard chord is needed for this launch path.

The existing `--devtools` launch option enables **Ctrl+Shift+F12** to toggle
developer setup or open the active developer simulation panel. That key chord
is ignored in an ordinary launch. F12 continues to take a screenshot. Developer
campaigns use separate `.dev17.json` saves; they cannot overwrite or load as
ordinary player campaigns.

Developer setup offers independent normal/special research completion and full
celestial coverage. Unchecked research starts with the normal research state.
The copied setup includes these flags. Coverage first runs normal seeded
generation, then fills missing categories without moving stars, replacing
measured anchors, replacing naturally rare stars, or adding a second central
black hole. Its forced stable IDs and coverage version survive save/load.

The simulation panel controls pause, 1/2/5/10/25x, one paused tick and AI takeover
of the existing player civilization. It opens the celestial index, which has
search, category counts, natural/forced provenance, physical values and map
centering. The central-state selector updates the existing central object.
Inspection/centering does not grant discovery or survey knowledge.
When the center is actually explored, its quiet/accreting/jet appearance follows
that saved state through the existing artwork/LOD system.

## Headless runs

Run the packaged headless executable against its own asset directory:

```powershell
.\stellar-continuum.exe --developer-qa --headless --devtools `
  --asset-root . --output D:\StellarQA\natural-001 `
  --seed 9142050 --systems 500 --civilizations 6 --ancients 1 `
  --years 100 --speed 25 --ai-control --checkpoint-days 3600
```

The output directory must not already exist. Use `--ticks N` instead of `--years`
for exact fixed-step counts. A strategic tick advances 0.25 day. Duration years
are 360 days; displayed calendar dates follow the Gregorian calendar starting
21 March 2050. A run of 100 duration years therefore advances 36,000 days.

For coverage testing add `--full-celestial-coverage`; add
`--all-normal-research` and optionally `--all-special-research` for later content.
These are independent switches, not a fully implemented all-unlocks preset.
Normal generation never forces rare objects. Accepted galaxy sizes are
250/500/1000/2500/5000/10000/25000/50000; speeds are 1/2/5/10/25. Log levels are
`errors`, `normal`, `detailed`, `trace`.

Resume an isolated checkpoint into a new output directory:

```powershell
.\stellar-continuum.exe --developer-qa --headless --devtools `
  --asset-root . --load D:\StellarQA\natural-001\latest.dev17.json `
  --output D:\StellarQA\resumed-001 --ticks 800 --speed 25
```

Do not overwrite the running executable when building another revision. Copy
the runner to a separate location first and provide an explicit asset root.

## Evidence

- `session.json`: exact executable SHA-256, commit/version, arguments, seed,
  generator version and flags.
- `initial.dev17.json`, `latest.dev17.json`, three rotating periodic slots:
  authoritative state including fixed-clock debt and AI review continuation.
  Atomic-save backups are retained. Critical failures attempt a separate save.
- `summary.json` / `summary.md`: result, invariants, canonical event counts,
  timing/memory and exact final checkpoint reload comparison. The separate
  `operationalFindings` array reports unresolved canonical return failures;
  a passing state/reload check can still contain those gameplay warnings.
- `celestial-index.json`: counts and actual object IDs, including QA provenance.
  `celestial-coverage.txt`: current generation profile diagnostics.
- `commands.json`: invocation/starting-checkpoint metadata. This is not a full
  interactive command replay implementation.
- `logs/events-*.jsonl`: typed, bounded records. Defaults retain eight 4 MiB
  segments, with explicit truncation and overwrite counts.

Phase measurements are owner-thread CPU wall times and do not affect simulation.
`core_total` includes the twelve core phases; do not sum it with its children.
Research, contacts and diplomacy follow Core. Timings, private-memory samples
and headless throughput do not establish rendered FPS.

A run passing invariants and save comparison does not prove its AI is effective
or every subsystem was exercised. Check event counts and stalled entities. The
researched soak exposed a fuel-stranded scout. Production exploration now
requires a fuel reserve to reach owned service and uses the normal return command
when no safe survey target remains. Already-exhausted saved ships retain their
actual fuel and report failed recovery; physical rescue and comprehensive stall
detection remain work.

## Diagnostic ZIP export

In an active isolated developer campaign, open **Ctrl+Shift+F12** and choose
**EXPORT DIAGNOSTIC BUNDLE**, or use F8. The report captures the current state,
then writes in the background. The existing status notice shows the result path
or a concrete error. The archive is named
`Stellar-Continuum-Diagnostic-<UTC-date>-<seed>.zip` beneath a unique directory in
the developer save folder's `support` directory. Duplicate clicks do not enqueue
more work. Ordinary player F8 exports still include only the last completed
normal save.

The developer ZIP contains a current `.dev17.json` checkpoint, session/build
metadata and exact executable hash, a short summary and file guide, structured
current invariant/operational findings, real celestial coverage, phase timing
CSV, recent bounded native session messages and renderer/window context.
`replay.json` documents checkpoint continuation; it explicitly does not claim
full command replay. Zero timing samples mean that phase has not been measured.

If checkpoint encoding fails, native export still preserves the available
findings and history. `checkpoint-error.txt` explains the failure;
`session.json` marks `checkpointIncluded: false`, and `replay.json` does not
claim that the missing checkpoint can be resumed. Use a previous healthy save.

Completed headless runs automatically write a ZIP under `exports` and print its
path. It includes the session/summary, initial/latest/three retained periodic
checkpoints, critical checkpoint when present, population diagnostics, measured
performance CSV, invocation metadata and retained typed log segments. Files are
selected by an explicit allowlist. A failed archive export is an error, never a
successful-path message. Raw session evidence remains available if export fails.

ZIP entries are bounded, checked for safe paths and validated with CRC-32;
archives are published only after completion and never overwrite earlier reports.
The ZIP itself does not certify a clean playthrough: read findings, coverage and
which checks actually ran.

## Live performance and recent events

The hidden simulation panel now has **PERFORMANCE & DIAGNOSTICS**. Live
performance lists measured phase sample counts, means and maxima; scroll for
additional phases. A phase without samples says **Unmeasured**. These are CPU
observations, not render FPS, and `core_total` includes its child phases.

**RECENT EVENTS** shows a stable newest-first snapshot. **REFRESH EVENTS**
updates it. The recording-level dropdown uses the same Errors only / Normal /
Detailed / Trace policy as structured logs. Native history retains at most 512
records and 2 MiB of serialized text; overwrite/filter totals remain visible.
Exports include the full retained text as `native-events.jsonl` and history
counters in `session.json`. History starts when that developer campaign becomes
active and resets on campaign replacement/load; it is not game state.

Current-state invariant/operational checks run at completed simulation-day
boundaries. Unchanged active warnings are not repeated every frame. This is
not yet a full AI decision journal or long-term stall detector.

The first critical finding in a native developer session automatically pauses
strategic and tactical simulation, opens diagnostics and exports an immutable
report. The banner reports success or the exact failure; the pause menu shows
the report location. The archive starts with `Critical-` and includes
`critical-trigger.jsonl`. Repeated observations do not export duplicates.
Campaign activation/load resets this first-fault latch. A prior in-flight export
may finish, but its status cannot overwrite the newly activated campaign's status.
Normal player campaigns do not auto-pause or export through this mechanism.

The captured state is not repaired. If invalid data prevents checkpoint encoding,
the report explicitly records that failure and retains available findings/history;
it does not claim that checkpoint continuation is possible. The graphical
developer smoke uses a temporary, isolated treasury fault to verify this route;
ordinary runtime observation never injects faults or changes balances.
