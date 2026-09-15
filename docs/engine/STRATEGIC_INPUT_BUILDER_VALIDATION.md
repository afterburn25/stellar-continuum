# Strategic input builder validation

The retained actual C# oracle contains 52 self-state cases, eight expected operation failures, 210 ordered injected callback observations, and one separately recorded null-world observation. The fixture SHA-256 is `451DE5E04F6C364B0A928DBF6100CADED84B4CB83DF3CCD6832C6217DB677E7C`.

The matrix covers required-state validation order; first-match behavior for duplicate civilization, economy, technology, construction and fleet IDs; logistics failure; negative and NaN economy inputs; spacecraft and experimental-transit capability ordering and failures; shipyard and legacy-research availability; and active, inactive, foreign, damaged and missing military fleets. Exploration cases invoke the actual mission planner, include default authoritative reach, preserve fleet-ID order and early success, exercise a 71-system blocked planning window, and record reach failures before later capability calls.

Colonization opportunity cases distinguish unknown, partial and complete surveys, treat settlements of every owner as occupied systems, exclude zero and foreign population sources, and evaluate mixed available species in source order through the authoritative biology implementation. Default cases use the real logistics, prototype capabilities, exploration, biology, combat readiness and legacy research ports.

The native world exposes const spans for all campaign records. Its lane reference is non-const only for the accepted reach-graph cache. Source snapshots compare the complete serialized input before and after every operation. The native fingerprint covers every decoded combat field in addition to the fields read by the builder; the const API prevents mutation of the remaining decoded world records.

Standalone C++23 MSVC Release and Debug builds pass all 52 cases with `/W4 /WX`. Object and PDB outputs remain in the ignored gate workspace. Maintained Windows validation is intentionally deferred for the combined strategic integration build.
