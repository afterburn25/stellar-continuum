# Ordinary fresh campaign progression

The maintained C++ regression creates ordinary Terran 500-system campaigns and
uses the public research, construction, shipyard and fleet controllers. It does
not edit snapshots, grant funds or capabilities, or inject ships. Every timed
change advances through CampaignFrame.

The Developer frame policy batches at most one game day into four canonical
0.25-day substeps. This accelerates test execution without changing the campaign
rules. The test is not evidence of real-time player pacing or a 60-FPS result.

## Successful path

Seed **115501** completes the following path:

1. Authorize the Orbital Launch Complex at the fresh start.
2. Mature in-space assembly, asteroid prospecting, asteroid mining, vacuum
   refining, orbital manufacturing and orbital shipyard research.
3. Build the physical Orbital Shipyard once its canonical prerequisites allow it.
4. Mature gravitational physics, field theory, warp metric theory, exotic energy
   coupling, micro-field distortion and warp field control.
5. Build the Warp Test Facility. Its completed project provides the specialist
   capabilities required by prototype-drive research. Construction and the
   subsequent capability synchronization complete by simulation day **5580**.
6. Reach Demonstrated prototype warp drive on day **6499** and authorize a scout
   and science vessel. Authorization creates no immediate fleet.
7. Finish both ships by day **6574**, select the scout and issue a canonical
   reachable route to system 1. A later CampaignFrame step advances its transit
   progress from **0.000000 to 0.935860** on day **6575**.

The physical yard remains completed, both ship designs are unlocked, and two
owned fleets exist at the final boundary. The treasury remains positive at
approximately 4126.88 internal budget units, displayed in sovereign currency by
the client. Research and building costs were paid through the normal commands.

The earlier audit omitted the Warp Test Facility action and therefore observed
a specialist-facility rejection. That was a missing step in the test sequence,
not a missing game rule. The corrected test covers the existing construction-to-
research capability handoff explicitly.

## Preserved research dead end

Seed **115500** archives `warp_metric_theory` on day **3642**. It has completed
the physical yard but still has zero fleets and locked interstellar ship designs.
The treasury is positive at approximately 2689.62 internal budget units.

The current catalog requires that hypothesis for the only prototype-drive chain.
A disproof can therefore prevent access to the current interstellar ship designs.
This is a canonical content/pacing limitation, not a migration bypass to fix by
granting capabilities. No alternative path or research rule has been added.
It remains a release-readiness issue requiring a separate gameplay decision.

## Evidence and reproducibility

Strict Debug and Release each repeated both seeds and compared the full milestone
trace, elapsed days, treasury, research outcome, physical construction, fleet
counts and design locks. Both configurations passed. The regression verifies
actual later transit progress, rather than treating an assigned destination as
movement. Each campaign has a 10,958-day cap.

The checked Debug execution took about 171 seconds locally and Release about
15 seconds. The maintained CTest timeout is 600 seconds to cover slower CI hosts.
An unexpected outcome fails in the terminal with a useful message. The test
source is `native-tests/native_fresh_progression_tests.cpp`; the CMake entry is
`native_fresh_progression`.

This proves a successful ordinary campaign path and a distinct failed-hypothesis
path. It does not certify every seed, race, research strategy or galaxy size.

## Developed-fleet profile save

The opt-in `--profile-save <absolute-path>` run creates a fresh campaign using
the same maintained recipe and only ordinary paid research, construction and shipyard
commands. Seed 115501 reaches day 10154 with 500 systems, 9 total colonies
(3 owned), treasury 346.233 and 24 ships (12 scouts and 12 science vessels). Developer
offline stepping is explicit; no funds, capabilities or ships are granted.
The 24-ship bound keeps the scenario within its real budget: an initial 32-ship
attempt exhausted funds after 25 ships. All 24 accepted routes must enter actual
transit before export. The output requires an absolute, nonexistent path with an
existing parent, and is checked again before writing.

After building the `stellar_native_fresh_progression_tests` target, run from
the repository root (create `work` first if absent):

```powershell
& ./build-native/preview/stellar_native_fresh_progression_tests.exe `
  "$PWD/data/research/v1" "$PWD/data/astronomy/hyg-nearby-500-v1.json" `
  --profile-save "$PWD/work/developed-fleet-24-fixed.player17.json"
```

Use a new output name for another run. This is a maintained C++ test executable,
not an unmanaged scratch checker. Failures report a cause and return nonzero.
The generated save is local validation tooling, not a committed game asset.

Every default positive/negative progression now restores and recaptures its full
Player17 snapshot; profile export does this before writing. Values and array
order must match, with no fields excluded. Object member order is immaterial:
the Leadership dictionary sorts on restoration. Research errors include the
decoder path and inner exception details in the terminal.

This test caught a native save writer defect: populated outcome history used
`"hypothesisSupported"` where the strict Player17 reader requires an integer.
The writer now converts the typed outcome, tacit scope/stage and four foreign
assessment enums at their schema paths. The strict reader and standalone
research snapshot formats stay unchanged. Eight focused CTests and 17 Python
profile checks pass; existing canonical JSON fixtures are unchanged.

Four active/reload Vulkan runs at 720p and 1080p passed exact paused payload
recapture. Over 600 frames at 8× speed, days advanced 10154→10234.2218632 and
→10314.4471072; interval means were 16.713/16.714 ms, p95 17.401/17.604 and
p99 18.516/18.560. All 24 ships moved in both intervals; they were still
transiting after the first interval and arrived/stationed by the second. Evidence
is `work/developed-fleet-24-profile.json` and
`native-developed-fleet-fixed-validation.log`; source hash remains
`b5a57777a4eacc57458ee92c5ebc74cc77aaea5409732e86d665e7151915a17f`.
