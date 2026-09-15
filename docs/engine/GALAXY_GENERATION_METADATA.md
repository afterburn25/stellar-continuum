# Gate 078 boundary

This draft ports the optional `GalaxyGenerationMetadata` record and the named
`GalacticCoreMetadata` record needed by the galaxy16 persistence boundary. It
also ports the exact validation and capture order reviewed in
`CampaignSaveService`: generation metadata, state landmark, then agreement.
The detached native capture owns copies of both records.

The two fields are appended to `FreshCampaignState` after every existing
member. Existing aggregate initializers therefore retain their field order and
default both additions to absence. `seed_fresh_campaign` and its random streams
are unchanged. This gate never invents generation metadata or reads a clock.

`CreatedAtUtc` is deliberately held as an opaque source string. Validation in
the current C# service does not inspect that value, and retaining the string
avoids an accidental timezone or precision conversion in this bounded gate.
The eventual general galaxy16 JSON codec must parse and emit the source
`DateTimeOffset` semantics, including offset and fractional precision; this DTO
alone is not evidence of that codec behavior. The source serializer also emits
the read-only, derived `SpoilerFreeSummary`; it is intentionally excluded from
the retained DTO projection here. A compatible JSON writer must derive that
property from the stored fields with the source invariant-lowercase behavior.

The named persisted `galactic_core` is authoritative for save/restore. The
existing geometric `core` remains the generation/runtime shape consumed by
stellar layout code. A future restore integration must first validate the named
record and metadata/state agreement, retain that named record unchanged, then
derive the geometric value exactly as
`{{named.x, named.y}, named.exclusion_radius}`. If both members are populated,
the integration must reject divergence rather than selecting one silently.
This gate leaves both absent during deterministic fresh generation and adds no
automatic adapter that could conceal inconsistent ownership.

No galaxy envelope decoder, historical-version conversion, encounter state,
filesystem writer, or player-save compatibility claim is included.
