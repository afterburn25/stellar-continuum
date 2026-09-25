# Chronicle/history workstream verification — 2026-09-23

**295/295 runnable native tests green** (417 s wall, `-j8`, `preview`
build tree). Supplements — does not replace — the 2026-09-24
development-sync receipt (294/294 on `build-native/devin`).

## Scope

Branch `engine/space-strategy-simulation-specialization`, verified at
`d68c98af` (pushed). Covers the chronicle/history increments landed
since the last receipt: diplomatic journal entries recorded into the
persistent chronicle (watermarked `DiplomacyState` accessors, generic
kind summaries, authoritative `known_to_civilization_ids` audience
exempt from knowledge widening), tag-chip entity focus, chronicle →
map/diplomacy navigation, seeded-notification system/contact actions,
significance floors, actor and recency filters, header text search,
`ChronicleFilter` bundle, chronicle retention policy, and time-window
paging (`since_day` + `before_day` closed ranges — every
`HistoryQuery` axis now has a browser surface).

## Command

```bat
ctest --test-dir build-native\preview -E engine_shell_tool -j8 --timeout 300 --output-on-failure
```

310 tests are registered; 295 ran. The 15 `engine_shell_tool_*`
smoke tests (new per-tool `--frames` coverage) were excluded: this
environment has no display and even `--tool Dashboard --frames 20`
hangs at window creation. They need a desktop/CI run — verified as
registered and compiling, not executed here. Four initially unbuilt
test executables (population_habitability, combined_persistence,
campaign_economy_projection, campaign_warfare_projection) were built
and pass.

## Coverage boundaries

- `stellar-continuum-native` builds `/W4 /WX` clean.
- GPU paths exercised only through headless `native_scene3d_gpu`
  unit tests; no interactive render verification possible here.
- Local gate only; no hosted CI.
