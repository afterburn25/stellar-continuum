# Legacy research progression validation

The native legacy research port passes 65 actual-C# cases for StartResearch, Advance and AdvanceForCivilization, including seven source failures. Three null-galaxy observations remain source-only boundary documentation. Fixture SHA-256: `90EA4301AC8E9AB57413966310EEEC6015D07E9B3F10B32BE0BA54C838891498`. The retained generator reproduces this hash.

Standalone Release and Debug pass with MSVC `/W4 /WX`. The maintained build with both exploration advancement and legacy research passes 35/35 CTest and 19/19 Python checks (`work/native-027-live-testing.log`). Engine 0.1.14 will package this combined integration.

The comparison includes ordered civilization snapshots and leadership, first matching technology/construction/economy records, AI score order, available/completed IDs, science spending, progress, exact errors, completion events and PreWarp-to-WarpCapable replacement. Negative and nonfinite scalar behavior follows the source. Earlier mutations survive a later exception, while method-local events are not returned. Input decoding and expected-result encoding stay outside operation catches; targeted commands require a civilization ID before dispatch.

This is explicitly the existing six-definition legacy ResearchSimulation. It does not replace Adaptive Research, add a new research tree, invent a monetary charge or spawn ships when warp research completes. Adaptive research and the full campaign coordinator remain separate migration gates.
