<!-- native-architecture-notice-20260920 -->
> **Current architecture (2026-09-20): custom Stellar Engine / C++23 engine / C++23 game.**
> Godot/C#/.NET references below are legacy implementation or fixture provenance,
> not the current runtime or instructions to restore it.
> Start with [the current handoff](../AGENT_HANDOFF.md) and
> [verified project state](../PROJECT_STATE.md).

# Persistable fresh campaign boundary

`seed_persistable_fresh_campaign` is the native counterpart of the plain-C#
`CampaignSessionService.CreateNew(long, GalaxyGenerationSettings)` path for the
supported FullGalaxy profile. It delegates world creation to
`seed_fresh_campaign`, then attaches the canonical generation metadata and one
named core copied from that generated world's geometric core.

The returned campaign owns the metadata strings and both named-core values.
The caller injects `created_at_utc`; this boundary preserves it as opaque text
because timestamp parsing and `DateTimeOffset` roundtrip semantics belong to the
later general save codec. This factory is only for new campaigns. It does not
invent missing metadata while loading an existing campaign.

The lower-level `seed_fresh_campaign` remains available to callers that need
only deterministic world construction. This boundary adds no galaxy shapes or
generation-option parser and retains the lower-level factory's validation and
first-error order.
