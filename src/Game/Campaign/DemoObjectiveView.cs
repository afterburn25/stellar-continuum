using System;
using System.Linq;
using Game.Simulation;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;

namespace Game.Campaign;

public sealed record DemoObjectiveSnapshot(string Objective, string Research, string Construction);

/// <summary>Read-only guidance from the player's existing orders, prerequisites and production.</summary>
public static class DemoObjectiveView
{
    private static readonly string[] ResearchPriority = { "fusion_propulsion", "deep_space_sensors", "orbital_industry", "exotic_field_theory", "warp_field_control", "prototype_warp_drive" };
    private static readonly string[] ConstructionPriority = { "research_network", "industrial_automation", "orbital_launch_complex", "orbital_shipyard", "warp_test_facility" };

    public static DemoObjectiveSnapshot Build(
        GalaxyState galaxy,
        double requestedSpeed,
        AdaptiveResearchCampaignState? adaptiveResearch = null)
    {
        var player = galaxy.PlayerCivilizationId;
        var technology = galaxy.Technologies.Single(t => t.CivilizationId == player);
        var construction = galaxy.ConstructionStates.Single(t => t.CivilizationId == player);
        var economy = galaxy.Economies.Single(t => t.CivilizationId == player);
        var researchChoices = TechnologyRegistry.GetAvailable(technology, construction);
        var constructionSimulation = adaptiveResearch is null
            ? new ConstructionSimulation()
            : new ConstructionSimulation(new AdaptiveResearchConstructionCapabilityView(adaptiveResearch));
        var constructionChoices = constructionSimulation.GetAvailableProjects(galaxy, player);
        var researchNext = ResearchPriority.FirstOrDefault(id => researchChoices.Any(t => t.Id == id));
        var constructionNext = ConstructionPriority.FirstOrDefault(id => constructionChoices.Any(t => t.Id == id));
        var optionalExtraction = constructionChoices.FirstOrDefault(project => project.Id == "asteroid_resource_network");
        var currency = SovereignCurrencyCatalog.ForCivilization(galaxy, player);
        var researchText = adaptiveResearch is null
            ? technology.ActiveResearchId is string activeResearch
                ? $"Research: {TechnologyRegistry.Get(activeResearch).Name} · {technology.ActiveResearchProgress:0} science stored · progress depends on supplied science."
                : researchNext is not null ? $"Next research: {TechnologyRegistry.Get(researchNext).Name}. Choose it directly in Research."
                : technology.CompletedTechnologyIds.Contains("prototype_warp_drive") ? "Research path complete."
                : "Research waits for the required construction project."
            : AdaptiveResearchGuidance(galaxy, adaptiveResearch);
        var constructionText = construction.ActiveProjectId is string activeConstruction
            ? ConstructionGuidance(construction, economy, activeConstruction)
            : constructionNext is not null ? $"Next build: {ConstructionRegistry.Get(constructionNext).Name}. Choose it directly in Industry."
            : optionalExtraction is not null ?
                $"Optional build: {optionalExtraction.Name}. Add {optionalExtraction.IndustryPerDay:0.00} Industry/day for {currency.FormatRate(-optionalExtraction.UpkeepCreditsPerDay)} upkeep."
            : "Construction prerequisites complete; keep research running.";
        var ownFleets = galaxy.Fleets.Where(f => f.CivilizationId == player && f.IsActive).ToArray();
        var homeSystemId = galaxy.Civilizations.Single(c => c.Id == player).HomeSystemId;
        var hasExperimentalTransit = adaptiveResearch?.GetCivilization(player)
            .HasCapability(ShipbuildingCapabilityIds.ExperimentalInterstellarTransit)
            ?? technology.CompletedTechnologyIds.Contains("prototype_warp_drive");
        var objective = galaxy.Colonies.Any(c => c.CivilizationId == player && c.SystemId != homeSystemId)
            ? "Expedition complete: your first interstellar colony is established. Save or continue building your civilization."
            : !hasExperimentalTransit
                ? "Objective 1/3: achieve warp flight. Choose the next research program and construction project, then fast-forward at 8× during long waits. Pause to review funding, materials, and the next decision."
                : !ownFleets.Any(f => f.Role == FleetRole.Scout) || !ownFleets.Any(f => f.Role == FleetRole.Science) || !ownFleets.Any(f => f.Role == FleetRole.Colony)
                    ? "Objective 2/3: build a Pathfinder Scout, Science Vessel and Colony Ship in the shipyard."
                    : "Objective 3/3: scout a nearby star and complete its science survey. Select the physical Colony Ship and right-click the surveyed star. On arrival, open the system, select its Colony Ship icon, hover a surveyed world for its cost, then right-click that world to settle. Settlement charges its listed fees and completes on its timer; survey another star if none is suitable.";
        return new DemoObjectiveSnapshot(objective, researchText, constructionText);
    }

    private static string AdaptiveResearchGuidance(
        GalaxyState galaxy,
        AdaptiveResearchCampaignState campaign)
    {
        var civilization = galaxy.Civilizations.Single(value => value.Id == galaxy.PlayerCivilizationId);
        var economy = galaxy.Economies.Single(value => value.CivilizationId == civilization.Id);
        var state = campaign.GetCivilization(civilization.Id);
        var view = campaign.Runtime.Authority.Kernel.BuildView(state, $"species:{civilization.SpeciesId}");
        var active = view.ActiveProjects.FirstOrDefault(value => !value.Paused);
        if (active is not null)
        {
            var node = campaign.Runtime.Authority.Catalog.GetNode(active.NodeId);
            var quote = AdaptiveResearchFundingPolicy.Quote(node, active.AssignedEffectiveLabs,
                campaign.Runtime.Authority.Catalog);
            var funding = campaign.GetProjectFunding(civilization.Id).TryGetValue(active.NodeId, out var stateFunding)
                ? Math.Max(0.0, stateFunding.ReservedMilestoneCredits - stateFunding.ConsumedMilestoneCredits)
                : 0.0;
            var currency = SovereignCurrencyCatalog.ForCivilization(galaxy, civilization.Id);
            return $"Research: {node.Name} · {active.StageProgress:P0} through {active.Stage.ToString().ToLowerInvariant()} · " +
                $"{active.AssignedEffectiveLabs:0.#} labs · {currency.FormatRate(-quote.OperatingCreditsPerDay)} operating · " +
                $"{economy.LastResearchFundingFraction:P0} funded · {currency.Format(funding)} milestone reserve.";
        }

        var paused = view.ActiveProjects.FirstOrDefault(value => value.Paused);
        if (paused is not null)
        {
            var node = campaign.Runtime.Authority.Catalog.GetNode(paused.NodeId);
            return string.Equals(paused.PauseReason, "hypothesis_resolution_required", StringComparison.Ordinal)
                ? $"Research: {node.Name} is paused for hypothesis resolution. Resolve the hypothesis in Research before it can continue."
                : $"Research: {node.Name} is paused: {paused.PauseReason ?? "paused by order"}. Resume it in Research when its listed requirements are met.";
        }

        if (state.HasCapability("experimental_interstellar_transit"))
            return "Research path complete.";

        var candidate = view.VisibleNodes
            .Where(value => value.State == ResearchMaturity.Investigable && value.Blockers.Count == 0 && value.MinimumLabs is int minimum &&
                minimum <= view.DirectedProgramCapacity.FreeEffectiveLabs + 0.000001)
            .OrderBy(value => EarlyCampaignResearchPlan.Rank(value.NodeId))
            .ThenBy(value => campaign.Runtime.Authority.Catalog.GetNode(value.NodeId).GraphDepth)
            .ThenBy(value => value.DisplayName, StringComparer.Ordinal)
            .FirstOrDefault();
        return candidate is null
            ? "Research waits for new evidence, facilities or free laboratory capacity."
            : $"Next research: {candidate.DisplayName}. Choose it directly in Research.";
    }

    private static string ConstructionGuidance(
        ConstructionState construction,
        CivilizationEconomyState economy,
        string activeConstruction)
    {
        var project = ConstructionRegistry.Get(activeConstruction);
        var remaining = Math.Max(0.0, project.IndustryCost - construction.ActiveProjectProgress);
        var supply = economy.LastBaseOperationsFundingFraction < .999
            ? $"{economy.LastBaseOperationsFundingFraction:P0} operating funding; material delivery may be reduced"
            : $"≥{remaining / ConstructionSimulation.IndustryPerDay:0.0} game days at full material supply";
        return $"Construction: {project.Name} · {remaining:N0} materials remaining · {supply}.";
    }
}
