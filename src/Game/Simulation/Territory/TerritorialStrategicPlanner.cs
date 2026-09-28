using System;
using System.Linq;
using Game.Simulation.AI;
using Game.Simulation.Models;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;

namespace Game.Simulation.Territory;

public sealed record TerritorialStrategicDecision(int CivilizationId, int? SystemId,
    TerritorialInstallationKind? Kind, double Score, bool NeedsLogisticsShip, string Reason);

/// <summary>Scheduled infrastructure advisor under the existing strategic director, using its priorities and traits.</summary>
public static class TerritorialStrategicPlanner
{
    public static TerritorialStrategicDecision Assess(GalaxyState galaxy, int owner, CivilizationStrategicIntent? intent = null)
    {
        TerritorialStrategicDecision Hold(string reason) => new(owner, null, null, 0, false, reason);
        var runtime = TerritorialRuntime.Peek(galaxy);
        var civ = galaxy.Civilizations.First(c => c.Id == owner);
        var economy = galaxy.Economies.First(e => e.CivilizationId == owner);
        if (runtime is null || !civ.ExpansionAllowed || civ.IsSeededAncient || civ.DevelopmentStage == CivilizationDevelopmentStage.PreWarp)
            return Hold("Regional expansion is not available at this development stage.");
        if (!galaxy.Technologies.First(t => t.CivilizationId == owner).CompletedTechnologyIds.Contains("orbital_industry"))
            return Hold("Develop orbital industry before extending regional infrastructure.");
        if (economy.OperatingArrears > 0 || economy.Credits < 180 || economy.Industry < 180 || economy.LastBaseOperationsFundingFraction < .8)
            return Hold("Restore funding and retain a treasury reserve before expansion.");
        if (galaxy.Territory!.Installations.Any(i => i.CivilizationId == owner && !i.IsComplete))
            return Hold("Finish the existing regional project before committing another site.");
        var builders = galaxy.Fleets.Where(f => f.IsActive && f.CivilizationId == owner && f.Role == FleetRole.Logistics).ToArray();
        if (builders.Any(f => f.DestinationSystemId is not null)) return Hold("A logistics ship is already travelling; finish its assignment first.");
        var owned = galaxy.Colonies.Where(c => c.CivilizationId == owner).ToArray();
        var weak = owned.Select(c => runtime.Read(owner, c.SystemId)!).Any(s => s.Administration < .4 || s.Supply < .3);
        var options = galaxy.Systems.Where(s => galaxy.Knowledge.IsSystemFullySurveyed(owner, s.Id))
            .Where(s => !galaxy.Colonies.Any(c => c.SystemId == s.Id && c.CivilizationId != owner))
            .Where(s => !galaxy.Territory.Installations.Any(i => i.SystemId == s.Id && i.CivilizationId != owner))
            .Select(s => (System: s, Reach: runtime.Read(owner, s.Id)!))
            .Where(x => x.Reach.Administration >= .035 && x.Reach.Supply >= .05)
            .OrderByDescending(x => x.Reach.Political).ThenBy(x => x.System.Id).Take(64)
            .SelectMany(x => TerritorialBalance.Installations.Select(d =>
            {
                var settled = owned.Any(c => c.SystemId == x.System.Id);
                var frontier = !settled && x.Reach.Expansion != ExpansionRegion.Established;
                var potential = x.System.HasHabitableWorld ? .25 : x.System.HasRareResource ? .2 : .05;
                var supportWeight = intent?.GetWeight(StrategicPriorityType.StabilizeSupply) ?? .5;
                var defenseWeight = intent?.GetWeight(StrategicPriorityType.Defend) ?? civ.Traits.Aggression;
                var score = d.Kind switch {
                    TerritorialInstallationKind.Relay => (1 - x.Reach.Administration) * .65 + (frontier ? potential + civ.Traits.Territoriality * .3 : 0),
                    TerritorialInstallationKind.SupplyDepot => (1 - x.Reach.Supply) * (.75 + supportWeight * .25) + (frontier ? potential : 0),
                    TerritorialInstallationKind.Administration => settled ? (1 - x.Reach.Administration) * 1.2 : 0,
                    TerritorialInstallationKind.TradeHub => settled ? civ.Traits.Greed * (1 - x.Reach.Trade) : 0,
                    TerritorialInstallationKind.ResearchStation => x.System.HasAnomaly ? civ.Traits.ScientificCuriosity * .75 : 0,
                    TerritorialInstallationKind.NavalBase => defenseWeight * (1 - x.Reach.Military) * (settled ? .7 : .3),
                    _ => 0 };
                if (weak && !settled && d.Kind is not (TerritorialInstallationKind.Relay or TerritorialInstallationKind.SupplyDepot)) score *= .3;
                score *= .6 + x.Reach.Supply * .4;
                return (x.System, Definition: d, Score: score);
            }))
            .Where(x => x.Score >= .48 && economy.Credits >= x.Definition.Credits + 120 && economy.Industry >= x.Definition.Industry)
            .Where(x => !galaxy.Territory.Installations.Any(i => i.CivilizationId == owner && i.SystemId == x.System.Id && i.Kind == x.Definition.Kind))
            .OrderByDescending(x => x.Score).ThenBy(x => x.System.Id).ThenBy(x => x.Definition.Kind);
        var reach = new LaneInterstellarOperationalReachView();
        foreach (var option in options)
        {
            var idle = builders.FirstOrDefault(f => f.CargoMaterials == 0 && f.FreightTargetOutpostId is null &&
                f.FreightHomeColonyId is null && f.TransitPhase == FleetTransitPhase.None &&
                reach.Assess(galaxy, owner, f, option.System.Id, InterstellarMissionKind.Logistics).IsSupported);
            if (builders.Length > 0 && idle is null) continue;
            return new(owner, option.System.Id, option.Definition.Kind, option.Score, builders.Length == 0,
                $"{option.Definition.Name} in {option.System.Name}: strategic value {option.Score:0.00}; strengthens a surveyed, supportable region while retaining operating reserves.");
        }
        return Hold("Current reach is adequate or no affordable, supportable improvement is known.");
    }

    public static TerritorialOrderResult Execute(GalaxyState galaxy, TerritorialStrategicDecision decision)
    {
        if (decision.SystemId is not int target || decision.Kind is not { } kind || decision.NeedsLogisticsShip)
            return new(false, decision.Reason);
        var fleet = galaxy.Fleets.FirstOrDefault(f => f.IsActive && f.CivilizationId == decision.CivilizationId &&
            f.Role == FleetRole.Logistics && f.CargoMaterials == 0 && f.FreightTargetOutpostId is null && f.FreightHomeColonyId is null &&
            f.DestinationSystemId is null && f.TransitPhase == FleetTransitPhase.None &&
            new LaneInterstellarOperationalReachView().Assess(galaxy, decision.CivilizationId, f, target, InterstellarMissionKind.Logistics).IsSupported);
        if (fleet is null) return new(false, "No uncommitted logistics vessel can support this site.");
        if (fleet.CurrentSystemId == target) return TerritorialConstruction.Start(galaxy, decision.CivilizationId, target, kind);
        var order = new FreightSimulation().IssueTransitOrder(galaxy, decision.CivilizationId, fleet.Id, target);
        return new(order.Accepted, order.Message);
    }
}
