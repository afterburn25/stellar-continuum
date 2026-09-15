# Gate 092 API: current Player17 JSON

```cpp
enum class PlayerCampaignJsonStage {
  Parse,
  Envelope,
  DiplomacyDecode,
  DiplomacyValidation,
  GalaxyDecode,
  GalaxyRestore,
  DiplomacyReferences,
  ResearchDecode,
  ResearchRestore,
  Representability,
  Encode,
};

std::string encode_player_campaign_v17_json(
    const PlayerCampaignPayloadV17Dto &payload);

RestoredPlayerCampaignV17 restore_player_campaign_v17_json(
    AdaptiveResearchStrategicRuntime research_runtime,
    std::string_view utf8_json);
```

`PlayerCampaignJsonError` retains the stage, source exception category,
source-authored outer message where the load entry authors one, optional inner
type/message, native JSON path, and absolute UTF-8 byte location. Low-level
native lexer and conversion text is identified as native diagnostic text; it
does not claim System.Text.Json line/column wording parity. Public
arguments and results contain no JSON-library types.

Restore parses the root once with the actual `JsonNode` duplicate semantics.
Duplicate root keys fail while nested typed duplicates are processed in order;
an invalid earlier value still fails even if a valid value follows, and valid
repeated values use the final value. It then interleaves work in source order:
Player envelope checks, Diplomacy decode
and structural validation, GalaxyFormatVersion16, Galaxy decode and restore,
Diplomacy world references, Adaptive Research decode and restore, and finally
Diplomacy state construction. Gate 091's shared deferred-decoder finalizer owns
campaign-reference validation, research decoding/restoration, and stable result
construction. An internal Gate 089 ordered-tree entry applies wrapper
exclusions and the format override without serializing or reparsing the world,
preserving original JSON diagnostic locations. The galaxy is restored exactly
once; later nested payloads are not decoded eagerly.

Diplomacy is decoded directly from the ordered tree through every nested
record, list, nullable, enum, and numeric width. Missing or null required
collections are reported only after all known fields have been decoded, so a
malformed later field keeps source decode-first order. Adaptive Research uses a
complete ordered schema pass over the campaign wrapper and schemas 5 through
1 before the validated last-value tree enters the maintained snapshot codec.
The pass validates every known nested record, collection element, dictionary
value, nullable, enum, Int32, Int64, and Double occurrence. Opaque dictionary
keys are preserved unchanged, including pressure metrics and every agenda
priority map. A second schema-aware pass examines only the final value of each
property after all ordered type checks finish. Source-accepted null references
that have no typed native representation fail with their original path and
absolute byte; they cannot mask a malformed later known property. This permits
source duplicate semantics without duplicating the authoritative research
restoration logic.

The scope is current Player17 JSON bytes. Historical versions, Developer
envelopes, file reads/fallback, atomic writes, progress reporting, and host
activation remain separate. Any source value absent from the sealed typed DTOs
is a named representability error rather than silently discarded.
