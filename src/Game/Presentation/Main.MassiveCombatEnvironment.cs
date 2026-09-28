using System;
using System.Linq;
using Game.Presentation.Spatial;
using Game.Simulation.Construction;
using Game.Simulation.Models;

namespace Game.Presentation;

public partial class Main
{
    private Guid? _massiveCombatEnvironmentBattleId;
    private SystemSpatialSnapshot? _massiveCombatEnvironmentSnapshot;

    /// <summary>Builds only the system knowledge already available to the observing player.</summary>
    private SystemSpatialSnapshot? BuildMassiveCombatEnvironment()
    {
        if (_galaxy?.ActiveCombatEncounter is not { Reconciled: false } encounter)
        {
            _massiveCombatEnvironmentBattleId = null;
            _massiveCombatEnvironmentSnapshot = null;
            return null;
        }
        if (_massiveCombatEnvironmentBattleId == encounter.Battle.BattleId)
            return _massiveCombatEnvironmentSnapshot;
        var exploration = _spatialExplorationReadModel.Build(_galaxy, _galaxy.PlayerCivilizationId);
        var known = exploration.KnownSystems.FirstOrDefault(candidate => candidate.SystemId == encounter.SystemId);
        if (known is null || !known.HasReconnaissanceCatalog) return null;

        var snapshot = _systemSpatialProjection.Build(known);
        var inhabited = _galaxy.Colonies.Where(c => c.CivilizationId == _galaxy.PlayerCivilizationId &&
            c.SystemId == snapshot.SystemId && c.PopulationMillions > 0).Select(c => c.PlanetaryBodyId).ToHashSet();
        snapshot = snapshot with { Bodies = snapshot.Bodies.Select(body => body with
            { HasCityLights = body.HasDetailedEnvironment && inhabited.Contains(body.BodyId) }).ToArray() };
        if (snapshot.SystemId != PlayerCivilization.HomeSystemId)
            return Cache(encounter.Battle.BattleId, snapshot);

        var construction = PlayerConstruction;
        snapshot = snapshot with
        {
            Infrastructure = ConstructionRegistry.All.Where(project => project.Category == ConstructionCategory.Orbital)
                .Select(project =>
                {
                    var complete = construction.CompletedProjectIds.Contains(project.Id);
                    var active = construction.ActiveProjectId == project.Id;
                    var available = _construction.GetLockReason(_galaxy, _galaxy.PlayerCivilizationId, project) is null;
                    var state = complete ? SystemSpatialInfrastructureState.Complete :
                        active ? SystemSpatialInfrastructureState.Active :
                        available ? SystemSpatialInfrastructureState.Available : SystemSpatialInfrastructureState.Locked;
                    var progress = complete ? 1 : active && project.IndustryCost > 0
                        ? Math.Clamp(construction.ActiveProjectProgress / project.IndustryCost, 0, 1) : 0;
                    return new SystemSpatialInfrastructureMarker(project.Id, project.Name, state, progress,
                        project.Id == "asteroid_resource_network" ? null :
                            _galaxy.Colonies.Where(c => c.CivilizationId == _galaxy.PlayerCivilizationId &&
                                c.SystemId == snapshot.SystemId).OrderByDescending(c => c.PopulationMillions)
                                .FirstOrDefault()?.PlanetaryBodyId);
                }).Where(marker => marker.State is SystemSpatialInfrastructureState.Complete or
                    SystemSpatialInfrastructureState.Active).ToArray(),
        };
        return Cache(encounter.Battle.BattleId, snapshot);
    }

    private SystemSpatialSnapshot Cache(Guid battleId, SystemSpatialSnapshot snapshot)
    {
        _massiveCombatEnvironmentBattleId = battleId;
        return _massiveCombatEnvironmentSnapshot = snapshot;
    }
}
