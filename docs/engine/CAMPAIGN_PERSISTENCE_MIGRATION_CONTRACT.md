# Campaign persistence migration contract

Reviewed on 2026-09-13: `CampaignStatePersistenceService.cs` (425 lines) and `DeveloperCampaignPersistenceService.cs` (144 lines), with the format declarations in `CampaignSaveService.cs`. The latter's full 2045-line galaxy conversion/validation remains a separate review gate.

## Version boundaries

The current PLAYER campaign format is **17**, wrapping the authoritative **galaxy format16**, Diplomacy snapshot, and Adaptive Research campaign schema2 (which nests standalone research schema5). Earlier coordination shorthand "save-v16" referred to the galaxy payload and must not be interpreted as the complete player save. Native foundation checkpoints and standalone research snapshots do not establish player campaign compatibility.

Galaxy-only formats1..8,10,12,16 restore through the galaxy service, then create fresh research state and an EMPTY DiplomacyState. Never infer contacts, treaties, wars, claims or trust from omniscient galaxy state. Campaign wrappers9/11/13 normalize galaxy versions8/10/12 respectively. Wrapper15 requires explicit inner version8/10/12; wrapper17 requires exactly16. Both15/17 require authoritative AdaptiveResearch payload; older wrappers create research profiles. All wrappers require strict Diplomacy snapshot validation and galaxy-reference checks.

## Capture and atomic write

PrepareSave validates galaxy/diplomacy/research presence, exact Player-versus-Developer provenance, then finite nonnegative simulation time. It captures and validates Diplomacy and its galaxy references BEFORE detached galaxy capture. Require inner format16. Research capture uses the passed research campaign's own runtime association. Build detached envelope format17 with GalaxyFormatVersion16, Diplomacy and AdaptiveResearch. Prepared payload must be immutable with respect to subsequent simulation mutation; capture and JSON/write durations are diagnostics, not simulation state.

WritePrepared validates nonblank path, nonnull prepared state and exact Player/Developer payload kind before serializing. Existing source writes indented, case-sensitive JSON. Atomic writer creates exclusively owned sibling GUID .tmp, writes UTF8 without BOM, flushes writer then file to disk, and either replaces the old primary (rotating .bak) or moves for a new file. Repair save after recovering a known-good backup preserves that backup once; ordinary saves resume rotation. Cleanup may remove only the temp file created by that operation. Preserve the prior primary/backup on write failure. Native implementation must provide the corresponding filesystem guarantees and failure evidence before claiming compatibility.

## Player restore order

Report progress .04 read, .16 decode. Reject DeveloperFormatVersion at Player boundary; require FormatVersion and supported1..17. Legacy galaxy-only branch reports .35 galaxy, .82 profile reconstruction, .97 validation. Wrapper branch reports .25 diplomacy; require/decode snapshot, wrapping JSON and Diplomacy invariant errors with original inner exception. Then .42 galaxy, validate historical inner version, restore normalized galaxy, .72 cross-reference validation, .82 research restore/profile generation, .97 finish. Return Galaxy, simulation day, game version, save timestamp, restored Diplomacy and research. Progress fractions remain finite0<=fraction<1 and nonblank status. Load errors must preserve original exception detail and never report completion.

Research wrapper Restore catches JSON decode errors only; semantic research failures retain their actual category. Underlying galaxy payload conversion still owns species, bodies, fleet/combat and economy invariant validation. Do not replace these checks with parse success or fabricate missing state.

## Developer envelope

Developer format1 requires exactly DeveloperFormatVersion, Mode, ToolsUsed and Campaign, with no duplicates/extras. Mode must equal Developer exactly and ToolsUsed must be a Boolean. Inner Campaign must be one of9/11/13/15/17. Recursively reject nested DeveloperSession, DeveloperFormatVersion, Mode or ToolsUsed anywhere inside the canonical campaign. Restore normal campaign first, then attach DeveloperSessionState with the preserved ToolsUsed flag. Never allow a Developer prepared payload or file to pass the Player save path.

Developer load progress .03 read, .14 validated, nested player progress mapped to .14+fraction*.83, final .98. Existing source creates an exclusively owned staging file and deletes only after ownership is established. Native codec may consume the same detached JSON through a shared internal decoder, but must preserve validation order, mode separation, returned state and diagnostics; avoid adding a second permissive decoder.

## Required evidence before integration

Use actual-source current and historical fixtures with complete restored state and a subsequent deterministic campaign advance. Exercise truncated JSON, unknown/missing/duplicate identities, inconsistent reference graphs, wrong wrapper/inner versions, research funding and outcome continuation, Developer-to-Player rejection, interrupted writes and known-good backup recovery. Prove stable runtime/support ownership throughout restoration. Performance or native FPS claims require later actual player/renderer testing; headless timing and clean package relocation are insufficient.
