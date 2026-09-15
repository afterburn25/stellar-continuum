# Campaign simulation benchmark integrated development validation

Before promotion, the application sources compiled with MSVC C++23 using `/W4
/WX /fp:precise` and linked against the accepted maintained campaign
coordinator. Four focused CLI test methods pass. They cover retained multi-step
state,
byte-identical repeat diagnostics, state advancement beyond the seed, positive
authoritative allocation output and nonzero economy throughput, exact hash
reuse, mode and numeric validation (including total-day overflow before asset
loading), diagnostic no-overwrite handling with byte preservation, the literal
relative output value `--seed-campaign`, and executable-relative catalog lookup
from a relocated package layout.

One 40-step, 0.25-day run was measured for each supported system count. These
are local benchmark observations, not performance guarantees or rendering/FPS
measurements.

| Systems | Initialization mean ms | Step mean ms | Step p95 ms | Step peak ms |
|---:|---:|---:|---:|---:|
| 250 | 9.6377 | 0.0867 | 0.1209 | 0.4621 |
| 500 | 20.0136 | 0.1069 | 0.1387 | 0.6122 |
| 1000 | 55.0550 | 0.2131 | 0.2399 | 0.8578 |
| 2500 | 447.8180 | 0.3572 | 0.4893 | 1.3534 |

Every run simulated 10 days and reported a final state distinct from its fresh
seed. The complete raw JSON reports are retained as standalone development
evidence at `work/041-campaign-benchmark-drafts/benchmark-results.json`. Timing
is excluded from the state projection and digest. These timings were collected
from the standalone draft executable; they are distinct from the maintained
integration tests below.

Long-run checks used 1,000 retained steps and two independently initialized
repeats at 250 and 2,500 systems. Each command simulated 500 aggregate days,
reported 12,000 allocation rows and 10 construction events, advanced beyond the
seed, and produced byte-identical final state across its repeats. The 250-system
run reported a 0.0621 ms step mean, 0.0636 ms p95, and 0.2901 ms peak. The
2,500-system run reported a 0.2681 ms step mean, 0.3146 ms p95, and 1.0271 ms
peak. Raw standalone reports are retained at
`work/041-campaign-benchmark-drafts/long-run-results.json`.

The maintained Windows validation passed 49/49 CTest targets, including the new
campaign simulation smoke, and 20/20 Python recovery/package tests, including
the retained-state and relocated-package checks. The successful transcript is
`work/native-041-campaign-benchmark-testing.log`; its SHA-256 is
`3A60AE84CA35B74A085FEF26948FC59A6663B451AE0C34E2F78666007C84A204`.
