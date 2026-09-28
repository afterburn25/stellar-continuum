using System;
using Game.Simulation.Construction;

namespace Game.Presentation;

public sealed record UiOrbitalConstruction(string Id, string Name, string Description, string State,
    double Progress, double DaysRemaining, string Cost, string Upkeep, double MaterialOutput,
    bool CanBuild, string? LockReason, int? QueuePosition = null, double CancellationRefundPreview = 0);

public partial class Main
{
    private string? _orbitalProjectId;
    public void UiCloseOrbitalInspector() => _orbitalProjectId = null;
    private void InspectOrbitalStructure(string id)
    {
        if (ConstructionRegistry.Find(id)?.Category != ConstructionCategory.Orbital) return;
        UiClearFleetSelection();
        GetNode<CampaignSidebar>("CampaignSidebar").CloseDrawer();
        _orbitalProjectId = id;
    }
    public UiOrbitalConstruction? UiSelectedOrbitalConstruction
    {
        get
        {
            if (!UiIsSystemSpatialView || _selectedSystemId != PlayerCivilization.HomeSystemId ||
                _orbitalProjectId is null || ConstructionRegistry.Find(_orbitalProjectId) is not { } project) return null;
            var state = PlayerConstruction;
            var complete = state.CompletedProjectIds.Contains(project.Id);
            var active = state.ActiveProjectId == project.Id;
            var queuePosition = state.QueuedProjects.FindIndex(order => order.ProjectId == project.Id);
            var queued = queuePosition >= 0;
            var reason = _construction.GetLockReason(_galaxy, _galaxy.PlayerCivilizationId, project);
            if (queued && queuePosition == 0 && state.ActiveProjectId is null)
                reason = _construction.GetQueueBlockerReason(_galaxy, _galaxy.PlayerCivilizationId) ?? reason;
            if (queued) reason ??= $"Already queued at position {queuePosition + 1}.";
            if (!complete && !active && !queued && state.QueuedProjects.Count >= ConstructionState.MaxQueuedProjects) reason ??= "Construction queue is full.";
            if (!complete && !active && PlayerEconomy.Credits < project.CreditCost) reason ??= "Insufficient funds for construction authorization.";
            return new(project.Id, project.Name, project.Description, complete ? "Operational" : active ? "Under construction" : queued ? "Queued" : "Planned orbital site",
                complete ? 1 : active ? state.ActiveProjectProgress / project.IndustryCost : 0,
                complete || queued ? 0 : Math.Max(0, project.IndustryCost - (active ? state.ActiveProjectProgress : 0)) / ConstructionSimulation.IndustryPerDay,
                $"{UiFormatMoney(project.CreditCost)} · {project.IndustryCost:N0} materials",
                UiFormatMoney(project.UpkeepCreditsPerDay) + " / day", project.IndustryPerDay,
                !complete && !active && !queued && reason is null, reason, queued ? queuePosition + 1 : null,
                _construction.GetCancellationRefundPreview(state, project.Id));
        }
    }
}
