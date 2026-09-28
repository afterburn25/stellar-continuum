<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Developer inspection and planetary classes — 18 September 2026

Developer campaigns can inspect actual alien settlements and active fleets.
The colony roster includes all empires; opening a planet shows its owner's live
population, species, infrastructure, stability, sustenance, employment, power,
treasury, output, local research facilities, buildings and construction progress.
Empty worlds explicitly show that there is no colony population or construction.
Empire Monitor continues to expose actual empire totals and research progress.

Inspection requires the existing developer campaign provenance and valid player
identity. It is independent of the full-exploration checkbox. Map revelation is
still a separate developer action. The shared Core observation policy does not
rewrite knowledge, contacts, ownership or AI behavior. Fleet and surface commands
retain their owned-object validation. Foreign inspection permits navigation,
not issuing another empire's construction or travel orders.

Planets now show physical type followed by a separate environmental class,
including Barren, Continental, Ocean, Desert, Frozen and Greenhouse. Both the
orbital inspector and planetary screen consume the same Core classification.
Unknown ordinary-game worlds do not expose their class. Classification derives
from saved physical/environmental properties, with canonical Sol giant identities.
Continental means temperate water-bearing land conditions; coastline percentages
and detailed biomes are not modeled. These labels do not alter habitability,
terraforming, climate, or generation. Existing saves need no conversion.

Validation: native build and focused regression checks; developer read projections
are checked against unchanged serialized campaign state, ordinary-game secrecy,
access revocation, actual alien ownership/economy, live refresh, command denial,
and planet class boundaries. Maintained developer graphical replay additionally
opens an alien colony, inspects its economy, and verifies Earth is Continental.

Final validation passed: the native build, 15 focused tests and 20 additional
consumer regressions, including fleet presentation, colony navigation, settlement
commands, economy/logistics and ordinary knowledge/combat authorization.
Packaged Vulkan replays passed at 1280x720 and 1920x1080. Inspected captures show
Aion's actual Dravak Compact population and economy, and Earth as Rocky world /
Continental. Both runs verified that opening those views did not change the
serialized developer campaign. The ordinary save anchors remained unchanged.
These are 250-system early-campaign functional checks, not late-game scale results.

The runnable package is
`C:/Steller Continuum/StellarContinuum-DevInspection-Fix-20260918`.
`Developer Game.lnk` targets this package's native executable with `--dev-game`;
it does not target an older installation. Both packaged executables match the
tested build, and all shipped files are covered by the validated build manifest.
Replay evidence: `work/star-art-current/inspection-final720.log`,
`inspection-final1080.log`, and the matching `-alien-colony.bmp`,
`-alien-economy.bmp` and `-earth-classification.bmp` captures.

Remaining scope: geographically allocated population/deposits, planetary military
layers, weather/climate simulation and terraforming are not implemented. No
statistics have been invented for those systems. The 50,000-system ceiling,
long-game performance work, whole-world capture/input memory costs, physical
central-black-hole integration and persistent empire history remain separate work.
