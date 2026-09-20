<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> This is a scoped historical record; later corrections may supersede its status,
> counts, visuals, controls or limitations. It is not the current startup handoff.
> Start with [the current handoff](AGENT_HANDOFF.md) and
> [verified project state](PROJECT_STATE.md).

# Developer exploration and empire observation

Open **Developer Game.lnk**, then **New Game → Sandbox**. The independent
**Entire galaxy explored and surveyed** checkbox reveals all generated systems,
their survey details, civilizations and the generated galactic core. The older
**Full celestial coverage** checkbox adds rare celestial types for QA; it is not
the exploration option. Research completion remains separate.

For an existing isolated developer campaign, click **DEV CONTROLS** at the top
of the map, then **REVEAL ENTIRE GALAXY**. Ctrl+Shift+F12 also opens those controls
in the developer launcher. Save afterward to retain the exploration change.

**EMPIRE MONITOR** lists every civilization, including unknown aliens. Select
an empire to see its actual development stage, expansion policy, colonies,
outposts, fleets, active claims, building projects, population, credits, industry,
research spending/funding, established knowledge and active research stage
progress. The population and established-knowledge changes compare against the
state when this monitor was opened. Lists scroll; **SHOW HOME SYSTEM** focuses
the selected empire on the map. Full map visibility requires the reveal option.
The display refreshes four times per second while open; simulation follows the
current pause/speed setting. **REFRESH NOW** reads current state immediately.

Full exploration displays all existing active territorial claims, including
uncommunicated claims and subsequent claims. It does not invent ownership,
contact permissions or treaties, alter alien knowledge, or complete research.
Ordinary player campaigns retain their observer filters. Territory rendering
continues to use the shared worker projection and collision-aware labels.

The central supermassive black hole is visible after exploration, above map fog
and territory fills. Its map scale is three times the previous radius, with a
larger overview minimum and a collision-aware name. Close zoom is bounded to
keep it in view. Ordinary black holes and physical masses are unchanged.
Use **CELESTIAL INDEX**, search **Galactic center**, then **CENTER GALAXY MAP**
to locate it. Its saved physical state selects the supplied artwork. Galaxies
whose generation rules did not create a central black hole remain without one.

Core changes live in `developer_campaign.hpp/.cpp` and
`DiplomacyState::territorial_claims`: guarded exploration, bounded read-only
empire projections, and claim-only snapshots. `CampaignDeveloperProvenance`
adds optional `FullExploration` in the developer JSON envelope; old saves default
to false. Survey/core/civilization knowledge uses the existing payload codec.
No player save format or simulation rule changes.

Monitoring is a current-state observer, not a persisted historical chart or an
estimate of how close an empire is to an overall victory. Research percentages
come from the authoritative adaptive research view. This does not add new AI
development rules or change the existing galaxy size and simulation limits.

Validation: 12 focused setup, generation, save, developer, territory and label
tests pass; 23 related developer, knowledge, diplomacy, persistence and artwork
regressions also pass (these groups overlap). Real packaged Vulkan replays at
720p and 1080p exercise live reveal, alien monitoring, map navigation, saving
and central-object rendering. A separate direct-launch replay checks full
exploration without enabling complete research or celestial coverage. Ordinary
save anchors remain unchanged. UI bounds are tested from 720p through 4K;
captured setup, monitor and map screens were visually inspected at 720p/1080p.
Evidence is in `work/star-art-current/exploration-*.log` and companion captures.
See the capability registry for interfaces, save behavior and remaining limits.
