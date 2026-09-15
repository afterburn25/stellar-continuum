# Gate 040 executed coverage

The retained actual-source fixture contains eight stateful scenarios and 42 commands. Each scenario keeps one `GalaxySimulationStepCoordinator` and one `GalaxyState` across its command sequence. Every command records the complete serialized campaign before and after the typed call, its complete result or exact error, and any ordered hostility callback pairs. Two additional source observations cover raw null-campaign ordering. One native-only safety probe covers mission-revision exhaustion.

## Actual-source scenarios

- `matched-combat-sequence`: matched single and distinct/sorted batch issue, single and batch preview, Engage Hostiles, own force, readiness for known and unknown civilizations, and own-fleet status.
- `raw-combat-ordering`: raw reverse/duplicate/missing-ID batch, fail-closed Engage Hostiles, and fail-closed preview.
- `raw-batch-partial-exception`: the fixture requests an Attack batch and throws on the second ordered hostility callback. It records the partial state and then performs a successful Hold retry with the same coordinator.
- `deployment-duplicate-reresolve`: the coordinator selects the first owned active Military fleet, while exploration re-resolves the first active duplicate ID regardless of owner or role.
- `deployment-unsupported`: a finite range-limited fleet records the source reach rejection and unchanged campaign.
- `deployment-guards-and-success`: inactive, foreign, wrong-role, missing-fleet, missing-system, and already-stationed rejections followed by one successful deployment.
- `colony-outpost-filtering`: nonempty colony and outpost plans serialize every candidate and nested reach field; one colony order and one staffed outpost order succeed. The sequence also covers inactive, foreign, wrong-role, zero-population, NaN-population, and missing-species colony filtering plus a foreign outpost rejection.
- `freight-and-civilian-sequence`: freight collection and transit forwarding, then civilian Hold, Resume, Return preview, and Return request.

The callback control is serialized per scenario as `ThrowHostilityCall`. Native replay decodes it and compares the complete ordered `{First, Second}` callback log. The retained fixture has four callback observations across two scenarios.

## Separate observations

- Actual C# raw `IssueEngageHostilesOrder(null, ...)` returns the generic matched-runtime rejection before campaign access.
- Actual C# raw `PreviewMilitaryOrder(null, ...)` throws the matched-runtime configuration error before campaign access.
- The native mission-revision probe begins at `INT_MAX` with Attack state, invokes deployment, checks the exact exhaustion error, and verifies that the accepted Hold mutation remains while target, destination, and revision state match the documented partial-failure boundary.

Detailed calculations and exhaustive lower-level state matrices remain owned by the existing combat, fleet-status, settlement, freight, and civilian-recovery parity gates. Gate 040 exercises their public coordinator forwarding and state sequencing; it does not claim to repeat every lower-gate formula case.
