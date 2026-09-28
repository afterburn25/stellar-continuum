using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Combat;

/// <summary>
/// Transient command-side composition for one authoritative CombatSimulation plus its read-only
/// single/batch order previews. All three are constructed from the exact same hostility view so
/// campaign Core cannot accidentally preview with different political permission from issuance.
/// </summary>
public sealed class CombatCommandRuntime
{
    private readonly CombatCommandBatchService _batchCommands;

    public CombatCommandRuntime(ICombatHostilityView? hostilityView = null)
    {
        var sharedHostility = hostilityView ?? PeacefulCombatHostilityView.Instance;
        Simulation = new CombatSimulation(sharedHostility);
        OrderPreview = new CombatOrderPreviewService(sharedHostility);
        BatchOrderPreview = new CombatCommandBatchPreviewService(OrderPreview);
        _batchCommands = new CombatCommandBatchService(Simulation);
    }

    public CombatSimulation Simulation { get; }
    public CombatOrderPreviewService OrderPreview { get; }
    public CombatCommandBatchPreviewService BatchOrderPreview { get; }

    public CombatOrderResult IssueOrder(
        GalaxyState galaxy,
        int civilizationId,
        int fleetId,
        MilitaryOrder order) =>
        Simulation.IssueOrder(galaxy, civilizationId, fleetId, order);

    public CombatBatchOrderResult IssueOrders(
        GalaxyState galaxy,
        int civilizationId,
        IEnumerable<int> fleetIds,
        MilitaryOrder order) =>
        _batchCommands.IssueOrder(galaxy, civilizationId, fleetIds, order);

    /// <summary>
    /// Issues an attack without exposing authoritative foreign fleet identities to presentation.
    /// Only co-located fleets that pass the same matched hostility/order preview are candidates;
    /// stable ID ordering makes selection replayable. A rejected result does not reveal whether
    /// a peaceful, disengaged, or absent foreign vessel caused the generic failure.
    /// </summary>
    public CombatOrderResult IssueEngageHostiles(
        GalaxyState galaxy,
        int civilizationId,
        int fleetId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var actor = galaxy.Fleets.FirstOrDefault(fleet => fleet.Id == fleetId && fleet.IsActive &&
            fleet.CivilizationId == civilizationId);
        if (actor?.CurrentSystemId is not int systemId)
            return new(false, "The selected fleet must be present in a star system to engage hostiles.");

        foreach (var candidate in galaxy.Fleets
                     .Where(fleet => fleet.IsActive && fleet.CivilizationId != civilizationId &&
                         fleet.CurrentSystemId == systemId)
                     .OrderBy(fleet => fleet.Id))
        {
            var order = new MilitaryOrder(MilitaryOrderType.Attack, candidate.Id);
            if (OrderPreview.Preview(galaxy, civilizationId, fleetId, order).Accepted)
                return Simulation.IssueOrder(galaxy, civilizationId, fleetId, order);
        }

        return new(false, "No attackable hostile fleet is detected in this fleet's current system.");
    }

    public CombatOrderPreview PreviewOrder(
        GalaxyState galaxy,
        int civilizationId,
        int fleetId,
        MilitaryOrder order) =>
        OrderPreview.Preview(galaxy, civilizationId, fleetId, order);

    public CombatBatchOrderPreview PreviewOrders(
        GalaxyState galaxy,
        int civilizationId,
        IEnumerable<int> fleetIds,
        MilitaryOrder order) =>
        BatchOrderPreview.Preview(galaxy, civilizationId, fleetIds, order);
}
