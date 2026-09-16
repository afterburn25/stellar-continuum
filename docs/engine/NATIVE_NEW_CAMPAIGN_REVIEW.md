# Mid-session new campaign integration review

Reviewed Devin `b9e55e79090b291b3a70545251c738c036c5c581` on 2026-09-15.
This is a source review; that commit has not been imported or executed on the
Codex integration branch. Its useful contribution is a save-gated transition
from the live campaign back to race/galaxy setup, using a distinct save slot.

## Integrated native lifecycle (2026-09-15)

The corrections below are now implemented on the Codex integration branch,
selectively adapting the New Game behavior without importing Devin's older
main loop. No C# or authoritative Core changes are needed.

`NativeCampaignSession::request_new_campaign()` requires a completed capture
boundary. `NewCampaignTransition` is Inactive/Waiting/Saving/Ready/Failed.
Waiting drains the single existing writer before admitting its own immutable
manual capture; only that Saving completion reaches Ready. Cancelling clears
transition authority, not the writer's durability work. A later request starts
at Waiting and therefore cannot accept a cancelled writer's completion.
`advance()` freezes while pending, including tactical reconciliation. The host
also blocks conflicting UI commands, showing Cancel New Game instead. Any write
failure leaves the original session open with its diagnostic and no retry loop.

`StartupEntryConfig::return_to_campaign_available` selects live setup: no boot
splash and no menu-ready callback. `StartupEntryResult` distinguishes a new
session, `exit_requested`, and `return_to_campaign`. Back/escape in setup opens
the entry screen, where Return is explicit and Escape returns to the campaign.
Generation cancellation or failure also leaves Return reachable. Exit/window
close remains a real application exit. The original NativeCampaign stays alive
through setup and cancellation; successful replacement happens after activation.
Audio/settings/window remain alive outside that loop. No global keyboard
shortcut bypasses focused text fields or modal controls.

### Evidence and reproduction

- MSVC native build; CTests `native_campaign_session`, `native_ui_layout`,
  `native_new_game_workspace`, `native_startup_session`,
  `native_startup_workspace`, `native_startup_artwork`, `native_audio_settings`,
  `native_audio_director`, `campaign_frame_parity` pass. UI layout was corrected
  after its first 640x360 gutter regression; supported 720p–4K layouts pass.
- Session tests force old and cancelled writer completion ordering, an actual
  filesystem obstruction, explicit retry, exact active-battle preservation,
  and matched continuation after cancellation. No Windows error dialog helper
  is involved; these are maintained native test executables.
- `verify_native_restart(package_folder, player17_fixture)` in
  `tools/stellar-export/native_restart_runtime.py` runs five isolated relocated
  Vulkan processes with narrow PATH and an unrelated working directory. It
  normalizes an anchor, then checks cancel/exit/create and the new slot reload.
  All outcomes preserve the original full Player17 payload except SavedAtUtc;
  new setup uses pelagic_high_pressure, seed 143250, 250 systems.
- Four validator regression tests reject missing/ambiguous outcomes, replayed
  boot/music, lost cancellation identity, and changes to previous campaign data.
- `validate_native_new_game_export(..., audio_check=True,
  audio_settings_check=True)` passes two additional cold startup/reload runs;
  its 33 Python tests also pass. These checks retain the approved cold splash,
  audio policy, settings persistence and unique-slot behavior.
- Raw evidence is in `work/native-new-campaign-*.log/.json` and
  `work/native-audio-validation/package-restart-*`. Native input events use the
  real UI hit regions; this is not physical mouse automation. Battle transition
  evidence is a canonical runtime/session test, not a battle-screen capture.

The local package is an unsealed validation artifact, not a release. Broader
visual and gameplay gaps remain in the migration handoff.

## Original review findings (addressed by the integration above)

## Required corrections and integration contracts

- At the bottom of `main.cpp`, the restart result tests only `session` and
  otherwise calls `campaign.release_session()`. It ignores `exit_requested`.
  An explicit Exit to Windows / window-close result from the startup entry
  therefore returns to the old campaign. Distinguish setup cancellation from
  application exit and honor the latter. The current `StartupEntryResult`
  needs an explicit return-to-campaign outcome if cancellation is supported.
- Save completion must identify the save requested for this transition.
  Freeze campaign time and conflicting commands while the transition is
  pending; a failure must keep the live campaign and explain recovery. Test
  cancellation and actual filesystem failure, not just successful replacement.
- Adapt the existing Codex lifecycle, not the older `main.cpp` wholesale.
  Retain audio hooks/settings, main-menu music policy, single startup splash,
  notification silence on reload, tactical manual-save boundaries, support
  export lifetime, cache invalidation and the current observer/controller APIs.
- Give the pause menu a responsive New Game action with the established
  graphical style. An unguarded `N` shortcut must not activate from research
  search, text entry, modal confirmations or other views that own keyboard input.
- New campaign creation must use a unique slot and leave the prior campaign
  recoverable. Cancellation must resume the original session without pretending
  that a fresh campaign was loaded. Canonical state, campaign identity and the
  selected race/seed require actual runtime evidence.

Devin's successful-restart smoke is a useful starting point. It does not by
itself prove cancellation, explicit exit, audio preservation or save-failure
recovery. These are the next integration gates before adapting this change.

## Follow-up review at Devin 263b4396 (2026-09-15)

The restart branch still ignores `restart_result.exit_requested`; the earlier
New Game lifecycle gates therefore remain open. The subsequent seven-commit
batch was inventoried. Civilian recovery `263b4396` is selectively adapted
with mission-bound commands and cancellation. Its boolean-only pending
confirmation was insufficient: it reset on selection/generation changes but
could survive a new mission on the same ship. The adaptation also avoids
running return-route planning in the frequent outliner refresh.

System inspection `d2a41caa` is not imported. After a full system survey and
civilization contact it reads a foreign colony's current population,
infrastructure and stability directly from `FreshCampaignState`; those
conditions alone do not establish live foreign-colony observation authority.
It also picks the first colony in a system rather than representing all known
bodies. Integrate through an observer DTO and prove stale/contact-only secrecy.

Logistics `ea9164e9` is not imported. Its broad exception-to-initializing
fallback needs an explicit error/recovery state so malformed data cannot look
like indefinite loading. Review clipping, refresh cadence and reuse of the
existing colony/logistics projections before adding another panel.

The new voice/settings/roster pipeline is inventoried, not fully audited or
imported. It introduces a SAPI synthesis/cache path and overlaps this branch's
existing bounded audio lifecycle, settings and dry recorded UK-female scientist
cues. Reconcile those contracts before integration; do not overwrite the
approved audio work or claim a specific production voice from an installed
system synthesizer.
