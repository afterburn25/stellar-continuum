using System;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Diplomacy;

public sealed record CampaignTerritorialClaimCommandResult(
    bool Accepted,
    ObserverDiplomacyCommandStatus Status,
    string Message,
    long? ClaimId = null);

/// <summary>
/// Campaign-aware territorial claim assertion boundary. Unlike the state-only observer command
/// gateway, this service has access to civilization-scoped Exploration knowledge and can therefore
/// reject arbitrary omniscient system IDs before creating political state.
/// </summary>
public sealed class CampaignDiplomacyTerritorialClaimCommandService
{
    private const string ActionUnavailableMessage = "That territorial claim action is not currently available.";
    private const string InvalidRequestMessage = "The territorial claim request is not valid.";

    private readonly GalaxyState _galaxy;
    private readonly DiplomacyCampaignRuntimeCoordinator _runtime;
    private readonly DiplomacySimulation _diplomacy;

    public CampaignDiplomacyTerritorialClaimCommandService(
        GalaxyState galaxy,
        DiplomacyCampaignRuntimeCoordinator runtime)
    {
        _galaxy = galaxy ?? throw new ArgumentNullException(nameof(galaxy));
        _runtime = runtime ?? throw new ArgumentNullException(nameof(runtime));
        _diplomacy = new DiplomacySimulation(_runtime.State);
    }

    public CampaignTerritorialClaimCommandResult AssertTerritorialClaim(
        int observerCivilizationId,
        int systemId,
        long tick)
    {
        if (observerCivilizationId < 0 || systemId < 0 || tick < 0 ||
            !_galaxy.Civilizations.Any(civilization => civilization.Id == observerCivilizationId))
        {
            return InvalidRequest();
        }

        // A catalog-visible coordinate is not sufficient. The civilization must have legitimately
        // revealed the system in its own knowledge state. Nonexistent and not-yet-known system IDs
        // intentionally share one observer-safe rejection.
        if (!_galaxy.Systems.Any(system => system.Id == systemId) ||
            !_galaxy.Knowledge.IsSystemKnown(observerCivilizationId, systemId))
        {
            return ActionUnavailable();
        }

        var existing = _runtime.BuildView(observerCivilizationId).Claims.FirstOrDefault(claim =>
            claim.Active &&
            claim.ClaimantCivilizationId == observerCivilizationId &&
            claim.SystemId == systemId);
        if (existing is not null)
        {
            return new CampaignTerritorialClaimCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Territorial claim is already active.",
                existing.ClaimId);
        }

        try
        {
            var claimId = _diplomacy.AssertTerritorialClaim(observerCivilizationId, systemId, tick);
            return new CampaignTerritorialClaimCommandResult(
                true,
                ObserverDiplomacyCommandStatus.Accepted,
                "Territorial claim asserted.",
                claimId);
        }
        catch (ArgumentException)
        {
            return InvalidRequest();
        }
        catch (InvalidOperationException)
        {
            return ActionUnavailable();
        }
    }

    private static CampaignTerritorialClaimCommandResult ActionUnavailable() => new(
        false,
        ObserverDiplomacyCommandStatus.ActionUnavailable,
        ActionUnavailableMessage);

    private static CampaignTerritorialClaimCommandResult InvalidRequest() => new(
        false,
        ObserverDiplomacyCommandStatus.InvalidRequest,
        InvalidRequestMessage);
}

public static class CampaignDiplomacyTerritorialClaimCommandExtensions
{
    public static CampaignDiplomacyTerritorialClaimCommandService CreateTerritorialClaimCommands(
        this DiplomacyCampaignRuntimeCoordinator runtime,
        GalaxyState galaxy)
    {
        ArgumentNullException.ThrowIfNull(runtime);
        ArgumentNullException.ThrowIfNull(galaxy);
        return new CampaignDiplomacyTerritorialClaimCommandService(galaxy, runtime);
    }
}
