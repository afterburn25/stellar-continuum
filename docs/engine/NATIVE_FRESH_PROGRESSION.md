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
