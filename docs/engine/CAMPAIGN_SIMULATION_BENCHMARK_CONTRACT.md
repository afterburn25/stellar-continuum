# Campaign simulation benchmark CLI contract

`stellar-continuum --headless --simulate-campaign` is a distinct diagnostic and
benchmark mode. It accepts the fresh-campaign seed options (`--systems`,
`--seed`, `--civilizations`, `--ancients`, `--player-species`, `--asset-root`,
and `--repeat`) plus `--ticks` and `--step-days`. The defaults are 40 steps of
0.25 simulation day. Supported galaxy sizes remain 250, 500, 1000, and 2500;
simulation steps are bounded to 1..10000 and simulation repeats to 1..10.
`--step-days` must be finite and positive.

Each repeat creates one fresh campaign, one owning `CampaignSimulationState`,
and one default `GalaxySimulationStepCoordinator`. That coordinator and state
survive for every step in the repeat. A later repeat starts from a newly seeded
campaign and new runtime instances. Initialization timing ends before the first
`Advance`; each step timing contains only the actual coordinator call.

The JSON report identifies `legacy-campaign-simulation-benchmark`, reports
initialization separately from step mean, p95, and peak milliseconds, and
reports total simulated days plus output-record counts for the allocation list
and each of the six event lists. The p95 is the nearest-rank sample at
`ceil(0.95 * sample count)`. It sets `gameplayParity` and
`playerSaveCompatible` false and describes the scope
as the ordered legacy campaign coordinator phases. It makes no rendering/FPS,
Adaptive Research, diplomacy, full gameplay, or player-save claim.

After every repeat, the complete campaign-owned diagnostic projection is
serialized with timing excluded. Repeated final serializations must match byte
for byte. The report exposes a deterministic FNV-1a hash of that serialization
and reports whether every repeat changed beyond its freshly seeded state. An
optional `--catalog-output` writes the final diagnostic projection with step
metadata and the same hash. Existing pending-file and no-overwrite behavior is
preserved; this file is diagnostic data rather than save-v16.

Campaign, seed, and catalog-preview modes are mutually exclusive. Simulation
only options are rejected outside `--simulate-campaign`. The top-level option
scan continues to skip option values, so a path value beginning with `--` is
never interpreted as a mode. Asset lookup defaults to the executable directory
and honors `--asset-root`, preserving relocated-package behavior.
