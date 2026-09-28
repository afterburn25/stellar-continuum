<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> Start with [the current handoff](../AGENT_HANDOFF.md) and
> [verified project state](../PROJECT_STATE.md).

# Campaign frame adapter validation

This is a reconstructed headless frame adapter, not a Godot `Main` invocation. It owns the integrated runtime before constructing the tactical service that borrows its stable Diplomacy hostility view, and it keeps one canonical campaign world.

Maintained sources are `core/include/stellar/core/campaign_frame.hpp` and `core/src/campaign_frame.cpp`. The maintained generator is `tests/Stellar.CampaignFrame.ParityGenerator`; its output reproduced `native-tests/fixtures/campaign-frame.json` byte for byte after promotion (`work/native-043-maintained-oracle.log`). Run it with output path followed by `src/Game`. CTest's `campaign_frame_parity` consumes that fixture and the existing integrated campaign fixture. The local test support adds tactical loadout/vessel codecs without changing the older integration harness.

Callers must keep runtime borrowers stable and must not move or replace the exposed runtime independently. Tactical controls ignore absent or reconciled encounters; allowed positive tactical speeds update the remembered resume speed. Strategic menu pause remains upstream source policy; this adapter owns only tactical menu admission.

Tactical frames take strategic pause ownership, sanitize real delta, use menu zero time, advance or reconcile, and consume a completion frame while restoring strategic speed. Restored unreconciled encounters begin tactically paused. Strategic Player frames always call the legacy clock and integrated runtime once, including zero; Developer frames use the shared bounded splitter and cumulative substep end days. `ready_for_save_capture` becomes true only after every strategic substep returns.

The bounded oracle uses the actual `Game.csproj` and invokes the source clock, developer scheduler, campaign massive-combat service, and integrated campaign subsystems through a small headless host adapter. It does not instantiate Godot `Main`; private notification, redraw, status-timer, and autosave UI calls remain outside this native orchestration boundary. Source fingerprints cover `IntegratedMain`, both source frame methods, both clocks, the developer scheduler, and campaign massive combat.

The row-driven strict native harness covers 12 source-host sequences plus two native ownership/failure contract cases: Player running and paused-zero frames; Developer four-quarter-day and paused frames; tactical pause ownership; restored tactical pause; menu pause/resume; completion-frame consumption and strategic speed restoration; new-battle-only admission; save readiness; cumulative end days; owner move construction and assignment; canonical runtime ownership; and failure without a completed frame. Every source row restores its supplied full campaign state and clocks, executes its supplied action and delta, then compares the complete result and resulting state with the row's `Result` and `After` values. The two contract rows are labelled separately because C# cannot establish native move semantics.

Debug and Release compile and link every current `core/src` translation unit with `/W4 /WX /permissive-`, verify source hashes before execution, guard fixture bytes during replay, and exercise missing-argument, fixture, integrated-fixture, source, and deliberately altered-result diagnostics. Two independent corrected fixture generations produced SHA-256 `83472E31179EF0A294C3ACBA839473DC10680C4DE7F0EA91B0CF53803826D094`. The correction materializes `Before` and `After` JSON at capture time so mutable worlds cannot retroactively alter earlier snapshots. Full-world replay retains local-transit coordinates separately from the source serializer's field-only vectors and uses the maintained massive-combat persistence codec for tactical loadouts and vessels.
