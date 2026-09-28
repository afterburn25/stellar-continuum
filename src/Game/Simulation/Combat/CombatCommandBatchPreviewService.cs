using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Combat;

/// <summary>
/// One fleet's result inside a transient multi-selection military-order preview.
/// </summary>
public sealed record FleetCombatOrderPreviewResult(
    int FleetId,
    bool Accepted,
    string Message);

/// <summary>
/// Read-only aggregate preview for one military order fanned out across a transient fleet selection.
/// Selection membership is never persisted and duplicate fleet IDs are evaluated once in stable order.
/// </summary>
public sealed record CombatBatchOrderPreview(
    int RequestedFleetCount,
    int AcceptedCount,
    int RejectedCount,
    IReadOnlyList<FleetCombatOrderPreviewResult> FleetResults)
{
    public bool AllAccepted => RequestedFleetCount > 0 && RejectedCount == 0;
    public bool AnyAccepted => AcceptedCount > 0;
}

/// <summary>
/// Non-mutating batch counterpart to <see cref="CombatCommandBatchService"/>. It deliberately reuses
/// <see cref="CombatOrderPreviewService"/> per fleet so multi-selection UI/AI preflight cannot drift
/// from the accepted single-order rules. This service does not discover targets, create task-force
/// membership, normalize Combat state, or issue any military order.
/// </summary>
public sealed class CombatCommandBatchPreviewService
{
    private readonly CombatOrderPreviewService _preview;

    public CombatCommandBatchPreviewService(CombatOrderPreviewService preview)
    {
        _preview = preview ?? throw new ArgumentNullException(nameof(preview));
    }

    public CombatBatchOrderPreview Preview(
        GalaxyState galaxy,
        int civilizationId,
        IEnumerable<int> fleetIds,
        MilitaryOrder order)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(fleetIds);
        ArgumentNullException.ThrowIfNull(order);

        var uniqueFleetIds = fleetIds
            .Distinct()
            .OrderBy(fleetId => fleetId)
            .ToArray();
        var results = new List<FleetCombatOrderPreviewResult>(uniqueFleetIds.Length);

        foreach (var fleetId in uniqueFleetIds)
        {
            var preview = _preview.Preview(galaxy, civilizationId, fleetId, order);
            results.Add(new FleetCombatOrderPreviewResult(fleetId, preview.Accepted, preview.Message));
        }

        return new CombatBatchOrderPreview(
            uniqueFleetIds.Length,
            results.Count(result => result.Accepted),
            results.Count(result => !result.Accepted),
            results);
    }
}
