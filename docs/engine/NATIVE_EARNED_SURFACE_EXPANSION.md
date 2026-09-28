# Earned colony power and research expansion

This continues the genuine seed-115501 Terran Xanthe colony after its first
fabricator. The source is `work/native-audio-validation/package-earned-surface-paused.player17.json`:
player 0, system 8, body 8004, colony 9, day 6832.1009387. The original save is
never edited. Construction uses the existing native UI and authoritative Core
rules, with no money, materials, research, population or completed-building grants.

## Player-facing behavior

Surface placement and the selected-building inspector show colony-wide power
supply, demand, spare capacity and industry separately from a module's costs.
They also show active local research facilities and effective labs. These values
come from the player's actual Adaptive Research institutions with the matching
colony context and the authority's institution catalog. Colony operations uses
the same projection. Legacy surface science output is retained internally for
compatibility; it is no longer presented as research points earned per day.

Unfinished modules explain that power and workers are allocated on completion.
Actual operating shortages remain visible once construction finishes. An
unfinished-to-complete transition on the selected site replaces the stale
authorization message with the site's current status. This is local presentation
of an observed state change, not a new Core construction event. Changing colony
or campaign clears old notices; loading an already-complete building does not
announce a new completion.

## Canonical construction sequence

| Module | Authorization | Construction materials | Power | Workforce |
| --- | --- | --- | --- | --- |
| Power generator | 25 budget units / $250M UED | 300 | Supplies 4 | 20,000 |
| Science lab | 40 budget units / $400M UED | 400 | Requires 2 | 50,000 |

Placement pays only the authorization; materials are consumed as construction
advances. The existing fabricator initially uses both local power units. The
generator raises supply to six. Completing the lab leaves four units in use and
two spare, while preserving the fabricator's one industry unit per day.

The lab registers `construction:surface:9:3`, archetype
`surface_science_laboratory`, context `colony:9`, with total and active count one.
Civilization-wide effective labs rise from 12 to 13. The player's local display
correctly reports one active facility / one effective lab, rather than showing
the empire total as if it belonged to this colony.

## Maintained graphical validation

`--earned-surface-expansion-smoke <bmp>` and
`--earned-surface-expansion-paused-smoke <bmp>` require `--load --seed 115501`
and are exclusive with other graphical smoke modes. The export pipeline runs
`native_earned_surface_expansion_runtime.py` immediately after the first earned
surface-building gate, passing its actual completed save forward.

The 720p continuation enters the colony surface, selects each real palette row,
reviews placement, cancels without any Player17 change and then confirms one
exact payment. Each new site starts unfinished at zero progress. Bounded Normal
1/64-day frames advance actual material allocation and construction. The window
continues pumping input/rendering; partial and completed captures use prepared
building artwork. The native check verifies the real lab institution and a full
Player17 encode/restore round trip. A separate 1080p process reloads all three
completed modules while paused and checks unchanged state except `SavedAtUtc`.

The strict export proof binds source time/treasury, stage IDs/positions/costs,
exact elapsed steps, immediate authorization debits, saved site state and the
actual research institution. It preserves the original fabricator and entity
identities, rejects forged research/context/count evidence and verifies source
immutability. Paused output and treasury are bound to the completed save.

## Verified continuation (2026-09-16)

Generator building 2 at approximately (-35, -70) completed in 3659 exact frames
(57.171875 days). Its immediate payment was 3774.944732772 to 3749.944732772.
Science building 3 at approximately (0, -70) completed in 4876 frames
(76.1875 days); its payment was 3707.109517191 to 3667.109517191. Normal operating
costs continue between these payments. The sequence finishes at day 6965.4603137,
with treasury 3608.645986696 and all three buildings enabled, powered, staffed
and at efficiency one. Both reviews, both 10% construction states, both completed
states and the paused reload were visually inspected.

Evidence is retained under `work/native-earned-expansion-*`, including the
export JSON, captured images and saved states. Continue from
`work/native-audio-validation/package-earned-surface-expansion-paused.player17.json`.

Validation passes: final MSVC build; thirteen unique focused colony/surface,
prepared-art and Core research parity CTests; 594 Python checks (577 passed,
17 optional executable checks skipped); and the maintained two-process Vulkan
expansion/reload. A fresh affected surface check and repeat graphical continuation
also verify the completion-notice correction. Logs include `-build-final.log`,
`-ctest.log`, `-research-ctest.log`, `-feedback-ctest.log`, `-python.log`,
`-export-final.log` and `-export.json`.
The original earned fabricator flow, authored surface controls and navigation
regressions also pass (`-regressions.log`/`.json`). Five invalid expansion launches
fail with useful terminal diagnostics and exit one, without creating a capture or
overwriting the supplied save (`-cli.json`).

## Boundaries and next work

No Core rules, C# reference, Player17 schema or approved artwork changed.
The local package is unsealed and retains an older embedded version; it is not
a release download. Accelerated stepping does not prove real-time pacing or
60 FPS. Current prepared buildings are prototype oblique sprites, not finished
3D city art. Surface terrain tiling, road presentation and colony naming remain
visible polish gaps.

Next, use this earned save to verify useful colony operations and recovery:
reviewed shutdown/restart, loss/restoration of real research capacity, and a
paid timed upgrade when funds and materials permit. Keep next-action guidance
and truthful progress feedback ahead of adding unrelated systems. Do not invent
completion events, grant resources or rebalance the simulation to pass a test.
