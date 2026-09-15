# Settlement planning validation

The retained actual C# oracle contains 90 cases covering colony and staffed
resource-outpost plans, explicit order assessments, operational reach, and outpost
fleet classification. The matrix covers private, partial, and complete surveys;
fleet and passenger validation; duplicate system and body dictionaries; missing
economies; exact affordability boundaries and currency messages; occupied and
reserved systems; foreign mission noninterference; candidate caps; explicit colony
targets beyond the display cap; outpost targets beyond its hard assessment cap;
stable ties; NaN ordering; flat float-distance ties; large-coordinate depth distance;
and both injected and authoritative lane reach.

Every candidate and plan field is compared, including ordered output, biology,
deposit metadata, physical distance, reach payloads, reservation metadata, status,
and exact approval or rejection text. The oracle records 80 reach callback calls,
including callbacks that throw before a result exists. The native consumer decodes
and validates fixture arguments before its operation catch, invokes only the typed
production method inside that catch, and encodes and checks typed results afterward.
It also verifies read-only world preservation. Six null-reference observations that
the typed native API cannot represent remain separately reported.

Source comparison exposed three production differences that were repaired: colony
approval text now uses the shared source-compatible custom formatter, ascending distance
ordering follows .NET's NaN ordering, and physical distance now reuses the accepted
`interstellar_distance_from_fleet` helper. The latter preserves C# `Vector2.Distance`
float behavior for legacy flat geometry and double subtraction for depth-aware
geometry. Dedicated approval probes verify midpoint rounding (`1.25` to `1.3`) and
positive-infinity text (`Infinity`) from the actual C# method.

The promoted fixture SHA-256 is
`ECD1FD275C77D4C13DED945C547BA274BB72088220D82F7965228DCB4D53D209`.

The maintained `windows-testing` build passed 41 of 41 CTest targets, including all
90 settlement-planning cases, and 19 of 19 Python recovery/package-integrity tests.
Its complete log is `work/native-033-strategic-planning-testing.log` with SHA-256
`FACB499DE74EC92CB0B7B7821654CCB6E87DEC0C34991932747BC436F1EF44C0`.
