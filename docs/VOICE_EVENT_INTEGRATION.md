# Event-driven voice integration

Branch: `work/event-driven-voice-integration`. Targets `integration` and builds on the existing voice-engine work in PR #306. The user-approved integration follow-up includes graphics PR #305 as a merge ancestor. PR #307 records the combined acceptance and merge status; the component evidence below retains its original source attribution.

## Runtime path

Authoritative research, construction, shipbuilding, exploration, colonization and combat results feed presentation adapters in `Main.Voice.cs` and `GameplayVoiceEventBridge`. Observers compare own fleet, hull and logistics snapshots and the player's authorized diplomatic view for important state transitions. These produce a typed `GameplayVoiceEvent`, not an audio call.

`VoiceEventRouter` checks audience, frequency, duplication and category cooldown; selects authored text and templates; then submits to the existing `VoicePlaybackController`. When a queued line starts, `CharacterVoiceResolver` resolves its current speaker. The existing `VoiceEngine`, optional offline Kokoro pack, SAPI fallback, bounded cache, Godot Voice bus and subtitles handle the result. No backend, Godot audio dependency or synthesis wait enters simulation. Voice cannot complete orders, advance time or establish contact.

## Roles and current characters

All thirteen roles are supported: Narrator, FleetCommander, ChiefScientist, Diplomat, Governor, EconomicAdvisor, OperationsOfficer, ShipComputer, ColonyComputer, ExpeditionCommander, AlienDiplomat, AlienScientist and AlienCommander.

A civilization has a current `Leadership` office roster. Character IDs, names, optional voice assignments and optional portraits persist in campaign saves. Old saves without a roster receive the deterministic founding roster; saved vacancies remain vacant. `Assign` and `Vacate` are the common handoff for appointments and future succession systems. This branch does not create a mortality, election or retirement simulation.

Playback resolves an explicit profile override or the current assigned character, then civilization-specific mappings, species mappings and a generic role mapping in `data/voice_profiles/roles.json`. Missing profiles fail safely to a valid fallback or subtitles. Character identity is retained when its timbre falls back. The player-side adapter does not inspect foreign internal office assignments; authorized incoming communications use a species translator. Pelagic, Compact and Cryogenic translators have distinct installed neural voices. Alien roles and non-Human species cannot silently acquire Human profiles.

The optional portrait uses an existing resource under `res://assets/visual/`. A portrait is not required for playback. Captions show the current character name and readable role, and wrap at 720p.

## Authored dialogue and overrides

`data/voice_profiles/events.json` retains legacy cues and adds canonical gameplay and expedition cues. Properties follow the existing camelCase JSON convention. For example:

```json
{
  "event": "research.completed",
  "profile": "human_female_chief_scientist",
  "speakerRole": "ChiefScientist",
  "priority": 40,
  "frequency": "Normal",
  "cooldownSeconds": 2,
  "category": "research",
  "lines": ["Research complete. {research_name} is now available."]
}
```

The legacy `profile` field remains for old callers; typed gameplay requests resolve the role at playback. To add a routine announcement, add its cue and have a presentation adapter publish the real event with source civilization/species, stable event identity, simulation tick/date, and authorized template variables.

Templates accept named lowercase placeholders such as `{research_name}`, `{project_name}`, `{ship_name}`, `{ship_class}`, `{colony_name}`, `{planet_name}`, `{system_name}`, `{fleet_name}`, `{species_name}`, `{civilization_name}`, `{enemy_name}`, `{resource_name}` and `{amount}`. Substitution is bounded plain text. Missing values suppress that voice line safely; normal gameplay notifications remain. No script, markup or code is evaluated.

`VoiceEventOverrides` supports role, exact character/profile, dialogue key, exact authored line, priority, emotion metadata, communications processing (including an explicit bypass), interruptibility, queue policy, category and cooldown. An unregistered cinematic key requires an exact line and explicit role. The configured backend must support a capability before it can perform it: emotion metadata does not promise acted emotion.

Variants are deterministic for event identity, tick and date. First-time text requires an explicit first-occurrence flag; loading a router does not itself make a typed event “first.” Gameplay adapters establish history baselines when a campaign loads and suppress re-announcing existing transitions. The bounded audio queue and recent-event history are not saved.

## Gameplay coverage

| Canonical event | Actual source | Speaker |
| --- | --- | --- |
| research.completed | Technology completion or newly established adaptive research | Chief Scientist |
| research.breakthrough.major | FTL-category completion or established adaptive FTL capability | Chief Scientist |
| construction.completed | Completed construction result | Operations Officer |
| construction.orbital_launch_complex.completed | Completed launch-complex project | Operations Officer |
| construction.orbital_shipyard.completed | Completed orbital shipyard project | Fleet Commander |
| ship.completed | Completed shipbuilding result, actual vessel and design | Fleet Commander |
| ship.launched | Later own-fleet interstellar departure transition | Fleet Commander |
| ship.interstellar.first_launch | First own-fleet interstellar departure | Fleet Commander |
| exploration.system.reached | Actual own-fleet arrival | Chief Scientist |
| exploration.survey.completed | Completed system survey result | Chief Scientist |
| exploration.anomaly.discovered | Observed anomaly signature or completed anomaly survey | Chief Scientist |
| colony.founded | Authoritative founded colony and body | Governor |
| contact.unknown.detected | Observed unidentified activity | Ship Computer |
| contact.first | Established first-contact result | Diplomat |
| diplomacy.alien.transmission | Incoming proposal in the player's authorized diplomatic view | Alien Diplomat |
| diplomacy.war.declared | Own-participant war declaration | Diplomat |
| combat.fleet.attacked | Own fleet's engagement-start result | Fleet Commander |
| combat.hull.critical | Own hull crosses into 25% or lower integrity | Ship Computer |
| logistics.critical | Own colony support changes to critical | Operations Officer |
| economy.treasury.critical | Depleted/arrears treasury or deficit with at most 30 days runway | Economic Advisor |

Survey signatures and arrival reports must use the player's known names. Sensor detection alone is not arrival. Hidden AI research, construction, officers and diplomacy are not eligible. Foreign events require an explicitly authorized direct recipient or observable evidence; observer-only evidence requires observer mode.

Six further canonical routes bring the gameplay vocabulary to 26: `combat.fleet.retreat_initiated`, `combat.fleet.destroyed`, `combat.engagement.concluded`, `diplomacy.proposal.rejected`, `diplomacy.agreement.activated` and `diplomacy.border_warning.issued`. These use real own-fleet results or own-participant diplomatic history. Engagement conclusion is neutral because the event does not declare a winner. Incoming offers speak the existing authorized proposal summary, not invented alien dialogue or private AI intent.

Colony vessels use the normal ship-ready and arrival routes; founded colonies have the Governor report, and critical supply uses logistics. Separate settlement-authorization, environmental-emergency, unusual-planet and explicit victory/defeat announcements need suitable authoritative events before receiving specialized dialogue. Agreement activation includes the existing peace agreement flow; there is no fabricated peace event.

Expedition support is ten clean registry hooks: reactor problem, navigation correction, resource shortage, crew issue, unknown signal, rogue planet, intergalactic object, ship damage, major scientific discovery and destination approach. No expedition simulation is added. The registry contains 51 cues: 26 gameplay, ten expedition, and fifteen compatible legacy cues.

## Frequency, queue and recovery

Minimal retains critical alerts, major research/discoveries, major construction, first contact and cinematics. Normal adds research, ship, exploration and colony milestones. Frequent includes routine operational cues. Settings preserve compatibility with the previous chatter-level field.

Cooldowns are per source civilization/category; exact event IDs and repeated text are bounded and deduplicated. The playback queue holds at most eight lines, at most two per category. Repeated work can replace pending category announcements; it does not create an invented aggregate count. Critical cues may interrupt lower-priority, interruptible dialogue, subject to the user's no-interruptions preference. Ordinary reports do not interrupt emergencies.

Every accepted and presented event uses the same subtitle path, including muted/unavailable/failed synthesis. User subtitle settings are respected. Filtered, duplicate or stale announcements do not suppress the underlying game's notifications. Muting during synthesis cancels pending audio and retains captions. Campaign reset stops speech and closes the Voice Lab.

The existing cache keys text, resolved profile, backend/model/version, selected voice, synthesis settings and processing version. Actual leadership replacement changes the profile before cache lookup. The local Kokoro pack is optional setup data, not a bundled portable model; see [VOICE_ENGINE.md](VOICE_ENGINE.md).

## Developer testing

Open campaign settings → Voice & subtitles → Developer Voice Lab. The event picker reads the dialogue registry, so new canonical cues appear automatically. “Present sample event” sends fixed labeled sample values through the real router, resolver and engine; it does not complete research, build ships, contact AI or change diplomacy.

The office picker can appoint a named current character using the selected voice in a Developer campaign. This assignment persists with the campaign. Player mode cannot use it. Event diagnostics show identity, civilization, role, resolved character/profile, dialogue key and text; playback diagnostics distinguish synthesis/cache/prerecorded/subtitles. These details remain in the Developer Lab and support log, not ordinary player notifications.

## Validation

Validated on Windows on 2026-09-10:

- Game Debug and Release builds: zero warnings/errors. Core Runtime: 72/72. Simulation: 69/69. Existing nullable warnings in the Core Runtime test project remain unrelated to this change.
- `work/event-final-voice-core.log`: all twelve voice-core groups, including seven actual neural PCM/cache pairs and SAPI male/female synthesis. `work/event-final-routing-checks.log`: all twelve groups after the distinct major-infrastructure cooldown regression was added.
- `work/event-voice-final`: native game exited 0, empty stderr, 25 checks, seven screenshots and three non-silent Voice-bus recordings. It exercised timed research/infrastructure/shipbuilding and right-click travel, then queued-character replacement, disabled event voice, and a fixed Developer Pelagic transmission through the live engine. Thirteen profiles loaded; nine audio lines and fifteen subtitle presentations were observed. No neural worker remained after shutdown.
- `work/event-voice-reviewed`: repeated that flow with actual mouse activation of the new office-assignment and event-tester buttons, scrolling the active caption into view for review. Exit 0, empty stderr, 25 checks, seven screenshots; successor and alien captions were visually reviewed at 720p. A final capture follows the treasury-observer correction noted below.

The first candidate capture, `work/event-voice-native`, failed cleanly when two distinct major infrastructure completions shared a cooldown. The final registry separates launch-complex and shipyard categories; the test now also waits for real commander playback after shipyard completion. No assertion was suppressed. Sensor detection incorrectly counting as arrival and third-party wars sounding like the player's own war were fixed and covered before the successful native run.

Final review also moved critical treasury assessment into the regular own-state observer, so the at-most-30-days runway warning can occur before operations become unfunded. Category cooldowns still bound repeated warnings.

`work/event-voice-release` validates that final source candidate: exit 0, empty stderr, all 25 checks, seven screenshots, three live Voice-bus recordings, and no remaining neural worker after shutdown. Debug and Release game builds both pass with zero warnings/errors. The earlier `reviewed` capture establishes the visible control/caption layout; the final capture repeats the same mouse-driven checks after the treasury hook adjustment. These are pre-commit source-candidate runs, not a claim of testing a subsequently merged build.

- Core voice checks cover audience isolation, all roles, current-character replacement, missing-profile fallbacks, safe/missing templates, exact dialogue, frequency, first-time selection, cooldowns, deduplication and existing engine/cache/error behavior.
- Core Runtime checks cover real event adapters and roster persistence, including old saves, vacancies and deterministic seeding.
- The maintained native `ScreenshotCapture` voice focus exercises real research, timed infrastructure and shipbuilding, right-click travel, disabled voices, multiple profiles, cache/replay/reset, queued successor resolution and a species-specific incoming transmission sample.
- Rare event adapter fixtures and Developer samples are distinguished from naturally triggered native gameplay in the evidence. Full battles and first contact are not claimed as a native campaign playthrough.
