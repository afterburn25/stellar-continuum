# Mid-session new campaign integration review

Reviewed Devin `b9e55e79090b291b3a70545251c738c036c5c581` on 2026-09-15.
This is a source review; that commit has not been imported or executed on the
Codex integration branch. Its useful contribution is a save-gated transition
from the live campaign back to race/galaxy setup, using a distinct save slot.

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
