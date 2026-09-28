using System;
using System.Linq;
using Game.Simulation.Models;
using Game.Simulation.Colonization;
using Game.Simulation.Economy;

namespace Game.Simulation.Territory;

public sealed record TerritorialExpansionQuote(bool Allowed, ExpansionRegion Region, double Credits, double Days, string Reason);
public static class TerritorialExpansion
{
    public static TerritorialExpansionQuote Quote(GalaxyState galaxy, FleetState fleet, int systemId)
    {
        var outpost = ResourceOutpostOpportunityPlanner.IsOutpostFleet(fleet);
        var credits = outpost ? ColonizationSimulation.ResourceOutpostExpeditionCreditCost : ColonizationSimulation.ColonyExpeditionCreditCost;
        var days = outpost ? ColonizationSimulation.OutpostEstablishmentDays : ColonizationSimulation.ColonyEstablishmentDays;
        var score = TerritorialRuntime.Peek(galaxy)?.Read(fleet.CivilizationId, systemId);
        if (score is null) return new(true, ExpansionRegion.Established, credits, days, "");
        var allowed = score.Expansion != ExpansionRegion.Remote;
        // Staffed extraction outposts may bridge a slightly weaker frontier, but still need support.
        if (outpost && score.Administration >= .06 && score.Supply >= .08) allowed = true;
        var region = allowed && score.Expansion == ExpansionRegion.Remote ? ExpansionRegion.Frontier : score.Expansion;
        if (region != ExpansionRegion.Established) { credits *= score.ExpeditionMultiplier; days *= score.EstablishmentMultiplier; }
        var currency = SovereignCurrencyCatalog.ForCivilization(galaxy, fleet.CivilizationId);
        var explanation = $"{region} expansion: political reach {score.Political:0.0}, administration {score.Administration:P0}, operational supply {score.Supply:P0}. ";
        explanation += allowed ? $"Expedition {currency.Format(credits)}; establishment {days:0.0} days. Control risk {score.InstabilityRisk:P0}; administration upkeep ×{score.AdministrationMultiplier:0.00}."
            : $"Normal settlement requires administration {(outpost ? .06 : TerritorialBalance.FrontierMinimumAdministration):P0} and operational supply {(outpost ? .08 : TerritorialBalance.FrontierMinimumSupply):P0}. Extend communications relays and supply depots from an existing colony, or develop a closer outpost first.";
        if (!allowed && galaxy.Systems.FirstOrDefault(s => s.Id == systemId) is { } target)
        {
            var nearest = galaxy.Colonies.Where(c => c.CivilizationId == fleet.CivilizationId && c.PopulationMillions > 0)
                .Select(c => (Colony: c, System: galaxy.Systems.FirstOrDefault(s => s.Id == c.SystemId)))
                .Where(item => item.System is not null)
                .OrderBy(item => InterstellarDistance.Between(item.System!, target)).FirstOrDefault();
            if (nearest.Colony is not null)
                explanation += $" Nearest populated anchor: {nearest.Colony.Name}, {Game.Units.InterstellarDistanceUnits.FormatMetricPrimary(InterstellarDistance.Between(nearest.System!, target))}.";
        }
        return new(allowed, region, credits, days, explanation);
    }
    public static double AdditionalCredits(GalaxyState galaxy, FleetState fleet, TerritorialExpansionQuote quote)
    {
        if (galaxy.Territory is null) return quote.Credits;
        var previous = galaxy.Territory.Expeditions.FirstOrDefault(e => e.FleetId == fleet.Id);
        return Math.Max(0, quote.Credits - (previous?.PaidCredits ?? 0));
    }
    public static bool Authorize(GalaxyState galaxy, FleetState fleet, int systemId, int bodyId, out string reason)
    {
        TerritorialRuntime.Initialize(galaxy).Recompute(galaxy);
        var quote = Quote(galaxy, fleet, systemId); reason = quote.Reason;
        if (!quote.Allowed) return false;
        var extra = AdditionalCredits(galaxy, fleet, quote);
        var economy = galaxy.Economies.First(e => e.CivilizationId == fleet.CivilizationId);
        if (economy.Credits + .0001 < extra) { reason = "Insufficient expedition funding. " + reason; return false; }
        economy.Credits -= extra;
        var state = galaxy.Territory!;
        var prior = state.Expeditions.FirstOrDefault(e => e.FleetId == fleet.Id);
        state.Expeditions.RemoveAll(e => e.FleetId == fleet.Id);
        state.Expeditions.Add(new(fleet.Id, bodyId, Math.Max(quote.Credits, prior?.PaidCredits ?? 0), quote.Days));
        return true;
    }
    public static double RequiredDays(GalaxyState galaxy, FleetState fleet) => galaxy.Territory?.Expeditions
        .FirstOrDefault(e => e.FleetId == fleet.Id)?.RequiredDays ?? ColonizationSimulation.EstablishmentDays(fleet);
}
