using System;
using System.Linq;
using System.Text;
using Game.Simulation.Colonization;
using Game.Simulation.Economy;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Presentation;

/// <summary>
/// Player-facing colony-opportunity adapter. Candidate eligibility, passenger-species viability,
/// knowledge filtering and reach reasons come from the authoritative Core/Colonization planner;
/// presentation only chooses among and formats those already-filtered results.
/// </summary>
public partial class Main
{
    public string UiColonyOpportunityDetails =>
        GetUiColonyOpportunityState(0, 0).Details;

    public ColonyOpportunityUiState GetUiColonyOpportunityState(
        int requestedFleetIndex,
        int requestedSiteIndex)
    {
        if (_galaxy is null)
            return ColonyOpportunityUiState.Unavailable("Colony opportunities are initializing…");

        var playerId = _galaxy.PlayerCivilizationId;
        var colonyFleets = _galaxy.Fleets
            .Where(fleet =>
                fleet.IsActive &&
                fleet.CivilizationId == playerId &&
                fleet.Role == FleetRole.Colony &&
                fleet.EmbarkedPopulationMillions > 0.0)
            .OrderBy(fleet => fleet.Id)
            .ToArray();

        if (colonyFleets.Length == 0)
        {
            return ColonyOpportunityUiState.Unavailable(
                "No populated colony ships are currently available for settlement planning.");
        }

        var fleetIndex = Math.Clamp(requestedFleetIndex, 0, colonyFleets.Length - 1);
        var fleet = colonyFleets[fleetIndex];
        var isOutpost = fleet.DesignId == ShipDesignRegistry.ResourceOutpostShipId;
        if (isOutpost)
            return GetUiResourceOutpostOpportunityState(fleet, fleetIndex, colonyFleets.Length, requestedSiteIndex);

        var plan = _coreSimulation.GetColonyOpportunityPlan(
            _galaxy,
            fleet.Id,
            maximumCandidates: 8);

        if (!plan.CanReceiveOrders || plan.Candidates.Count == 0)
        {
            return new ColonyOpportunityUiState(
                true,
                fleetIndex,
                colonyFleets.Length,
                0,
                0,
                fleet.Id,
                null,
                null,
                false,
                BuildFleetOnlyDetails(plan, fleetIndex, colonyFleets.Length),
                plan.Status,
                false,
                "Fund & Settle");
        }

        var siteIndex = Math.Clamp(requestedSiteIndex, 0, plan.Candidates.Count - 1);
        var site = plan.Candidates[siteIndex];
        var availableCredits = PlayerEconomy.Credits;
        var canAfford = availableCredits + 0.0001 >= ColonizationSimulation.ColonyExpeditionCreditCost;
        var details = BuildSelectedSiteDetails(
            plan,
            fleetIndex,
            colonyFleets.Length,
            siteIndex,
            plan.Candidates.Count,
            site,
            availableCredits,
            UiCurrency);

        return new ColonyOpportunityUiState(
            true,
            fleetIndex,
            colonyFleets.Length,
            siteIndex,
            plan.Candidates.Count,
            fleet.Id,
            site.SystemId,
            site.PlanetaryBodyId,
            site.CanOrder && canAfford,
            details,
            site.CanOrder && !canAfford
                ? $"Settlement requires {UiFormatMoney(ColonizationSimulation.ColonyExpeditionCreditCost)}; {UiFormatMoney(availableCredits)} is available."
                : site.Reason,
            false,
            "Fund & Settle");
    }

    public string IssueUiColonyOrder(
        int fleetId,
        int destinationSystemId,
        int planetaryBodyId)
    {
        if (_galaxy is null)
            return "Colony command is unavailable while the campaign is initializing.";

        var fleet = _galaxy.Fleets.FirstOrDefault(candidate => candidate.Id == fleetId);
        var result = fleet?.DesignId == ShipDesignRegistry.ResourceOutpostShipId
            ? _coreSimulation.IssueResourceOutpostFleetOrder(_galaxy, _galaxy.PlayerCivilizationId, fleetId, destinationSystemId, planetaryBodyId)
            : _coreSimulation.IssueColonyFleetOrder(_galaxy, _galaxy.PlayerCivilizationId, fleetId, destinationSystemId, planetaryBodyId);
        return result.Message;
    }

    private ColonyOpportunityUiState GetUiResourceOutpostOpportunityState(
        FleetState fleet, int fleetIndex, int fleetCount, int requestedSiteIndex)
    {
        var plan = _coreSimulation.GetResourceOutpostOpportunityPlan(_galaxy, fleet.Id, maximumCandidates: 8);
        if (!plan.CanReceiveOrders || plan.Candidates.Count == 0)
        {
            var fleetDetails = $"Outpost vessel {fleetIndex + 1}/{fleetCount}: {plan.FleetName} — {plan.PersonnelSpeciesName} — {plan.PersonnelMillions:0.#}M specialists aboard\n\n{plan.Status}";
            return new ColonyOpportunityUiState(true, fleetIndex, fleetCount, 0, 0, fleet.Id, null, null,
                false, fleetDetails, plan.Status, true, "Fund & Deploy");
        }

        var siteIndex = Math.Clamp(requestedSiteIndex, 0, plan.Candidates.Count - 1);
        var site = plan.Candidates[siteIndex];
        var credits = PlayerEconomy.Credits;
        var affordable = credits + 0.0001 >= ColonizationSimulation.ResourceOutpostExpeditionCreditCost;
        var details = new StringBuilder()
            .Append("Outpost vessel ").Append(fleetIndex + 1).Append('/').Append(fleetCount).Append(": ").Append(plan.FleetName)
            .Append(" — ").Append(plan.PersonnelSpeciesName).Append(" — ").Append(plan.PersonnelMillions.ToString("0.#")).AppendLine("M specialists aboard")
            .Append("Resource site ").Append(siteIndex + 1).Append('/').Append(plan.Candidates.Count)
            .Append(site.CanOrder ? ": ✓ " : ": · ").Append(site.SystemName).Append(" / ").AppendLine(site.PlanetaryBodyName)
            .Append(site.DepositGrade).Append(' ').Append(site.DepositMaterialName)
            .Append(" | yield ").Append(site.ExtractionYieldMultiplier.ToString("0.00")).Append("× | access ")
            .Append(site.DepositAccessibility.ToString("P0")).Append(" | reserve ").AppendLine(site.InitialDepositMaterials.ToString("N0"))
            .Append("Natural fit ").Append(site.NaturalHabitability.ToString("P0")).Append(" | unprotected capacity ")
            .Append(site.UnprotectedOperationalCapacity.ToString("P0")).Append(" | limiting factor ").AppendLine(site.LimitingFactor.ToString())
            .Append("Sealed outpost authorization: ").AppendLine(UiFormatMoney(ColonizationSimulation.ResourceOutpostExpeditionCreditCost))
            .Append("Treasury available: ").Append(UiFormatMoney(credits)).Append(" · ")
            .AppendLine(affordable ? "funded" : "additional funding required").AppendLine().Append(CompactPlannerReason(site.Reason)).ToString().TrimEnd();
        var reason = site.CanOrder && !affordable
            ? $"Outpost deployment requires {UiFormatMoney(ColonizationSimulation.ResourceOutpostExpeditionCreditCost)}; {UiFormatMoney(credits)} is available."
            : site.Reason;
        return new ColonyOpportunityUiState(true, fleetIndex, fleetCount, siteIndex, plan.Candidates.Count,
            fleet.Id, site.SystemId, site.PlanetaryBodyId, site.CanOrder && affordable, details, reason, true, "Fund & Deploy");
    }

    private static string BuildFleetOnlyDetails(
        ColonizationOpportunityPlan plan,
        int fleetIndex,
        int fleetCount)
    {
        var builder = new StringBuilder();
        builder.Append("Colony ship ").Append(fleetIndex + 1).Append('/').Append(fleetCount)
            .Append(": ").Append(plan.FleetName);
        if (!string.IsNullOrWhiteSpace(plan.PassengerSpeciesName))
            builder.Append(" — ").Append(plan.PassengerSpeciesName);
        builder.Append(" — ").Append(plan.EmbarkedPopulationMillions.ToString("0.#")).AppendLine("M aboard");
        builder.AppendLine();
        builder.Append(plan.Status);
        return builder.ToString();
    }

    private static string BuildSelectedSiteDetails(
        ColonizationOpportunityPlan plan,
        int fleetIndex,
        int fleetCount,
        int siteIndex,
        int siteCount,
        ColonizationOpportunityCandidate site,
        double availableCredits,
        SovereignCurrencyDefinition currency)
    {
        var builder = new StringBuilder();
        builder.Append("Colony ship ").Append(fleetIndex + 1).Append('/').Append(fleetCount)
            .Append(": ").Append(plan.FleetName)
            .Append(" — ").Append(plan.PassengerSpeciesName)
            .Append(" — ").Append(plan.EmbarkedPopulationMillions.ToString("0.#")).AppendLine("M aboard");
        builder.Append("Site ").Append(siteIndex + 1).Append('/').Append(siteCount)
            .Append(site.CanOrder ? ": ✓ " : ": · ")
            .Append(site.SystemName).Append(" / ").AppendLine(site.PlanetaryBodyName);
        builder.Append("Viability: ").Append(FormatColonizationViability(site.ColonizationViability))
            .Append(" | natural fit ").Append(site.NaturalHabitability.ToString("P0"))
            .Append(" | unprotected ").AppendLine(site.UnprotectedOperationalCapacity.ToString("P0"));
        builder.Append("Limiting factor: ").Append(site.LimitingFactor)
            .Append(" | reach: ").Append(site.Reach.IsSupported ? "supported" : "blocked");
        if (!site.Reach.IsAuthoritative)
            builder.Append(" (provisional)");
        builder.AppendLine();
        builder.Append("Expedition authorization: ").AppendLine(currency.Format(ColonizationSimulation.ColonyExpeditionCreditCost));
        builder.Append("Treasury available: ").Append(currency.Format(availableCredits))
            .Append(" · ").AppendLine(availableCredits + 0.0001 >= ColonizationSimulation.ColonyExpeditionCreditCost ? "funded" : "additional funding required");
        builder.AppendLine();
        builder.Append(CompactPlannerReason(site.Reason));
        return builder.ToString().TrimEnd();
    }

    private static string FormatColonizationViability(Game.Simulation.Species.SpeciesColonizationViability viability) => viability switch
    {
        Game.Simulation.Species.SpeciesColonizationViability.NaturallyViable => "natural",
        Game.Simulation.Species.SpeciesColonizationViability.HabitatSupportedFallback => "habitat support",
        _ => "unsuitable",
    };

    private static string CompactPlannerReason(string reason)
    {
        const int maximumLength = 180;
        if (string.IsNullOrWhiteSpace(reason) || reason.Length <= maximumLength)
            return reason;

        return reason[..(maximumLength - 1)].TrimEnd() + "…";
    }
}

public sealed record ColonyOpportunityUiState(
    bool Available,
    int FleetIndex,
    int FleetCount,
    int SiteIndex,
    int SiteCount,
    int? FleetId,
    int? SystemId,
    int? PlanetaryBodyId,
    bool CanOrder,
    string Details,
    string ActionReason,
    bool IsResourceOutpostMission,
    string ActionLabel)
{
    public static ColonyOpportunityUiState Unavailable(string details) =>
        new(false, 0, 0, 0, 0, null, null, null, false, details, details, false, "Fund & Settle");
}
