using Game.Campaign;
using Game.Presentation;
using Game.Simulation;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Industry;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

namespace Game.CoreRuntime.Validation;

internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Contains("--full-galaxy", StringComparer.Ordinal))
        {
            try { FullGalaxyPopulationValidation.Run(); return 0; }
            catch (Exception ex) { Game.Validation.RegressionRunner.Report("full-galaxy population", ex); return 1; }
        }
        if (args.Contains("--home-distance-reference", StringComparer.Ordinal))
        {
            try { HomeDistanceReferenceValidation.Run(); return 0; }
            catch (Exception ex) { Game.Validation.RegressionRunner.Report("homeworld distance reference", ex); return 1; }
        }
        if (args.Contains("--refresh-rate", StringComparer.Ordinal))
        {
            try { RefreshRateValidation.Run(); return 0; }
            catch (Exception ex) { Game.Validation.RegressionRunner.Report("automatic Windows refresh", ex); return 1; }
        }
        if (args.Contains("--prepared-save", StringComparer.Ordinal))
        {
            try
            {
                PreparedCampaignSaveValidation.Run();
                CampaignBackupRecoveryValidation.Run();
                CampaignV9DiplomacyPersistenceValidation.RunCampaignV9DiplomacyPersistenceChecks();
                AdaptiveResearchCampaignPersistenceValidation.Run();
                DeveloperModeValidation.ValidateDeveloperSaveContinuity();
                return 0;
            }
            catch (Exception ex) { Game.Validation.RegressionRunner.Report("prepared campaign save", ex); return 1; }
        }

        var tests = new (string Name, Action Run)[]
        {
            ("prepared campaign saves are detached, atomic and ordered", PreparedCampaignSaveValidation.Run),
            ("automatic Windows refresh lifecycle and mode filtering", RefreshRateValidation.Run),
            ("nearby 500-star catalogue campaign", NearbyCatalogValidation.Run),
            ("full-galaxy sizes, population and persistence", FullGalaxyPopulationValidation.Run),
            ("homeworld distance references remain physical and privacy-safe", HomeDistanceReferenceValidation.Run),
            ("balanced fair industry allocation", ValidateBalancedFairAllocation),
            ("player industry priority persists and reflows scarce materials", IndustryPriorityValidation.Run),
            ("weighted industry allocation", ValidateWeightedAllocation),
            ("zero-time simulation step is mutation-free", ValidateZeroTimeMutationFree),
            ("Adaptive campaign does not bank retired Science currency", ValidateAdaptiveEconomyDoesNotAccrueLegacyScience),
            ("Adaptive Research consumes funding and stalls cleanly without it", AdaptiveResearchFundingValidation.Run),
            ("idle Industry respects physical storage capacity", ValidateIndustryStorageCapacity),
            ("mature homeworld supports an opening expansion fleet", ValidateOpeningFleetAffordability),
            ("civilizations expose distinct sovereign currencies", ValidateSovereignCurrencies),
            ("civilian tax revenue is backed by represented employment", ValidateLaborBackedTaxBase),
            ("treasury runway distinguishes surplus, deficit and depletion", ValidateTreasuryHealth),
            ("unpaid operations accrue and recover as treasury arrears", ValidateOperatingArrears),
            ("unfunded operations stop free industrial and science output", ValidateUnderfundedProduction),
            ("coordinator budgets construction and shipbuilding", ValidateCoordinatorIndustryBudgeting),
            ("shipyard reports exact missing capabilities and facility", ValidateShipyardRequirementDiagnostics),
            ("player notification feed stays bounded and ordered", ValidatePlayerNotificationFeed),
            ("coordinator executes authoritative combat", ValidateCoordinatorCombat),
            ("strategic AI drives bounded Core industry priorities", StrategicAiRuntimeValidation.Run),
            ("campaign session lifecycle and recovery", ValidateCampaignSessionLifecycle),
            ("startup initialization failures retain actionable diagnostics", StartupInitializationFailureValidation.Run),
            ("authoritative planetary catalogs survive and reject malformed saves", PlanetaryCatalogPersistenceValidation.Run),
            ("leadership replacement and voice metadata persist independently", LeadershipPersistenceValidation.Run),
            ("Sandbox seed setup is deterministic and persists", SandboxGenerationSetupValidation.Run),
            ("new Player campaign reaches a real surveyed settlement through Adaptive Research", DemoProgressionValidation.Run),
            ("ordinary Player Sandbox reaches a real surveyed settlement through Adaptive Research", DemoProgressionValidation.RunPlayerSandbox),
            ("Developer accelerated clock reaches the same settlement within five active minutes", DemoProgressionValidation.RunDemo),
            ("demo configuration clock and separate-save continuity", PlayableDemoValidation.Run),
            ("human Earth origin and canonical Sol save continuity", SolStartingWorldValidation.Run),
            ("every species receives compatible Adaptive Research campaign state", AdaptiveResearchCampaignStateValidation.Run),
            ("Adaptive Research campaign state persists and v13 migrates", AdaptiveResearchCampaignPersistenceValidation.Run),
            ("surface free placement authority and rejection", SurfaceConstructionValidation.ValidateFreePlacementAndAuthority),
            ("surface shortage feedback remains read-only and reserve-aware", SurfaceConstructionValidation.ValidateSurfaceFeedbackReadModel),
            ("surface cancellation and demolition authority", SurfaceConstructionValidation.ValidateRemovalAuthorityAndEffects),
            ("surface buildings can shut down and restart without free upkeep", SurfaceConstructionValidation.ValidateOperatingShutdown),
            ("surface underfunding causes repairable physical wear", SurfaceConstructionValidation.ValidatePhysicalMaintenanceAndRepair),
            ("surface grid protects essential services with player override", SurfaceConstructionValidation.ValidateEssentialServicePriority),
            ("surface grid storage charges, discharges and persists", SurfaceConstructionValidation.ValidatePowerStorage),
            ("surface hub upgrades control module capacity and persist", SurfaceConstructionValidation.ValidateHubCapacityAndUpgrade),
            ("surface upgrade authority, economy and save continuity", SurfaceConstructionValidation.ValidateUpgradeAuthorityAndEffects),
            ("surface district specialization follows completed complexes", SurfaceConstructionValidation.ValidateDerivedSpecialization),
            ("surface habitats reduce exact-world life-support costs", SurfaceConstructionValidation.ValidateHabitatSupportInfrastructure),
            ("orbital extraction has prerequisites, output, upkeep and logistics", OrbitalEconomyValidation.Run),
            ("surface rate budget and pause", SurfaceConstructionValidation.ValidateRateBudgetAndPause),
            ("surface and regular project share industry", SurfaceConstructionValidation.ValidateSharedConstructionBudget),
            ("surface construction is independent of frame partition", SurfaceConstructionValidation.ValidateFramePartitionIndependence),
            ("surface power feeds authoritative economy", SurfaceConstructionValidation.ValidatePowerAndEconomy),
            ("surface output requires represented workforce", SurfaceConstructionValidation.ValidateWorkforceLimitsOutput),
            ("surface positions and progress survive save resume", SurfaceConstructionValidation.ValidateSaveContinuity),
            ("invalid surface saves fail closed", SurfaceConstructionValidation.ValidateInvalidSurfaceSaves),
            ("Developer opening retains ordinary rules without automatic grants", DeveloperModeValidation.ValidateUnmodifiedOpening),
            ("Player persistence rejects all Developer provenance", DeveloperModeValidation.ValidatePlayerSaveBoundary),
            ("Developer commands enforce mode and observer isolation", DeveloperModeValidation.ValidateCommandAuthorityAndIsolation),
            ("finish orders affects only the owning civilization", DeveloperModeValidation.ValidateFinishOrdersScope),
            ("already-paid restored orders complete without charging or losing queued population", DeveloperModeValidation.ValidateAlreadyPaidOrderCompletion),
            ("Developer surface progress and provenance survive save recovery", DeveloperModeValidation.ValidateDeveloperSaveContinuity),
            ("malformed Developer envelopes fail closed", DeveloperModeValidation.ValidateInvalidEnvelopes),
            ("legacy demo import preserves originals and newer Developer state", DeveloperModeValidation.ValidateLegacyImportIsolation),
            ("civilian holds retain physical routes and paid missions", CivilianFleetHoldOrderValidation.Run),
        };

        var failures = Game.Validation.RegressionRunner.Run(typeof(Program).Assembly);
        foreach (var test in tests)
        {
            try
            {
                test.Run();
                Console.WriteLine($"PASS: {test.Name}");
            }
            catch (Exception ex)
            {
                failures++;
                Game.Validation.RegressionRunner.Report(test.Name, ex);
            }
        }

        Console.WriteLine($"Core runtime validation: {tests.Length + Game.Validation.RegressionRunner.Count - failures}/{tests.Length + Game.Validation.RegressionRunner.Count} passed.");
        return failures == 0 ? 0 : 1;
    }

    private static void ValidateBalancedFairAllocation()
    {
        var policy = new WeightedFairIndustryAllocationPolicy();
        var allocation = policy.Allocate(new IndustryAllocationContext(
            CivilizationId: 4,
            AvailableIndustry: 100.0,
            ConstructionDemand: 80.0,
            ShipbuildingDemand: 40.0));

        RequireNear(allocation.ConstructionAllocated, 60.0, "construction did not receive reflowed fair capacity");
        RequireNear(allocation.ShipbuildingAllocated, 40.0, "shipbuilding demand was not fully satisfied");
        RequireNear(allocation.TotalAllocated, 100.0, "available Industry was not fully allocated");
        RequireNear(allocation.ConstructionWeight, 1.0, "balanced construction weight changed");
        RequireNear(allocation.ShipbuildingWeight, 1.0, "balanced shipbuilding weight changed");
    }

    private static void ValidateWeightedAllocation()
    {
        var policy = new WeightedFairIndustryAllocationPolicy(new FixedIndustryPriorityProvider(2.0, 1.0));
        var allocation = policy.Allocate(new IndustryAllocationContext(
            CivilizationId: 7,
            AvailableIndustry: 90.0,
            ConstructionDemand: 200.0,
            ShipbuildingDemand: 200.0));

        RequireNear(allocation.ConstructionAllocated, 60.0, "2:1 construction priority did not receive two thirds of constrained Industry");
        RequireNear(allocation.ShipbuildingAllocated, 30.0, "2:1 shipbuilding priority did not receive one third of constrained Industry");
        RequireNear(allocation.TotalAllocated, 90.0, "weighted allocation lost Industry");
    }

    private static void ValidateOpeningFleetAffordability()
    {
        var galaxy = CreateGalaxy();
        var playerId = galaxy.PlayerCivilizationId;
        var home = galaxy.Colonies.Where(colony => colony.CivilizationId == playerId)
            .MaxBy(colony => colony.PopulationMillions)!;
        home.PopulationMillions = 10_000.0;
        var system = galaxy.Systems.Single(item => item.Id == home.SystemId);
        var roles = new[] { FleetRole.Scout, FleetRole.Science, FleetRole.Colony };
        foreach (var role in roles)
        {
            galaxy.Fleets.Add(new FleetState
            {
                Id = galaxy.Fleets.Count == 0 ? 1 : galaxy.Fleets.Max(item => item.Id) + 1,
                CivilizationId = playerId,
                Name = $"Opening {role}",
                Role = role,
                Position = system.Position,
                CurrentSystemId = system.Id,
            });
        }

        var construction = galaxy.ConstructionStates.Single(state => state.CivilizationId == playerId);
        foreach (var project in new[] { "orbital_launch_complex", "orbital_shipyard", "warp_test_facility" })
            construction.CompletedProjectIds.Add(project);
        var flow = EconomySimulation.GetCreditFlow(galaxy, playerId, includeResearchOperations: false);
        Require(flow.NetCreditsPerDay > 0.0,
            $"opening scout, science, and colony fleet deadlocked the mature homeworld economy ({flow.NetCreditsPerDay:0.###} C/day)");
    }

    private static void ValidateSovereignCurrencies()
    {
        var speciesIds = new[]
        {
            SpeciesCatalog.TerranBaselineId,
            SpeciesCatalog.PelagicHighPressureId,
            SpeciesCatalog.CompactHighGravityId,
            SpeciesCatalog.CryogenicHydrocarbonId,
        };
        var currencies = speciesIds.Select(SovereignCurrencyCatalog.ForSpecies).ToArray();
        Require(currencies.Select(value => value.Name).Distinct(StringComparer.Ordinal).Count() == speciesIds.Length,
            "species shared a sovereign currency name");
        Require(currencies.Select(value => value.Code).Distinct(StringComparer.Ordinal).Count() == speciesIds.Length,
            "species shared a sovereign currency code");
        Require(currencies.Select(value => value.LocalUnitsPerBudgetUnit).Distinct().Count() == speciesIds.Length,
            "species shared a local denomination scale");

        var human = SovereignCurrencyCatalog.ForSpecies(SpeciesCatalog.TerranBaselineId);
        Require(human.Format(500.0) == "$5B UED", $"opening Human treasury was not $5B UED: {human.Format(500.0)}");
        Require(!currencies.Select(value => value.Format(500.0)).Any(value =>
                value.Contains("Credit", StringComparison.OrdinalIgnoreCase)),
            "an opening sovereign balance exposed the future interstellar Credit");
    }

    private static void ValidateLaborBackedTaxBase()
    {
        var galaxy = CreateGalaxy();
        var playerId = galaxy.PlayerCivilizationId;
        var colony = galaxy.Colonies.First(value => value.CivilizationId == playerId && value.Kind == SettlementKind.Colony);
        colony.PopulationMillions = 1_000.0;
        colony.Infrastructure = 1.0;
        colony.Stability = 1.0;
        var baseline = ColonyLaborEconomy.GetSnapshot(colony);
        RequireNear(baseline.WorkingAgePopulationMillions, 450.0, "working-age population was not bounded");
        RequireNear(baseline.EmploymentRate, ColonyLaborEconomy.BaselineEmploymentRate,
            "baseline employment rate changed");
        var flow = EconomySimulation.GetCreditFlow(galaxy, playerId, includeResearchOperations: false);
        Require(flow.ColonyRevenuePerDay > 0.0, "employed population produced no tax revenue");

        colony.Infrastructure = 0.1;
        var constrained = ColonyLaborEconomy.GetSnapshot(colony);
        Require(constrained.EmploymentRate < baseline.EmploymentRate,
            "weak infrastructure did not reduce employment capacity");
        var newJobs = ColonyLaborEconomy.GetSnapshot(colony, additionalRepresentedJobsMillions: 10.0);
        RequireNear(newJobs.EmployedPopulationMillions, constrained.EmployedPopulationMillions + 10.0,
            "represented surface jobs did not increase employment");
    }

    private static void ValidateTreasuryHealth()
    {
        var surplus = TreasuryHealth.Assess(100.0, 2.0);
        Require(surplus.State == TreasuryHealthState.Surplus && double.IsPositiveInfinity(surplus.RunwayDays),
            "surplus treasury reported finite runway");
        var deficit = TreasuryHealth.Assess(100.0, -4.0);
        Require(deficit.State == TreasuryHealthState.Deficit, "funded deficit reported wrong state");
        RequireNear(deficit.RunwayDays, 25.0, "deficit runway was incorrect");
        Require(TreasuryHealth.Assess(0.0, -1.0).State == TreasuryHealthState.Depleted,
            "empty deficit treasury did not report depletion");
        Require(TreasuryHealth.Assess(0.0, -1.0, 5.0).State == TreasuryHealthState.Arrears,
            "unpaid obligations did not supersede the generic depleted state");
    }

    private static void ValidateOperatingArrears()
    {
        var galaxy = CreateGalaxy();
        var playerId = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(value => value.CivilizationId == playerId);
        foreach (var colony in galaxy.Colonies.Where(value => value.CivilizationId == playerId))
            colony.PopulationMillions = 0.001;
        economy.Credits = 0.0;

        new EconomySimulation().Advance(galaxy, 1.0, accrueLegacyScience: false);
        Require(economy.Credits == 0.0 && economy.OperatingArrears > 0.0,
            "unfunded base operations disappeared at an empty treasury");
        Require(economy.LastBaseOperationsFundingFraction < 1.0,
            "unfunded base operations reported full payment coverage");

        var arrears = economy.OperatingArrears;
        economy.Credits = arrears + 100.0;
        new EconomySimulation().Advance(galaxy, 1.0, accrueLegacyScience: false);
        RequireNear(economy.OperatingArrears, 0.0, "restored treasury did not clear operating arrears");
        Require(economy.Credits < 100.0,
            "arrears and current obligations were not paid before reserves rebuilt");
    }

    private static void ValidateUnderfundedProduction()
    {
        var galaxy = CreateGalaxy();
        var playerId = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(value => value.CivilizationId == playerId);
        foreach (var colony in galaxy.Colonies.Where(value => value.CivilizationId == playerId))
            colony.PopulationMillions = 0.001;
        economy.Credits = 0.0;
        economy.Industry = 0.0;
        economy.Science = 0.0;

        var simulation = new EconomySimulation();
        simulation.Advance(galaxy, 1.0, accrueLegacyScience: true);
        RequireNear(economy.Industry, 0.0, "unfunded civilization created free Industry");
        RequireNear(economy.Science, 0.0, "unfunded civilization created free Science");
        RequireNear(economy.LastIndustryPerSecond, 0.0, "unfunded Industry rate remained positive");
        RequireNear(economy.LastSciencePerSecond, 0.0, "unfunded Science rate remained positive");

        economy.Credits = economy.OperatingArrears + 100.0;
        simulation.Advance(galaxy, 1.0, accrueLegacyScience: true);
        Require(economy.LastBaseOperationsFundingFraction > 0.999999,
            "funded recovery did not restore base operations");
        Require(economy.Industry > 0.0 && economy.Science > 0.0,
            "funded recovery did not restore industrial and science output");
    }

    private static void ValidateShipyardRequirementDiagnostics()
    {
        var galaxy = CreateGalaxy();
        var playerId = galaxy.PlayerCivilizationId;
        var shipbuilding = new ShipbuildingSimulation();
        var scout = ShipDesignRegistry.Get("warp_scout");
        var reason = shipbuilding.GetLockReason(galaxy, playerId, scout);
        Require(reason is not null && reason.Contains("Spacecraft Construction", StringComparison.Ordinal) &&
            reason.Contains("Experimental Interstellar Transit", StringComparison.Ordinal) &&
            reason.Contains("Orbital Shipyard", StringComparison.Ordinal),
            $"shipyard lock reason omitted an exact requirement: {reason}");
        var rejected = shipbuilding.StartBuild(galaxy, playerId, scout.Id);
        Require(!rejected.Accepted && rejected.Message.Contains(scout.Name, StringComparison.Ordinal) &&
            rejected.Message.Contains("Spacecraft Construction", StringComparison.Ordinal) &&
            rejected.Message.Contains("Experimental Interstellar Transit", StringComparison.Ordinal) &&
            rejected.Message.Contains("Orbital Shipyard", StringComparison.Ordinal),
            $"rejected ship order was not actionable: {rejected.Message}");
    }

    private static void ValidatePlayerNotificationFeed()
    {
        var feed = new PlayerNotificationFeed();
        for (var index = 0; index < PlayerNotificationFeed.MaxItems + 5; index++)
            feed.Publish("Research", $"2050-01-{index + 1:00}", $"Event {index}");
        Require(feed.Items.Count == PlayerNotificationFeed.MaxItems,
            "player notification history exceeded its bound");
        Require(feed.Items[0].Message == "Event 5" && feed.Items[^1].Message == "Event 36" &&
            feed.Items.Zip(feed.Items.Skip(1), (left, right) => right.Sequence > left.Sequence).All(value => value),
            "player notification history did not retain the newest events in sequence");
        feed.Clear();
        Require(feed.Items.Count == 0, "player notification history survived a campaign reset");
        feed.Publish("Colony", "2051-01-01", "New session event");
        Require(feed.Items[0].Sequence == PlayerNotificationFeed.MaxItems + 6,
            "player notification sequence reset and could hide new-session unread events");
    }

    private static void ValidateZeroTimeMutationFree()
    {
        var galaxy = CreateGalaxy();
        var playerId = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.First(state => state.CivilizationId == playerId);
        var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == playerId);
        var shipyard = galaxy.ShipyardStates.First(state => state.CivilizationId == playerId);
        var research = galaxy.Technologies.First(state => state.CivilizationId == playerId);

        construction.ActiveProjectId = ConstructionRegistry.All.First().Id;
        construction.ActiveProjectProgress = 3.0;
        shipyard.ActiveDesignId = ShipDesignRegistry.All.First().Id;
        shipyard.ActiveBuildProgress = 2.0;
        research.ActiveResearchId = TechnologyRegistry.All.First().Id;
        research.ActiveResearchProgress = 4.0;
        economy.Industry = 17.0;
        economy.Science = 19.0;

        var beforeIndustry = economy.Industry;
        var beforeScience = economy.Science;
        var beforeConstruction = construction.ActiveProjectProgress;
        var beforeShipbuilding = shipyard.ActiveBuildProgress;
        var beforeResearch = research.ActiveResearchProgress;
        var beforeFleets = galaxy.Fleets.Count;
        var beforeColonies = galaxy.Colonies.Count;

        var result = new GalaxySimulationStepCoordinator().Advance(galaxy, 0.0);

        Require(result.SimulationDays == 0.0, "zero-time result reported simulation progress");
        Require(result.IndustryAllocations.Count == 0, "zero-time step performed Industry allocation");
        Require(result.ConstructionEvents.Count == 0 && result.ShipbuildingEvents.Count == 0 && result.ResearchEvents.Count == 0,
            "zero-time step emitted production/research events");
        Require(result.CombatEvents.Count == 0, "zero-time step emitted combat events");
        RequireNear(economy.Industry, beforeIndustry, "zero-time step changed Industry");
        RequireNear(economy.Science, beforeScience, "zero-time step changed Science");
        RequireNear(construction.ActiveProjectProgress, beforeConstruction, "zero-time step advanced construction");
        RequireNear(shipyard.ActiveBuildProgress, beforeShipbuilding, "zero-time step advanced shipbuilding");
        RequireNear(research.ActiveResearchProgress, beforeResearch, "zero-time step advanced research");
        Require(galaxy.Fleets.Count == beforeFleets, "zero-time step changed fleet count");
        Require(galaxy.Colonies.Count == beforeColonies, "zero-time step changed colony count");
    }

    private static void ValidateCoordinatorIndustryBudgeting()
    {
        var galaxy = CreateGalaxy();
        var playerId = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.First(state => state.CivilizationId == playerId);
        var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == playerId);
        var shipyard = galaxy.ShipyardStates.First(state => state.CivilizationId == playerId);

        var constructionDefinition = ConstructionRegistry.All.OrderByDescending(definition => definition.IndustryCost).First();
        var shipDefinition = ShipDesignRegistry.All.OrderByDescending(definition => definition.IndustryCost).First();
        Require(constructionDefinition.IndustryCost > 5.0, "validation construction project is too small for constrained-allocation test");
        Require(shipDefinition.IndustryCost > 5.0, "validation ship design is too small for constrained-allocation test");

        construction.ActiveProjectId = constructionDefinition.Id;
        construction.ActiveProjectProgress = 0.0;
        shipyard.ActiveDesignId = shipDefinition.Id;
        shipyard.ActiveBuildProgress = 0.0;
        economy.Industry = 0.0;

        var result = new GalaxySimulationStepCoordinator().Advance(galaxy, 0.000001);
        var allocation = result.IndustryAllocations.Single(item => item.CivilizationId == playerId);

        Require(allocation.ConstructionDemand > allocation.ConstructionAllocated, "construction was not resource-constrained in validation step");
        Require(allocation.ShipbuildingDemand > allocation.ShipbuildingAllocated, "shipbuilding was not resource-constrained in validation step");
        RequireNear(allocation.ConstructionAllocated, allocation.ShipbuildingAllocated, "balanced simultaneous claims were not allocated equally");
        RequireNear(allocation.TotalAllocated, allocation.AvailableIndustry, "coordinator did not allocate the full constrained Industry stockpile");
        RequireNear(construction.ActiveProjectProgress, allocation.ConstructionAllocated, "construction spent a different amount than its Core budget");
        RequireNear(shipyard.ActiveBuildProgress, allocation.ShipbuildingAllocated, "shipbuilding spent a different amount than its Core budget");
        Require(Math.Abs(economy.Industry) < 0.000001, $"unaccounted Industry remained after fully constrained allocation: {economy.Industry}");
    }

    private static void ValidateAdaptiveEconomyDoesNotAccrueLegacyScience()
    {
        var galaxy = CreateGalaxy();
        var economy = galaxy.Economies.Single(state => state.CivilizationId == galaxy.PlayerCivilizationId);
        economy.Science = 37.0;

        _ = new GalaxySimulationStepCoordinator(advanceLegacyResearch: false).Advance(galaxy, 30.0);

        RequireNear(economy.Science, 37.0,
            "Adaptive campaign accumulated the retired Science stockpile");
        RequireNear(economy.LastSciencePerSecond, 0.0,
            "Adaptive campaign reported retired Science income");
    }

    private static void ValidateIndustryStorageCapacity()
    {
        var galaxy = CreateGalaxy();
        var playerId = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(state => state.CivilizationId == playerId);
        var capacity = EconomySimulation.GetIndustryStorageCapacity(galaxy, playerId);
        Require(capacity > economy.Industry, "opening Industry storage cannot hold the starting reserve");

        _ = new GalaxySimulationStepCoordinator(advanceLegacyResearch: false).Advance(galaxy, 10_000.0);

        RequireNear(economy.Industry, capacity, "idle Industry exceeded physical storage capacity");
        var construction = galaxy.ConstructionStates.Single(state => state.CivilizationId == playerId);
        construction.CompletedProjectIds.Add("industrial_automation");
        RequireNear(EconomySimulation.GetIndustryStorageCapacity(galaxy, playerId), capacity + 500.0,
            "Industrial Automation did not expand reserve storage");

        var legacy = CreateGalaxy();
        var legacyEconomy = legacy.Economies.Single(state => state.CivilizationId == legacy.PlayerCivilizationId);
        var legacyCapacity = EconomySimulation.GetIndustryStorageCapacity(legacy, legacy.PlayerCivilizationId);
        legacyEconomy.Industry = legacyCapacity + 125.0;
        _ = new GalaxySimulationStepCoordinator(advanceLegacyResearch: false).Advance(legacy, 30.0);
        RequireNear(legacyEconomy.Industry, legacyCapacity + 125.0,
            "storage cap destroyed a pre-existing or Developer-granted reserve");
    }

    private static void ValidateCoordinatorCombat()
    {
        var galaxy = CreateGalaxy();
        galaxy.Fleets.Clear();

        var civilizations = galaxy.Civilizations.Where(civilization => !civilization.IsSeededAncient).Take(2).ToArray();
        Require(civilizations.Length == 2, "validation galaxy did not contain two ordinary civilizations");
        var system = galaxy.Systems[0];
        var first = CreatePatrolFleet(9001, civilizations[0].Id, "Core Combat One", system.Id, system.Position);
        var second = CreatePatrolFleet(9002, civilizations[1].Id, "Core Combat Two", system.Id, system.Position);
        galaxy.Fleets.Add(first);
        galaxy.Fleets.Add(second);

        var combat = new CombatSimulation(new DelegateCombatHostilityView((firstId, secondId) =>
            (firstId == first.CivilizationId && secondId == second.CivilizationId) ||
            (firstId == second.CivilizationId && secondId == first.CivilizationId)));
        var coordinator = new GalaxySimulationStepCoordinator(combat: combat);
        var order = coordinator.IssueMilitaryOrder(
            galaxy,
            first.CivilizationId,
            first.Id,
            new MilitaryOrder(MilitaryOrderType.Attack, second.Id));
        Require(order.Accepted, $"Core coordinator rejected a valid hostile combat order: {order.Message}");

        var targetBefore = CombatProfileRegistry.EnsureState(second).Shields;
        var result = coordinator.Advance(galaxy, 0.75);
        var targetAfter = CombatProfileRegistry.EnsureState(second).Shields;

        Require(result.CombatEvents.Any(combatEvent => combatEvent.Type == CombatEventType.EngagementStarted),
            "Core coordinator did not publish engagement start");
        Require(result.CombatEvents.Any(combatEvent => combatEvent.Type == CombatEventType.DamageApplied && combatEvent.TargetFleetId == second.Id),
            "Core coordinator did not publish authoritative damage");
        Require(targetAfter < targetBefore, "Core combat step did not mutate authoritative target defenses");
    }

    private static void ValidateCampaignSessionLifecycle()
    {
        var tempDirectory = Path.Combine(Path.GetTempPath(), $"stellar-continuum-core-{Guid.NewGuid():N}");
        Directory.CreateDirectory(tempDirectory);

        try
        {
            var settings = new GalaxyGenerationSettings
            {
                SystemCount = 32,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 360.0f,
            };
            var service = new CampaignSessionService();
            const long initialSeed = 0x5345_5353_494F_4EL;
            var fresh = service.CreateNew(initialSeed, settings);

            Require(fresh.Source == CampaignBootstrapSource.NewCampaign, "new session reported the wrong bootstrap source");
            Require(fresh.Seed == initialSeed, "new session changed the requested seed");
            RequireNear(fresh.SimulationDays, 0.0, "new session did not start at day zero");
            Require(!fresh.WasLoaded && !fresh.RecoveredFromInvalidSave, "new session reported load/recovery state");

            var savePath = Path.Combine(tempDirectory, "campaign.json");
            const double savedDays = 123.25;
            service.Save(savePath, fresh.Galaxy, savedDays);
            var loaded = service.LoadOrCreate(savePath, fallbackSeed: 99L, fallbackSettings: settings);

            Require(loaded.Source == CampaignBootstrapSource.LoadedSave && loaded.WasLoaded, "valid save did not reload as a loaded campaign");
            Require(loaded.Seed == initialSeed, "loaded campaign did not preserve its seed");
            RequireNear(loaded.SimulationDays, savedDays, "loaded campaign did not restore simulation time");
            Require(loaded.SavedAtUtc is not null, "loaded campaign omitted save timestamp");
            Require(string.IsNullOrWhiteSpace(loaded.LoadFailure), "successful load reported a load failure");

            const long missingSeed = 0x4D49_5353_494E_47L;
            var missing = service.LoadOrCreate(Path.Combine(tempDirectory, "missing.json"), missingSeed, settings);
            Require(missing.Source == CampaignBootstrapSource.NewCampaign, "missing save did not start a new campaign");
            Require(missing.Seed == missingSeed, "missing-save fallback changed the requested seed");

            var corruptPath = Path.Combine(tempDirectory, "corrupt.json");
            File.WriteAllText(corruptPath, "{ definitely-not-valid-json");
            const long recoverySeed = 0x5245_434F_5645_52L;
            var recovered = service.LoadOrCreate(corruptPath, recoverySeed, settings);

            Require(recovered.Source == CampaignBootstrapSource.RecoveredFromInvalidSave, "corrupt save did not enter explicit recovery state");
            Require(recovered.RecoveredFromInvalidSave, "corrupt-save recovery flag was false");
            Require(recovered.Seed == recoverySeed, "recovery campaign changed the requested fallback seed");
            RequireNear(recovered.SimulationDays, 0.0, "recovery campaign did not restart at day zero");
            Require(!string.IsNullOrWhiteSpace(recovered.LoadFailure), "corrupt-save recovery did not preserve the load failure for diagnostics");
        }
        finally
        {
            if (Directory.Exists(tempDirectory))
                Directory.Delete(tempDirectory, recursive: true);
        }
    }

    private static FleetState CreatePatrolFleet(int id, int civilizationId, string name, int systemId, System.Numerics.Vector2 position) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = name,
        Role = FleetRole.Military,
        Position = position,
        CurrentSystemId = systemId,
        StrategicSpeed = 21.0,
        SensorRange = 125.0f,
        IsActive = true,
        Combat = CombatProfileRegistry.CreateInitialState(CombatProfileIds.PatrolCorvetteMk1, FleetRole.Military),
    };

    private static Game.Simulation.Models.GalaxyState CreateGalaxy() =>
        new GalaxyGenerator().Generate(
            0x434F_5245_5255_4EL,
            new GalaxyGenerationSettings
            {
                SystemCount = 40,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 460.0f,
            });

    private static void RequireNear(double actual, double expected, string message, double tolerance = 0.000001)
    {
        if (Math.Abs(actual - expected) > tolerance)
            throw new InvalidOperationException($"{message}: expected {expected:0.######}, got {actual:0.######}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
