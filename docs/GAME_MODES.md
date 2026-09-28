# Player and Developer campaigns

Stellar Continuum is being developed as one full game. The current development
build has no campaign timer, demo ending or artificial progression cutoff. Features
are still under construction; switching modes does not imply that the economy,
research cutover, late game or content catalog is finished.

## Choose a mode

The main menu has Player and Developer cards. Resume returns to the current
campaign. Open Player or Open Developer saves the current campaign before opening
the other mode's campaign. New campaign requires confirmation and a successful
checkpoint first. New Developer accepts a reproducible signed 64-bit world seed.
Humans start on Earth in Sol; other civilizations retain their species' home worlds.

Player mode uses ordinary costs, construction time, research prerequisites,
physical ships and observer knowledge. Its clock runs at 1x, 2x, 3x or 8x. Developer
commands are rejected by the campaign command boundary in this mode, even if
called without the graphical menu.

Developer mode begins with ordinary resources and knowledge. It adds a bounded
24x clock and an explicit toolbox. The toolbox pauses normal time while open and
restores the prior speed when closed. Commands affect only the Developer campaign:

| Action | Effect |
| --- | --- |
| Add test resources | Add 1,000 credits, industry and science to your civilization |
| Finish current orders | Fund and complete active research, empire project, active ship and placed surface sites |
| Survey the galaxy | Fully survey systems for your civilization's observer |
| Unlock gameplay technology | Complete the current gameplay technology and empire project catalogs |
| Advance 30 days | Run normal simulation steps, including AI, economy, construction, combat and diplomacy |

Finishing orders preserves physical ship creation and colony-ship population
transfer. Queued ships are not all completed by one action. Technology unlocks
refer to the current gameplay catalog; they do not replace the separate Adaptive
Research integration work. Selecting Developer mode does not automatically run any
of these commands.

The HUD, menu, surface header and toolbox identify the active mode. Executing a
tool marks the Developer campaign **Tools used** before mutation. The marker
persists after saving and loading; there is no in-game action to clear it.

## Separate saves

Player keeps `user://saves/autosave.json`. Developer keeps
`user://saves/developer-autosave.json`. Each has its own `.bak` recovery file.
Switching modes loads the other campaign, rather than converting a modified
Developer world into a Player campaign. If a checkpoint fails, switching is
cancelled and the current campaign remains open.

Player save formats remain 8/9, 10/11 or 12/13, according to persisted features.
Developer saves use a version-1 envelope with a required mode, tools-used marker
and the canonical validated campaign payload. Player writers reject Developer
state, and Player readers reject the Developer envelope. Corrupt primary saves
fall back to the mode's own backup. If both fail, a fresh campaign is generated
with a visible recovery message.

When neither Developer file exists, an earlier `demo-autosave.json` or its backup
can be imported into Developer mode. Those earlier campaigns used ordinary rules,
so import starts with Tools unused. The original files remain intact. Existing
Player saves retain their saved world identities.

## Next full-game priorities

1. Continue tuning the economy's completed first spending loop. Credits fund
   infrastructure, ships, settlement expeditions and surface buildings; colony and
   fleet operations create recurring costs. The Economy page reconciles the live flow.
2. Continue colony depth beyond the implemented power, upkeep, selection, cancellation,
   demolition and first upgrade tier. Add specialization and planet-condition choices
   while preserving clear costs and save compatibility.
3. Finish the Adaptive Research cutover now that campaign persistence, player actions,
   AI selection and the playable warp path are connected. Replace the remaining
   temporary legacy capability readers and connect research infrastructure to labs.
4. Strengthen AI interaction, conflict goals and campaign progression, then expand
   content and scale after profiling. Keep each milestone playable in Player mode.

The mode boundary is a development foundation, not multiplayer anti-cheat or a
claim that externally edited local saves can be trusted.
