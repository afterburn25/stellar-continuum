# Massive combat performance benchmarks

This benchmark exercises the plain deterministic combat engine without Godot or a renderer. It measures 25 cases: five fleet sizes and five tactical/loadout patterns. Every case advances 40 fixed 0.1-second ticks after a separate JIT warm-up. The formations are deliberately concentrated into at most four occupied spatial cells, which produces a dense targeting workload.

Recorded on 2026-09-11 using a 24-logical-processor `JOHN_DESKTOP`, Windows 10.0.26200, .NET 8.0.31, Release build. Allocation is total managed allocation during all 40 measured ticks; it is not process working set. The full machine-readable result is in [`massive-combat-benchmark.json`](../tests/Game.MassiveCombat.Validation/Artifacts/massive-combat-benchmark.json).

| Ships / side | Scenario | Formations | Init ms | Avg tick ms | p95 ms | Worst ms | Alloc MiB | Candidates / tick | Weapons / tick | Cells | GC 0/1/2 |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 10 | beam | 2 | 0.054 | 0.040 | 0.046 | 0.063 | 0.62 | 8 | 2 | 2 | 0/0/0 |
| 10 | missile | 2 | 0.026 | 0.042 | 0.046 | 0.221 | 0.67 | 8 | 2 | 2 | 0/0/0 |
| 10 | mixed | 2 | 0.027 | 0.054 | 0.071 | 0.072 | 0.72 | 8 | 6 | 2 | 0/0/0 |
| 10 | interdictor | 2 | 0.116 | 0.083 | 0.094 | 1.553 | 0.70 | 8 | 2 | 2 | 0/0/0 |
| 10 | breakout | 2 | 0.030 | 0.062 | 0.085 | 0.102 | 0.80 | 8 | 6 | 2 | 0/0/0 |
| 100 | beam | 2 | 0.025 | 0.035 | 0.041 | 0.042 | 0.62 | 8 | 2 | 2 | 0/0/0 |
| 100 | missile | 2 | 0.021 | 0.036 | 0.043 | 0.044 | 0.67 | 8 | 2 | 2 | 0/0/0 |
| 100 | mixed | 2 | 0.021 | 0.066 | 0.081 | 0.087 | 0.82 | 8 | 6 | 2 | 0/0/0 |
| 100 | interdictor | 2 | 0.023 | 0.040 | 0.049 | 0.051 | 0.70 | 8 | 2 | 2 | 0/0/0 |
| 100 | breakout | 2 | 0.024 | 0.074 | 0.094 | 0.110 | 0.90 | 8 | 6 | 2 | 0/0/0 |
| 1,000 | beam | 2 | 0.027 | 0.035 | 0.041 | 0.046 | 0.62 | 8 | 2 | 2 | 0/0/0 |
| 1,000 | missile | 2 | 0.020 | 0.036 | 0.043 | 0.044 | 0.67 | 8 | 2 | 2 | 0/0/0 |
| 1,000 | mixed | 2 | 0.024 | 0.067 | 0.082 | 0.124 | 0.82 | 8 | 6 | 2 | 0/0/0 |
| 1,000 | interdictor | 2 | 0.035 | 0.042 | 0.051 | 0.053 | 0.70 | 8 | 2 | 2 | 0/0/0 |
| 1,000 | breakout | 2 | 0.025 | 0.071 | 0.086 | 0.088 | 0.90 | 8 | 6 | 2 | 0/0/0 |
| 10,000 | beam | 20 | 0.062 | 0.508 | 0.546 | 1.234 | 9.02 | 800 | 20 | 4 | 0/0/0 |
| 10,000 | missile | 20 | 0.085 | 0.484 | 0.640 | 0.697 | 9.55 | 800 | 20 | 4 | 0/0/0 |
| 10,000 | mixed | 20 | 0.085 | 0.658 | 0.737 | 0.805 | 10.71 | 800 | 60 | 4 | 0/0/0 |
| 10,000 | interdictor | 20 | 0.090 | 0.527 | 0.647 | 0.783 | 11.24 | 795 | 20 | 4 | 0/0/0 |
| 10,000 | breakout | 20 | 0.094 | 0.749 | 0.852 | 0.914 | 13.00 | 800 | 60 | 4 | 0/0/0 |
| 50,000 | beam | 100 | 0.246 | 3.674 | 4.300 | 4.636 | 121.38 | 20,000 | 100 | 4 | 8/1/0 |
| 50,000 | missile | 100 | 0.273 | 4.332 | 4.874 | 5.375 | 122.99 | 20,000 | 100 | 4 | 8/0/0 |
| 50,000 | mixed | 100 | 0.283 | 5.945 | 7.031 | 7.198 | 127.32 | 20,000 | 300 | 4 | 8/1/0 |
| 50,000 | interdictor | 100 | 0.335 | 6.270 | 6.896 | 9.037 | 164.58 | 19,875 | 99 | 4 | 11/0/0 |
| 50,000 | breakout | 100 | 0.371 | 6.519 | 7.415 | 7.612 | 171.80 | 20,000 | 300 | 4 | 11/1/0 |

All cases retained the requested exact initial ship count. The 100,000-ship cases stayed below 7.5 ms at p95 and below 9.1 ms at the worst measured tick, inside the engine's 100 ms fixed-tick budget on this hardware. The benchmark makes no GPU, rendering, frame-rate, or other-hardware claim.

Run the validation and benchmark with:

```text
dotnet run --project tests/Game.MassiveCombat.Validation/Game.MassiveCombat.Validation.csproj -c Release
dotnet run --project tests/Game.MassiveCombat.Validation/Game.MassiveCombat.Validation.csproj -c Release -- --benchmark tests/Game.MassiveCombat.Validation/Artifacts/massive-combat-benchmark.json
```
