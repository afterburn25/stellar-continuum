# Validation receipt — late-game scale + deterministic save/continue (2026-09-28)

Scope: grand-strategy platform maturity, milestone 3 (scale + save
continuation). Branch `engine/grand-strategy-platform-maturity`, commit
`2af8d120` + doc follow-up.

## Command

```text
stellar-continuum.exe --simulate-adaptive-campaign --systems 5000
  --civilizations 22 --ancients 3 --ticks 7300 --step-days 5
  --repeat 2 --stress-fleets 50
  --events-root <repo>/data/events --verify-continuation-tick 3650
```

5,000 systems · 25 civilizations · 36,500 simulated days (100 years) ·
automation enabled · 4 data-authored event chains loaded.

## Results

| Metric | Value |
|---|---|
| Repeat determinism | `repeatFinalStatesDeterministic: true` (2 independent runs identical) |
| Save→restore→continue | `continuationDeterministic: true` — dev save at tick 3650 (year 50), second runtime restored + advanced to tick 7300, canonicalized save documents byte-identical |
| Final state hash | `25eac6e30fce280f6baad9c992958cc1f3fb3a0eb4def979fd40bf80081c9f66` |
| Initialization | 4,067 ms total / 2,033 ms mean per repeat |
| Step timing | 953.5 s total, mean 65.31 ms, p95 82.51 ms, peak 664.8 ms |
| Memory | working set 518.5 MB; peak 2,254.8 MB (includes verification JSON trees) |
| Save size | 145,102,997 bytes (developer envelope incl. runtime continuation) |
| Civilizations / colonies / fleets / wars | 25 / 27 / 150 (stress-injected on warp-capable ancients) / 0 |
| Economy totals | credits 2,604,204; industry 409,020 |
| Research / construction / industry events | 36,530 / 186 / 321,200 |
| Exploration / diplomacy / colonization / combat | 30 / 18 / 0 / 0 |
| Scripted event definitions / journal entries | 4 / 128 |

## Supporting checks

- `ctest -R "automation|scripted|player_campaign|campaign_coordinator|
  campaign_command|foundation|economy_persist|fresh_campaign|persistable"`:
  14/14 pass, including all C# parity fixtures.
- 5,000-system sanity run (200 ticks, verify at 100): deterministic.
- Save-fidelity bisect probe (uncommitted tooling): save→load capture
  byte-identical after canonicalization; post-restore continuation
  identical for 30 ticks once advisory proposal state persisted.

## Fixes proven by this run

- `AutomationController::State::last_proposals` — advisory top-pick
  suppression round-trips; restored controllers no longer re-journal.
- Scripted-event snapshots embed definition documents — save restores
  its trigger contract without an external data root.

## Honest limitations observed

- Wars: 0 — organic AI war-making does not emerge at this horizon;
  fleets exist only via `--stress-fleets` injection.
- Colonization: 0 events — civilizations do not expand organically.
- Verification peak memory includes the canonicalization parse trees;
  steady-state simulation memory is ~0.5 GB.
