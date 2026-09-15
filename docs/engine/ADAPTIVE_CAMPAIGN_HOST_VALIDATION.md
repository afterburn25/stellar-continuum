# Gate 077 adaptive campaign host validation

The draft adds `--simulate-adaptive-campaign` to the existing headless campaign argument surface. It loads the strategic research runtime from the resolved asset root at `Data/research/v1`, creates a fresh integrated campaign for every repeat, advances by `stepDays` to absolute `(tick + 1) * stepDays`, and rejects any legacy research output. The existing `--simulate-campaign` body remains unchanged.

The deterministic diagnostic contains the existing complete campaign projection, schema-2 research campaign state with nested schema-5 state and funding, the full diplomacy snapshot with scheduler ticks, and every fleet-power observation. Its state hash excludes timing. The report identifies both the resolved astronomy and research paths, real phase/event counts, final projection counts, and the disabled legacy research path. It explicitly disclaims player-save, FPS, rendering, and full gameplay parity.

Output uses an exclusive C++23 `noreplace` pending file and removes only a pending file created by the current invocation on failure. Existing output and pre-existing pending files remain byte-identical.

## Completed strict checks

Both Debug and Release compiled the final Gate 075 source plus the proposed host and entry-point edits with C++23, `/W4`, `/WX`, `/permissive-`, explicit isolated `/Fo` and `/Fd`, then passed:

- help and new-mode dispatch;
- two repeats and a second invocation with byte-identical complete diagnostics;
- real initialized research-state count and all three required projections;
- explicit asset-root resolution for astronomy and research;
- mixed legacy/adaptive mode rejection;
- existing-output and pending-output preservation;
- zero/negative ticks, repeats, and system bounds;
- missing research tree and corrupt `index.json`, both exit 1 with exception type/message, absolute research path, source revision, and working directory.

Logs are retained at `work/077-adaptive-campaign-host/build-{debug,release}/{compile,run,negative}.log`.

Maintained 0.1.35 validation passed 84/84 CTest and 28/28 Python. The three retained NativeRecovery tests cover deterministic full Adaptive campaign output, real research state, asset-root resolution, missing/corrupt research, output/pending preservation and argument boundaries. The maintained `dotnet run` source generator reproduced the 15-row fixture exactly (`work/075-maintained-generator.log`). Clean committed export is the subsequent gate.
