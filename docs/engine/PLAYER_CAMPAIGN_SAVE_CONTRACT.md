# Native Player17 save ownership

The C++ save controller captures the existing Player17 DTO on the simulation owner thread. The captured value owns immutable nested state. A single Engine job writes that value through the existing JSON codec and durable atomic-file writer; it never borrows the live campaign. This ports the prepared-save and scheduled-autosave behavior from `CampaignStatePersistenceService.cs` and `Main.CampaignSession.cs` without changing authoritative simulation rules.

Only an actual completed strategic frame admits scheduled capture. Tactical and unfinished frames do not. The controller retains one pending write with its path, session revision, capture day and backup policy. Success is scheduled from the day completion is consumed, not its earlier capture day. Failure preserves diagnostics and the recovered-backup flag, and schedules the established retry interval. A completion for a different session or path is failure even when the disk write finished.

A recovered session starts on the normal autosave interval with backup preservation enabled. It does not count as a failed write. The first confirmed save repairs the primary without rotating the known-good backup; subsequent successful saves resume ordinary rotation.

The host must explicitly consume and report a pending result before manual save or session replacement. `save_manual` rejects an undrained pending job so failure diagnostics cannot disappear during a manual retry. Controller destruction waits for its worker; the interactive host must drain explicitly to display any failure before exit.

## Verification

Strict MSVC Debug and Release replay passed with the actual Engine atomic writer. Tests cover detached payload ownership during live mutation, one blocked writer, explicit blocking drain, observable pending failure before manual retry, real primary/backup rotation, repair protection, Developer provenance rejection, invalid paths, frame admission, completion-day scheduling, stale session identity and owner-thread enforcement.

The actual C# Player17 service loaded and recaptured the native output identically in both configurations: 342,452 bytes, SHA-256 `F0D494456482E197D7CEA1B0537AB2AB66882C56BD0A32288CC6DA3985608309`. This is interoperability evidence. The native scheduler host tests reconstruct the source host contract; they do not invoke private Godot `Main` methods.

Run maintained checks with CTest `player_campaign_save` and the existing `player_campaign_json_parity`. The source interoperability verifier is `tests/Stellar.PlayerCampaignJson.ParityGenerator` with `--verify <native output> <research root> <Player17 fixture>`.

The controller supports Player17 UTF-8 saves. Legacy Developer recovery and the full native player UI remain separate migration work. No C# runtime is shipped by the native exporter.
