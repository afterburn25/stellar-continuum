# Gate 083C knowledge persistence boundary

This gate ports only `CampaignSaveService.ToKnowledge` and `ToKnowledgeDtos`. Restore creates a new knowledge state; capture reads one existing state. It does not validate IDs against a galaxy, discover new information, run sensors, or implement an envelope/player save.

Restore preserves the parser-facing presence of the top-level DTO list, DTO elements, all three nested lists, and survey elements. It performs the landmark invariant before any mutation, then delegates in source order to the maintained public knowledge operations. An empty survey list selects the legacy fully-surveyed interpretation of known systems. A nonempty list reveals known systems first and then processes each survey. Unknown survey enum values have no switch arm and are retained as a no-op. Known civilizations are processed last.

Capture obtains the two public snapshot maps and sorted core observers, unions and numerically sorts observer IDs, and then uses public read methods for every projected collection. Core-only observers therefore remain present. Results own all nested values and never retain the source state.

Malformed null collections/records are represented explicitly for exact failure order. General JSON syntax/type diagnostics remain a parser-layer boundary. A failed restore does not publish its method-local state; partial effects are evidenced through the exact operation prefix and error ordering, while continuation checks use successfully returned states.
