using System;
using System.Linq;
using Game.Diagnostics;
using Game.Simulation.Economy;
using Game.Simulation.Territory;

namespace Game.Presentation;

public sealed record UiTerritorialInstallationOption(TerritorialInstallationKind Kind, string Name, string Preview, bool Available);
public sealed record UiTerritorialSite(int Id, string Name, double Progress, bool Complete, bool Paused, string Detail);

public partial class Main
{
    public UiTerritorialInstallationOption[] UiTerritorialInstallationOptions
    {
        get
        {
            if (_galaxy is null || _selectedSystemId < 0) return Array.Empty<UiTerritorialInstallationOption>();
            var owner = _galaxy.PlayerCivilizationId;
            return TerritorialBalance.Installations.Select(definition =>
            {
                var preview = TerritorialConstruction.Preview(_galaxy, owner, _selectedSystemId, definition.Kind);
                return new UiTerritorialInstallationOption(definition.Kind, definition.Name, preview.Message, preview.Accepted);
            }).ToArray();
        }
    }

    public UiTerritorialSite[] UiSelectedTerritorialSites
    {
        get
        {
            if (_galaxy?.Territory is null || _selectedSystemId < 0) return Array.Empty<UiTerritorialSite>();
            return _galaxy.Territory.Installations.Where(item => item.SystemId == _selectedSystemId &&
                item.CivilizationId == _galaxy.PlayerCivilizationId).OrderBy(item => item.Id).Select(site =>
            {
                var builderPresent = _galaxy.Fleets.Any(fleet => fleet.Id == site.BuilderFleetId &&
                    TerritorialConstruction.CanBuild(fleet, site.CivilizationId, site.SystemId));
                var funding = CivilizationOperatingCapacity.GetFundingFraction(_galaxy, site.CivilizationId);
                var paused = funding <= .01 || !site.IsComplete && !builderPresent;
                var detail = site.IsComplete
                    ? $"{(funding <= .01 ? "Offline: operating funding unavailable" : funding < .99 ? $"Operating at {funding:P0} funding" : "Operational")} · upkeep {UiFormatMoney(TerritorialBalance.Definition(site.Kind).Upkeep)}/day · decommissioning has no refund."
                    : !builderPresent ? "Paused: an unassigned logistics ship must remain at this system to continue work."
                    : funding <= .01 ? "Paused: operating funding is unavailable for this regional work site."
                    : $"Building · {site.CompletedDays:0}/{site.RequiredDays:0} days · moving the logistics ship pauses work.";
                return new UiTerritorialSite(site.Id, TerritorialBalance.Definition(site.Kind).Name,
                    Math.Clamp(site.CompletedDays / Math.Max(1, site.RequiredDays), 0, 1), site.IsComplete, paused, detail);
            }).ToArray();
        }
    }

    public void UiStartTerritorialInstallation(TerritorialInstallationKind kind)
    {
        if (_galaxy is null || _selectedSystemId < 0) return;
        var result = TerritorialConstruction.Start(_galaxy, _galaxy.PlayerCivilizationId, _selectedSystemId, kind);
        SetStatus(result.Message, result.Accepted ? 7 : 8);
        SupportLogger.Log("territorial-construction", $"system={_selectedSystemId} kind={kind} accepted={result.Accepted} message={result.Message}");
        if (result.Accepted) PublishPlayerNotification("Regional infrastructure", result.Message);
        QueueRedraw();
    }

    public void UiRemoveTerritorialInstallation(int installationId, bool confirmed)
    {
        var site = UiSelectedTerritorialSites.FirstOrDefault(item => item.Id == installationId);
        if (_galaxy is null || site is null) return;
        if (site.Complete && !confirmed)
        {
            SetStatus("Confirm decommissioning: completed regional infrastructure is removed without a refund.", 8);
            return;
        }
        var result = TerritorialConstruction.Remove(_galaxy, _galaxy.PlayerCivilizationId, installationId);
        SetStatus(result.Message, result.Accepted ? 7 : 8);
        SupportLogger.Log("territorial-construction", $"remove={installationId} accepted={result.Accepted} message={result.Message}");
        if (result.Accepted) PublishPlayerNotification("Regional infrastructure", result.Message);
        QueueRedraw();
    }
}
