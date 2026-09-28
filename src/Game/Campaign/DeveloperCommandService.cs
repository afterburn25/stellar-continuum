using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Construction;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;

namespace Game.Campaign;

public sealed record DeveloperCommandDefinition(string Id, string Title, string Description);
public sealed record DeveloperCommandResult(bool Accepted, string Message);

/// <summary>Explicit testing operations on a marked Developer world. Normal gameplay never
/// calls this boundary and cannot activate it by selecting a faster clock or opening a panel.</summary>
public static class DeveloperCommandService
{
    public static IReadOnlyList<DeveloperCommandDefinition> Commands { get; } = Array.AsReadOnly(new[]
    {
        new DeveloperCommandDefinition("grant_resources", "Add test resources", "Add sovereign treasury funds and 1,000 industry to your civilization."),
        new DeveloperCommandDefinition("finish_orders", "Finish current orders", "Fund and finish your active research, project, ship and placed surface sites. Only the active ship completes; later ships still need construction."),
        new DeveloperCommandDefinition("reveal_galaxy", "Survey the galaxy", "Reveal system survey information to your Developer civilization."),
        new DeveloperCommandDefinition("unlock_technology", "Unlock gameplay technology", "Complete the current gameplay technology and empire project catalogs. Ships still need construction."),
        new DeveloperCommandDefinition("unlock_research", "Unlock research only", "Grant gameplay capabilities while preserving construction orders and project timers for testing."),
        new DeveloperCommandDefinition("advance_30_days", "Advance 30 days", "Run 30 days through normal simulation rules, including other civilizations and diplomacy."),
    });

    public static DeveloperCommandResult Execute(GalaxyState galaxy, string commandId, Action<double>? advanceDays = null)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (galaxy.DeveloperSession is null)
            return new(false, "Developer commands are unavailable in Player mode. Open your separate Developer campaign first.");
        if (!Commands.Any(command => command.Id == commandId))
            return new(false, "Unknown Developer command.");
        if (commandId == "advance_30_days" && advanceDays is null)
            return new(false, "The campaign simulation must be connected before advancing time.");
        var playerId = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.First(e => e.CivilizationId == playerId);
        if (!double.IsFinite(economy.Credits) || !double.IsFinite(economy.Industry) || !double.IsFinite(economy.Science) ||
            economy.Credits < 0 || economy.Industry < 0 || economy.Science < 0)
            return new(false, "The campaign has invalid resources. Load a valid checkpoint before using Developer commands.");
        // Mark before mutation: even an unexpected partial failure must retain its provenance.
        galaxy.DeveloperSession = galaxy.DeveloperSession with { ToolsUsed = true };
        switch (commandId)
        {
            case "grant_resources":
                economy.Credits += 1000;
                economy.Industry += 1000;
                var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, playerId);
                return new(true, $"Added {currency.Format(1000)} and 1,000 industry. This Developer campaign is marked Tools used.");
            case "finish_orders":
                FinishOrders(galaxy, playerId);
                return new(true, "Current research, project, active ship and surface sites completed. Tools used is saved with this campaign.");
            case "reveal_galaxy":
                foreach (var system in galaxy.Systems) galaxy.Knowledge.MarkSystemFullySurveyed(playerId, system.Id);
                return new(true, "Your Developer civilization has surveyed every system. Other observers keep their own knowledge.");
            case "unlock_technology":
            case "unlock_research":
                var technology = galaxy.Technologies.First(t => t.CivilizationId == playerId);
                technology.CompletedTechnologyIds.UnionWith(TechnologyRegistry.All.Select(t => t.Id));
                technology.ActiveResearchId = null; technology.ActiveResearchProgress = 0;
                if (commandId == "unlock_technology")
                {
                    var construction = galaxy.ConstructionStates.First(c => c.CivilizationId == playerId);
                    construction.CompletedProjectIds.UnionWith(ConstructionRegistry.All.Select(c => c.Id));
                    construction.ActiveProjectId = null; construction.ActiveProjectProgress = 0;
                    construction.ActiveProjectAuthorizationCredits = 0; construction.QueuedProjects.Clear();
                }
                for (var i = 0; i < galaxy.Civilizations.Count; i++)
                    if (galaxy.Civilizations[i].Id == playerId && galaxy.Civilizations[i].DevelopmentStage == CivilizationDevelopmentStage.PreWarp)
                        galaxy.Civilizations[i] = galaxy.Civilizations[i] with { DevelopmentStage = CivilizationDevelopmentStage.WarpCapable };
                return new(true, commandId == "unlock_research"
                    ? "Gameplay technology unlocked. Infrastructure still requires construction. Tools used is saved with this campaign."
                    : "Gameplay technology and empire projects unlocked for your Developer civilization.");
            default:
                advanceDays!(30);
                return new(true, "Advanced 30 days through the normal simulation. Tools used is saved with this campaign.");
        }
    }

    private static void FinishOrders(GalaxyState galaxy, int playerId)
    {
        var economy = galaxy.Economies.First(e => e.CivilizationId == playerId);
        var research = galaxy.Technologies.First(t => t.CivilizationId == playerId);
        if (research.ActiveResearchId is string researchId)
        {
            var remaining = Math.Max(0, TechnologyRegistry.Get(researchId).ResearchCost - research.ActiveResearchProgress);
            economy.Science = Math.Max(economy.Science, remaining);
            new ResearchSimulation().AdvanceForCivilization(galaxy, playerId);
        }
        var construction = new ConstructionSimulation();
        var demand = construction.GetIndustryDemand(galaxy, playerId);
        economy.Industry = Math.Max(economy.Industry, demand);
        // Large bounded construction duration lifts per-site rate limits; no calendar is advanced.
        construction.AdvanceForCivilization(galaxy, playerId, demand, 1_000_000);
        var shipbuilding = new ShipbuildingSimulation();
        var shipDemand = shipbuilding.GetIndustryDemand(galaxy, playerId);
        if (galaxy.ShipyardStates.First(s => s.CivilizationId == playerId).ActiveDesignId is not null)
        {
            // Keep the canonical population/cargo/fleet handoff. Only the active ship completes.
            economy.Industry = Math.Max(economy.Industry, shipDemand);
            shipbuilding.AdvanceForCivilization(galaxy, playerId, shipDemand, 1_000_000);
        }
    }
}
