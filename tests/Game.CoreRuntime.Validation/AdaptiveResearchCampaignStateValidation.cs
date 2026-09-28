using Game.Campaign;
using Game.Simulation.Construction;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;
using Game.Simulation;

namespace Game.CoreRuntime.Validation;

internal static class AdaptiveResearchCampaignStateValidation
{
    public static void Run()
    {
        var root = AdaptiveResearchDataLocator.FindDataRoot();
        var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(root);
        var galaxy = new GalaxyGenerator().Generate(94217, new GalaxyGenerationSettings());
        var campaign = new AdaptiveResearchCampaignFactory(runtime).Create(galaxy);

        Require(campaign.Civilizations.Count == galaxy.Civilizations.Count,
            "not every campaign civilization received Adaptive Research state");
        foreach (var civilization in galaxy.Civilizations)
        {
            var state = campaign.GetCivilization(civilization.Id);
            var start = campaign.Starts[civilization.Id];
            Require(state.CivilizationId == $"civilization:{civilization.Id}",
                "research state used an unstable civilization identity");
            Require(start.SpeciesId == civilization.SpeciesId,
                "research start lost its owning species identity");
            Require(state.TotalEffectiveResearchLabs > 0 && state.NodeStates.Count > 0,
                "starting research history did not compose a playable state");
            Require(state.ApplicabilityContexts.ContainsKey($"species:{civilization.SpeciesId}"),
                "starting state omitted its primary species context");
        }

        var profileBySpecies = galaxy.Civilizations
            .GroupBy(value => value.SpeciesId)
            .ToDictionary(group => group.Key, group => campaign.Starts[group.First().Id].ReferenceProfileId);
        Require(profileBySpecies[SpeciesCatalog.TerranBaselineId] == AdaptiveResearchCampaignFactory.TerranProfileId,
            "Terran civilization did not receive the humanlike Sol history");
        Require(profileBySpecies[SpeciesCatalog.PelagicHighPressureId] == AdaptiveResearchCampaignFactory.PelagicProfileId,
            "pelagic civilization did not receive pressure/liquid-medium history");
        Require(profileBySpecies[SpeciesCatalog.CompactHighGravityId] == AdaptiveResearchCampaignFactory.HighGravityProfileId,
            "high-gravity civilization did not receive high-gravity history");
        Require(profileBySpecies[SpeciesCatalog.CryogenicHydrocarbonId] == AdaptiveResearchCampaignFactory.CryogenicHydrocarbonProfileId,
            "hydrocarbon civilization did not receive compatible biochemical history");

        VerifyAdaptiveGameplayPrerequisites(runtime);

        var pelagic = galaxy.Civilizations.First(value => value.SpeciesId == SpeciesCatalog.PelagicHighPressureId);
        var pelagicState = campaign.GetCivilization(pelagic.Id);
        var pelagicContext = $"species:{pelagic.SpeciesId}";
        Require(pelagicState.HasApplicabilityTrait(pelagicContext, "liquid_medium_native") &&
                pelagicState.GetPressure("high_pressure_environment") >= 28,
            "pelagic start omitted its defining medium and pressure conditions");

        var cryogenic = galaxy.Civilizations.First(value => value.SpeciesId == SpeciesCatalog.CryogenicHydrocarbonId);
        var cryogenicState = campaign.GetCivilization(cryogenic.Id);
        var cryogenicContext = $"species:{cryogenic.SpeciesId}";
        Require(cryogenicState.HasApplicabilityTrait(cryogenicContext, "hydrocarbon_solvent_biology") &&
                !cryogenicState.HasApplicabilityTrait(cryogenicContext, "water_solvent_biology"),
            "cryogenic start inherited incompatible water-based biology");

        var playerId = galaxy.PlayerCivilizationId;
        var player = campaign.GetCivilization(playerId);
        var startingPlayerLabs = player.TotalEffectiveResearchLabs;
        var legacyStep = new GalaxySimulationStepCoordinator(advanceLegacyResearch: false)
            .Advance(galaxy, 1);
        Require(legacyStep.ResearchEvents.Count == 0 &&
                galaxy.Technologies.All(value => value.ActiveResearchId is null),
            "integrated campaign advanced the retired linear research loop");
        var aiCivilization = galaxy.Civilizations.First(value => !value.IsPlayer && !value.IsSeededAncient);
        var aiState = campaign.GetCivilization(aiCivilization.Id);
        Require(aiState.ActiveProjects.Count == 0 && player.ActiveProjects.Count == 0,
            "fresh Adaptive Research campaign invented active work");
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 1, currentSimulationDay: 1);
        Require(aiState.ActiveProjects.Values.Any(value => !value.Paused) && player.ActiveProjects.Count == 0,
            "Adaptive Research AI did not choose work or auto-selected for the player");
        galaxy.ConstructionStates.Single(value => value.CivilizationId == playerId)
            .CompletedProjectIds.Add("research_network");
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 1, currentSimulationDay: 2);
        Require(Math.Abs(player.TotalEffectiveResearchLabs -
                         (startingPlayerLabs + AdaptiveResearchCampaignSimulation.PlanetaryResearchNetworkLabCount)) < 0.000001,
            "completed Planetary Research Network did not add its physical Effective Research Labs");
        var networkLabs = player.Expertise.Institutions.Values.Count(value =>
            value.InstitutionInstanceId == "construction:research_network");
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 1, currentSimulationDay: 3);
        Require(player.Expertise.Institutions.Values.Count(value =>
                    value.InstitutionInstanceId == "construction:research_network") == networkLabs &&
                Math.Abs(player.TotalEffectiveResearchLabs -
                         (startingPlayerLabs + AdaptiveResearchCampaignSimulation.PlanetaryResearchNetworkLabCount)) < 0.000001,
            "Planetary Research Network duplicated laboratory capacity on a later simulation step");
        var homeColony = galaxy.Colonies.First(value =>
            value.CivilizationId == playerId && value.SystemId ==
            galaxy.Civilizations.Single(civilization => civilization.Id == playerId).HomeSystemId);
        homeColony.SurfaceBuildings.Add(CompletedSurfaceBuilding(1, "power_generator"));
        homeColony.SurfaceBuildings.Add(CompletedSurfaceBuilding(2, "science_lab"));
        homeColony.SurfaceBuildings.Add(CompletedSurfaceBuilding(3, "science_lab"));
        homeColony.SurfaceBuildings.Add(CompletedSurfaceBuilding(4, "science_lab"));
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 1, currentSimulationDay: 4);
        Require(Math.Abs(player.TotalEffectiveResearchLabs - (startingPlayerLabs + 4 + 3.75)) < 0.000001,
            "three powered surface labs did not add their exact research-district capacity");
        homeColony.SurfaceBuildings.Single(value => value.Id == 2).TypeId = "advanced_science_lab";
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 1, currentSimulationDay: 5);
        Require(Math.Abs(player.TotalEffectiveResearchLabs - (startingPlayerLabs + 4 + 4.375)) < 0.000001,
            "powered upgraded campus did not replace its base-lab capacity or respect the power budget");
        homeColony.SurfaceBuildings.Remove(homeColony.SurfaceBuildings.Single(value => value.Id == 1));
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 1, currentSimulationDay: 6);
        Require(Math.Abs(player.TotalEffectiveResearchLabs - (startingPlayerLabs + 4 + 1.25)) < 0.000001 &&
                player.Expertise.Institutions.Values.Count(value =>
                    value.InstitutionInstanceId.StartsWith("construction:surface:", StringComparison.Ordinal)) == 1,
            "unpowered surface labs retained phantom research capacity");
        homeColony.SurfaceBuildings.Clear();
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 1, currentSimulationDay: 7);
        Require(Math.Abs(player.TotalEffectiveResearchLabs - (startingPlayerLabs + 4)) < 0.000001 &&
                player.Expertise.Institutions.Values.All(value =>
                    !value.InstitutionInstanceId.StartsWith("construction:surface:", StringComparison.Ordinal)),
            "demolished surface labs retained research institutions");
        Require(runtime.Authority.StartDirectedResearch(player, "fusion_power", 6).Accepted,
            "player could not start a visible Adaptive Research program");
        galaxy.Economies.Single(value => value.CivilizationId == playerId).Credits = 1_000_000;
        var events = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 36525, currentSimulationDay: 36525);
        Require(events.Any(value => value.CivilizationId == playerId && value.NodeId == "fusion_power") &&
                player.HasEstablishedKnowledge("fusion_power"),
            "campaign time did not advance Adaptive Research to mature knowledge");
        Require(!player.HasCapability("orbital_industry") &&
                player.HasEstablishedKnowledge("deep_space_radar"),
            "Adaptive Research skipped the intended Orbital Manufacturing gate or lost mature sensor knowledge");
        Require(runtime.Authority.StartDirectedResearch(player, "fusion_propulsion", 6).Accepted,
            "mature fusion power did not expose the propulsion program");
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 36525, currentSimulationDay: 73050);
        Require(player.HasEstablishedKnowledge("fusion_propulsion"),
            "mature Adaptive fusion propulsion was not retained as established knowledge");
        Require(runtime.Authority.StartDirectedResearch(player, "in_space_assembly", 6).Accepted,
            "starting orbital history did not expose In-Space Assembly");
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, campaign, elapsedDays: 36525, currentSimulationDay: 109575);
        Require(player.HasEstablishedKnowledge("in_space_assembly") &&
                !player.HasCapability("orbital_industry"),
            "In-Space Assembly incorrectly bypassed the Orbital Manufacturing capability gate");

        VerifyPlayableWarpPath(runtime);
    }

    private static SurfaceBuildingState CompletedSurfaceBuilding(int id, string typeId)
    {
        var definition = SurfaceBuildingCatalog.Find(typeId)
            ?? throw new InvalidOperationException($"Missing surface-building definition '{typeId}'.");
        return new SurfaceBuildingState
        {
            Id = id,
            TypeId = typeId,
            X = 80 * id,
            Z = 80,
            IndustryProgress = definition.IndustryCost,
            IsComplete = true,
        };
    }

    private static void VerifyAdaptiveGameplayPrerequisites(AdaptiveResearchStrategicRuntime runtime)
    {
        var galaxy = new GalaxyGenerator().Generate(80641, new GalaxyGenerationSettings());
        var campaign = new AdaptiveResearchCampaignFactory(runtime).Create(galaxy);
        var playerId = galaxy.PlayerCivilizationId;
        var adaptive = campaign.GetCivilization(playerId);
        var legacy = galaxy.Technologies.Single(value => value.CivilizationId == playerId);
        legacy.CompletedTechnologyIds.Add("orbital_industry");
        legacy.CompletedTechnologyIds.Add("prototype_warp_drive");

        var constructionState = galaxy.ConstructionStates.Single(value => value.CivilizationId == playerId);
        constructionState.CompletedProjectIds.Add("orbital_launch_complex");
        var construction = new ConstructionSimulation(
            new AdaptiveResearchConstructionCapabilityView(campaign));
        var shipyardProject = ConstructionRegistry.Get("orbital_shipyard");
        var lockReason = construction.GetLockReason(galaxy, playerId, shipyardProject);
        Require(lockReason?.Contains("Orbital Industry", StringComparison.Ordinal) == true,
            "integrated construction accepted a retired legacy technology flag");
        var warpFacility = ConstructionRegistry.Get("warp_test_facility");
        Require(construction.GetLockReason(galaxy, playerId, warpFacility)?
                .Contains("Warp Field Control", StringComparison.Ordinal) == true,
            "integrated warp construction accepted a retired legacy technology flag");

        runtime.Authority.AddCapability(adaptive, "orbital_industry");
        Require(construction.GetLockReason(galaxy, playerId, shipyardProject) is null,
            "Adaptive orbital-industry capability did not unlock orbital construction");

        constructionState.CompletedProjectIds.Add("orbital_shipyard");
        var shipbuilding = new ShipbuildingSimulation(
            new AdaptiveResearchShipbuildingCapabilityView(campaign));
        var scout = ShipDesignRegistry.Get("warp_scout");
        lockReason = shipbuilding.GetLockReason(galaxy, playerId, scout);
        Require(lockReason?.Contains("Experimental Interstellar Transit", StringComparison.Ordinal) == true &&
                !lockReason.Contains("Spacecraft Construction", StringComparison.Ordinal),
            "shipyard did not derive construction capability from Adaptive orbital industry or trusted a legacy warp flag");

        runtime.Authority.AddCapability(adaptive, "experimental_interstellar_transit");
        Require(shipbuilding.GetLockReason(galaxy, playerId, scout) is null,
            "Adaptive interstellar-transit capability did not unlock first-generation ship designs");
    }

    private static void VerifyPlayableWarpPath(AdaptiveResearchStrategicRuntime runtime)
    {
        var galaxy = new GalaxyGenerator().Generate(20260908, new GalaxyGenerationSettings());
        var campaign = new AdaptiveResearchCampaignFactory(runtime).Create(galaxy);
        var civilization = galaxy.Civilizations.Single(value => value.Id == galaxy.PlayerCivilizationId);
        var state = campaign.GetCivilization(civilization.Id);
        var construction = galaxy.ConstructionStates.Single(value => value.CivilizationId == civilization.Id);
        // This benchmark isolates research-path pacing; dedicated funding validation covers
        // treasury exhaustion and partial funding behavior.
        galaxy.Economies.Single(value => value.CivilizationId == civilization.Id).Credits = 1_000_000;
        var simulation = new AdaptiveResearchCampaignSimulation();
        var elapsedDays = 0.0;

        const double pacingStepDays = 5;
        const double researchNetworkCompletionDay = 125;
        const double maximumPacingDays = 365.25 * 20;
        while (!state.HasCapability("experimental_interstellar_transit") &&
               elapsedDays < maximumPacingDays)
        {
            if (elapsedDays >= researchNetworkCompletionDay &&
                !construction.CompletedProjectIds.Contains("research_network"))
                construction.CompletedProjectIds.Add("research_network");
            var view = runtime.Authority.Kernel.BuildView(state, $"species:{civilization.SpeciesId}");
            if (state.ActiveProjects.Count > 0)
            {
                elapsedDays += pacingStepDays;
                _ = simulation.Advance(galaxy, campaign, pacingStepDays, elapsedDays);
                continue;
            }
            if (state.HasEstablishedKnowledge("warp_field_control") &&
                !construction.CompletedProjectIds.Contains("warp_test_facility"))
            {
                var blockedPrototype = view.VisibleNodes.Single(value => value.NodeId == "prototype_warp_drive");
                Require(blockedPrototype.Blockers.Any(value => value.Code == ResearchBlockerCode.MissingFacilityCapability),
                    "Prototype Warp bypassed its physical test-facility requirement");
                construction.CompletedProjectIds.Add("warp_test_facility");
                elapsedDays += 1;
                _ = simulation.Advance(galaxy, campaign, 1, elapsedDays);
                continue;
            }
            var next = EarlyCampaignResearchPlan.WarpCapabilityPath
                .Select(id => view.VisibleNodes.FirstOrDefault(value => value.NodeId == id))
                .FirstOrDefault(value => value is { State: ResearchMaturity.Investigable } &&
                    value.Blockers.Count == 0);
            Require(next is not null,
                "playable Adaptive Research path stalled before experimental interstellar transit: " +
                string.Join("; ", EarlyCampaignResearchPlan.WarpCapabilityPath.Select(id =>
                {
                    var visible = view.VisibleNodes.FirstOrDefault(value => value.NodeId == id);
                    return visible is null ? $"{id}=unknown" :
                        $"{id}={visible.State}[{string.Join(',', visible.Blockers.Select(value => value.Code))}]";
                })));
            var node = runtime.Authority.Catalog.GetNode(next!.NodeId);
            var labs = Math.Min(node.ProjectRequirements.RecommendedLabs, state.FreeEffectiveLabs);
            Console.WriteLine($"ADAPTIVE WARP START: day {elapsedDays:0.#}, {node.Name}, {labs:0.#} labs, {node.ProjectRequirements.BaseResearchPoints:0} RP");
            Require(runtime.Authority.StartDirectedResearch(
                    state, node.Id, labs, targetApplicabilityContextId: next.TargetApplicabilityContextId).Accepted,
                $"playable Adaptive Research path could not start {node.Name}");
        }

        var activeAtLimit = state.ActiveProjects.Values.FirstOrDefault();
        Console.WriteLine($"ADAPTIVE WARP PACING: {elapsedDays:0.#} simulation days ({elapsedDays / 365.25:0.0} years); active={activeAtLimit?.NodeId ?? "none"}; progress={activeAtLimit?.TotalResearchPoints ?? 0:0}/{(activeAtLimit is null ? 0 : runtime.Authority.Catalog.GetNode(activeAtLimit.NodeId).ProjectRequirements.BaseResearchPoints):0}");
        Require(state.HasCapability("experimental_interstellar_transit") &&
                state.HasCapability("orbital_industry") &&
                state.HasCapability("spacecraft_construction") &&
                galaxy.Civilizations.Single(value => value.Id == civilization.Id).DevelopmentStage ==
                    CivilizationDevelopmentStage.WarpCapable,
            "playable Adaptive Research path did not materialize its orbital, spacecraft and warp capabilities");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
