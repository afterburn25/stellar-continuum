# Galaxy16 JSON codec boundary

This gate converts UTF-8 JSON to and from the complete owned Galaxy16 DTO from
Gate 087. Public types remain standard C++ values. The decoder retains object
member order and duplicates, so an invalid earlier duplicate fails before a
later value can replace it. A source-accepted value that the DTO cannot retain
fails as `Representability`; it is never omitted or fabricated.
Repeated `Galaxy` properties each create a fresh DTO, matching the source
property setter. An empty or partial later object therefore replaces all fields
from an earlier object rather than merging with it. Populated top-level
`Diplomacy` and `AdaptiveResearch` objects are explicit representability
boundaries for this Galaxy16-only DTO.

The encoder preserves property casing, explicit nulls, source omission rules,
integer widths, numeric enums, and all typed values. JSON whitespace and the
shortest textual spelling of floating-point values are implementation details.
The replay compares the complete typed projection and the full property, array,
and null shape. It also gives a native baseline document to the actual
System.Text.Json reader and CampaignSaveService restore/capture path.

Offset-bearing DateTimeOffset values are normalized to the source's serialized
form, including fractional-tick truncation and zero-offset normalization.
Date-only and offset-less input depends on the source host's local time zone and
is outside the portable DTO. Error locations use absolute UTF-8 byte offsets.

Historical wrappers, developer payloads, player-save version 17, and a general
save-file dispatcher remain outside this gate.
