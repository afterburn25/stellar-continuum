# Event-driven voice handoff

Branch: `work/event-driven-voice-integration`, based on voice-engine PR #306 at `afe6f7f`. Target is `integration`. The user authorized integration on 2026-09-10. PR #307 now includes premium art PR #305 at `b88095d` as a merge ancestor, retaining both source histories. The combined menu exposes Audio, Video, and Voice & subtitles. Earth retains its previous photograph, with explicit texture availability handling. See PR #307 for the final combined acceptance and merge status.

Read [VOICE_EVENT_INTEGRATION.md](../VOICE_EVENT_INTEGRATION.md) for the architecture, exact 26-event coverage, ten expedition hooks, JSON extension pattern, role assignments, observer boundaries, settings, validation evidence and remaining semantic hooks. Reuse the existing engine; do not add another TTS queue/provider/cache.

Current speaker assignments are persisted cosmetic metadata in each civilization's Leadership roster. New or old-save founding rosters are deterministic. Replacement and vacancies survive saves; this is not a new election/mortality simulator. Playback resolves the current office after queueing. Foreign private rosters must remain unreadable to the receiving player.

Validation: Debug/Release game builds clean; Core Runtime 72/72; Simulation 69/69; voice checks 12/12 including seven distinct actual neural outputs/cache reuse. Maintained native voice capture passes 25 checks with voices enabled/disabled, real timers/travel, queued successor and species voice. Capture paths and limits are in the main document. Neural models and generated audio remain local review/setup data, not committed assets.

Important regressions prevented: detected systems announcing arrival without travel; unknown catalog names leaking through speech; observer third-party wars using own-war phrasing; distinct infrastructure milestones suppressing each other; muted pending audio returning after cancellation; current character identity disappearing when its voice falls back. Keep the focused adapter/core tests and native new-control clicks during integration.

Production casting/listening approval, portable neural pack distribution, acted emotion, spatial/formant work and missing specialized authoritative event hooks remain future work. Rare combat/diplomacy conditions are covered by actual-record adapter fixtures; do not describe them as a naturally played full campaign battle.
