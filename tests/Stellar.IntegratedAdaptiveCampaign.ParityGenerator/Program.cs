using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
    if (args.Length != 3)
        throw new ArgumentException("Expected research root, source root, and fixture path.");

    var researchRoot = Path.GetFullPath(args[0]);
    var sourceRoot = Path.GetFullPath(args[1]);
    var output = Path.GetFullPath(args[2]);
    var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(researchRoot);
    var factory = new AdaptiveResearchCampaignFactory(runtime);
    var codec = new AdaptiveResearchCampaignSnapshotCodec(runtime);
    var researchSimulation = new AdaptiveResearchCampaignSimulation();
    var snapshotJson = new JsonSerializerOptions
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
    };

    CivilizationState Civilization(int id, bool player = true) => new(
        id, $"C{id}", 7, CivilizationArchetype.Adaptive,
        CivilizationTraits.Balanced, player,
        CivilizationDevelopmentStage.WarpCapable, false,
        SpeciesId: SpeciesCatalog.TerranBaselineId);
    StarSystemState System(int id, float x = 0) => new(
        id, $"S{id}", new Vector2(x, 0), StarArchetype.Standard,
        true, false, false, false, null, StellarPrimaryClass.GYellowDwarf,
        null, null, null, $"integrated:{id}");
    PlanetaryBodyState Body(int id, int systemId) => new(
        id, systemId, null, id, $"B{id}", PlanetaryBodyKind.Planet,
        1, 1, new PlanetaryEnvironmentState(
            1, 288, 101.3, PlanetaryAtmosphereRegime.OxygenNitrogen,
            PlanetarySolventRegime.Water, 0.1, false, true),
        true, false, false, false);
    FleetState Fleet(int id, int civilizationId) => new()
    {
        Id = id, CivilizationId = civilizationId, Name = $"F{id}",
        Role = FleetRole.Military, Position = Vector2.Zero, CurrentSystemId = 7,
        Combat = new FleetCombatState
        {
            ProfileId = "corvette", Shields = 60, Armor = 40, Hull = 100,
        },
    };

    GalaxyState World(bool economy, bool contactFleets, bool aiCivilization)
    {
        var civilizations = new List<CivilizationState>
        {
            Civilization(2, !aiCivilization), Civilization(1),
        };
        return new GalaxyState
        {
            Seed = 42,
            Systems = new[] { System(7), System(8, 4) },
            PlanetaryBodies = new[] { Body(70, 7), Body(80, 8) },
            Civilizations = civilizations,
            Fleets = contactFleets
                ? new List<FleetState> { Fleet(20, 2), Fleet(10, 1) }
                : new List<FleetState>(),
            Colonies = new List<ColonyState>(),
            Economies = economy
                ? new[]
                {
                    new CivilizationEconomyState { CivilizationId = 2, Credits = 100, Industry = 100 },
                    new CivilizationEconomyState { CivilizationId = 1, Credits = 100, Industry = 100 },
                }
                : Array.Empty<CivilizationEconomyState>(),
            Technologies = new TechnologySeeder().Seed(civilizations),
            ConstructionStates = new ConstructionSeeder().Seed(civilizations),
            ShipyardStates = new ShipyardSeeder().Seed(civilizations),
            PlayerCivilizationId = 1,
            Knowledge = new CivilizationKnowledgeState(),
        };
    }

    object KnowledgeProjection(GalaxyState world) => new
    {
        CoreObservers = Array.Empty<int>(),
        Observers = world.Civilizations.Select(civilization => new
        {
            CivilizationId = civilization.Id,
            HasCoreAccess = world.Knowledge.HasGalacticCoreAccess(civilization.Id),
            CoreDiscovered = world.Knowledge.IsGalacticCoreDiscovered(civilization.Id),
            KnownSystems = world.Knowledge.GetKnownSystems(civilization.Id),
            KnownCivilizations = world.Knowledge.GetKnownCivilizations(civilization.Id),
            Survey = Array.Empty<object>(),
        }),
    };
    object WorldProjection(GalaxyState world) => new
    {
        world.Seed, world.Systems, Bodies = world.PlanetaryBodies,
        world.Civilizations, world.Fleets, world.Colonies, world.Economies,
        world.Technologies, Construction = world.ConstructionStates,
        Shipyards = world.ShipyardStates, world.PlayerCivilizationId,
        Knowledge = KnowledgeProjection(world), Core = (object?)null,
        UsedConstrainedHomeFallback = false, world.CombatIntelligence,
        SystemPositions = world.Systems.Select(value => new
        {
            value.Id, X = value.Position.X, Y = value.Position.Y,
        }),
        FleetPositions = world.Fleets.Select(value => new
        {
            value.Id, X = value.Position.X, Y = value.Position.Y,
            LocalStartX = value.LocalTransitStart.X,
            LocalStartY = value.LocalTransitStart.Y,
            LocalPositionX = value.LocalTransitPosition.X,
            LocalPositionY = value.LocalTransitPosition.Y,
            LocalTargetX = value.LocalTransitTarget.X,
            LocalTargetY = value.LocalTransitTarget.Y,
        }),
    };
    bool HasScanner(AdaptiveResearchCampaignState campaign, int civilizationId)
    {
        var state = campaign.Civilizations.TryGetValue(civilizationId, out var value) ? value : null;
        return state is not null &&
               (state.HasCapability("tech:quantum_sensors") ||
                state.HasCapability("tech:distributed_sensor_network"));
    }
    object ResearchCapabilities(GalaxyState world, AdaptiveResearchCampaignState campaign) =>
        world.Civilizations.OrderBy(value => value.Id).Select(value => new
        {
            CivilizationId = value.Id,
            QuantumSensors = campaign.GetCivilization(value.Id).HasCapability("tech:quantum_sensors"),
            DistributedSensors = campaign.GetCivilization(value.Id).HasCapability("tech:distributed_sensor_network"),
            Scanner = HasScanner(campaign, value.Id),
            OrbitalIndustry = campaign.GetCivilization(value.Id).HasCapability("orbital_industry"),
            SpacecraftConstruction = campaign.GetCivilization(value.Id).HasCapability("spacecraft_construction"),
            ExperimentalTransit = campaign.GetCivilization(value.Id).HasCapability("experimental_interstellar_transit"),
        });
    object State(GalaxyState world, AdaptiveResearchCampaignState campaign, DiplomacyState diplomacy) => new
    {
        World = WorldProjection(world),
        Research = JsonSerializer.SerializeToElement(codec.Capture(campaign), snapshotJson),
        ResearchCapabilityProjection = ResearchCapabilities(world, campaign),
        Diplomacy = diplomacy.Snapshot(),
    };

    void GrantCapability(AdaptiveResearchCampaignState campaign, int civilizationId, string capability)
    {
        var method = typeof(AdaptiveResearchCivilizationState).GetMethod(
            "AddCapability", BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new MissingMethodException("Adaptive Research AddCapability");
        _ = method.Invoke(campaign.GetCivilization(civilizationId), new object?[] { capability, null });
    }
    void StartResearch(GalaxyState world, AdaptiveResearchCampaignState campaign, int civilizationId)
    {
        var state = campaign.GetCivilization(civilizationId);
        var candidate = runtime.Authority.BuildView(state).VisibleNodes.First(value =>
            value.State == ResearchMaturity.Investigable && value.Blockers.Count == 0 &&
            value.MinimumLabs is not null);
        var result = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            world, campaign, civilizationId, candidate.NodeId,
            candidate.MinimumLabs!.Value, $"species:{SpeciesCatalog.TerranBaselineId}");
        if (!result.Accepted) throw new InvalidOperationException(result.Message);
    }
    void SeedCapabilityGates(GalaxyState world, AdaptiveResearchCampaignState campaign, bool grant)
    {
        var construction = world.ConstructionStates.First(value => value.CivilizationId == 1);
        construction.CompletedProjectIds.Add("orbital_launch_complex");
        construction.QueuedProjects.Add(new QueuedConstructionProject("orbital_shipyard", 350));
        world.ShipyardStates.First(value => value.CivilizationId == 1).QueuedBuilds.Add(
            new ShipBuildOrderState
            {
                OrderId = "build-1", DesignId = "warp_scout", AuthorizationCredits = 70,
            });
        if (!grant) return;
        GrantCapability(campaign, 1, "orbital_industry");
        GrantCapability(campaign, 1, "spacecraft_construction");
        GrantCapability(campaign, 1, "experimental_interstellar_transit");
    }
    void SeedObserverPrivacy(DiplomacyState diplomacy)
    {
        new DiplomacySimulation(diplomacy).ProcessContactOpportunity(
            new FirstContactOpportunity(1, "private-contact", 2, 0, 7,
                ContactAwareness.ContactEstablished, ContactCondition.Active, false, 0.9));
    }

    var rows = new List<object>();
    void Case(string name, bool restored, bool economy, bool contactFleets,
        bool scannerKnown, double days, double end, double reset = 0,
        bool activeResearch = false, bool completionAdvance = false,
        bool capabilityGate = false, bool grantGateCapabilities = false,
        bool aiPrivacy = false)
    {
        var input = JsonSerializer.SerializeToElement(new
        {
            Days = days, End = end, Economy = economy, ContactFleets = contactFleets,
            ScannerKnown = scannerKnown, Restored = restored, Reset = reset,
            ActiveResearch = activeResearch, CompletionAdvance = completionAdvance,
            CapabilityGate = capabilityGate, GrantGateCapabilities = grantGateCapabilities,
            AiPrivacy = aiPrivacy,
        });
        var world = World(economy, contactFleets, aiPrivacy);
        var research = factory.Create(world);
        if (scannerKnown) GrantCapability(research, 1, "tech:quantum_sensors");
        if (activeResearch) StartResearch(world, research, 1);
        if (capabilityGate) SeedCapabilityGates(world, research, grantGateCapabilities);
        if (completionAdvance)
            world.Economies.First(value => value.CivilizationId == 1).Credits = 1_000_000;
        if (restored) research = codec.Restore(world, codec.Capture(research));
        var diplomacyState = new DiplomacyState();
        if (aiPrivacy) SeedObserverPrivacy(diplomacyState);

        var constructionCapabilities = new AdaptiveResearchConstructionCapabilityView(research);
        var shipbuildingCapabilities = new AdaptiveResearchShipbuildingCapabilityView(research);
        var diplomacyRuntime = new DiplomacyCampaignRuntimeCoordinator(diplomacyState);
        diplomacyRuntime.Reset(reset, reviewImmediately: true);
        var strategic = new CivilizationStrategicRuntimeCoordinator(
            director: new CivilizationStrategicDirector(new CivilizationStrategicInputBuilder(
                shipbuildingCapabilities: shipbuildingCapabilities)),
            knowledgeProvider: new DiplomacyStrategicKnowledgeProvider(diplomacyState));
        var core = new GalaxySimulationStepCoordinator(
            construction: new ConstructionSimulation(constructionCapabilities),
            shipbuilding: new ShipbuildingSimulation(shipbuildingCapabilities),
            strategicAi: strategic,
            combatRuntime: diplomacyRuntime.CreateCombatCommandRuntime(),
            advanceLegacyResearch: false);

        var before = JsonSerializer.SerializeToElement(State(world, research, diplomacyState));
        SimulationStepResult? coreResult = null;
        IReadOnlyList<AdaptiveResearchCampaignEvent>? researchEvents = null;
        DiplomacyCampaignRuntimeStepResult? diplomacyResult = null;
        var sensorContacts = new List<object>();
        Exception? error = null;
        try
        {
            coreResult = core.Advance(world, days);
            if (days > 0)
            {
                foreach (var civilization in world.Civilizations.OrderBy(value => value.Id))
                {
                    var scanner = HasScanner(research, civilization.Id);
                    sensorContacts.Add(new
                    {
                        CivilizationId = civilization.Id, Scanner = scanner,
                        Recorded = FleetCombatPower.RecordSensorContacts(
                            world, civilization.Id, end, scanner),
                    });
                }
            }
            researchEvents = researchSimulation.Advance(world, research, days, end);
            diplomacyResult = diplomacyRuntime.Process(
                coreResult.ExplorationEvents, coreResult.CombatEvents, end);
        }
        catch (Exception caught)
        {
            error = caught;
        }

        var after = JsonSerializer.SerializeToElement(State(world, research, diplomacyState));
        rows.Add(new
        {
            Name = name,
            Composition = "plain C# reconstruction of Main.CoreIntegration; no Godot Main invocation; no player-save17 restore",
            Restored = restored, Arguments = input, InputBefore = input, InputAfter = input,
            Before = before, Core = coreResult, SensorContacts = sensorContacts,
            ResearchEvents = researchEvents, Diplomacy = diplomacyResult,
            PhaseEvidence = new
            {
                CoreCompleted = coreResult is not null,
                SensorCivilizationsCompleted = sensorContacts.Count,
                ResearchCompleted = researchEvents is not null,
                DiplomacyCompleted = diplomacyResult is not null,
                DiplomacyLastProcessedTick = diplomacyRuntime.LastProcessedTick,
                DiplomacyNextMaintenanceReviewTick = diplomacyRuntime.NextMaintenanceReviewTick,
                StrategicIntentCount = strategic.PublishedIntentCount,
                AiIndustryWeights = aiPrivacy ? strategic.GetIndustryWeights(2) : null,
                AiKnownCivilizations = aiPrivacy
                    ? new DiplomacyStrategicKnowledgeProvider(diplomacyState)
                        .Build(2, Math.Max(0, (long)Math.Floor(days))).Civilizations.Keys.OrderBy(value => value).ToArray()
                    : Array.Empty<int>(),
            },
            Error = error is null ? null : new { Type = error.GetType().Name, error.Message },
            After = after,
        });
    }

    Case("fresh-zero", false, true, false, false, 0, 0);
    Case("fresh-positive-numeric-order", false, true, false, false, 1, 1);
    Case("fresh-positive-contact-fleets-no-scanner", false, true, true, false, 1, 1);
    Case("fresh-positive-contact-fleets-quantum-scanner", false, true, true, true, 1, 1);
    Case("restored-positive", true, true, false, false, 1, 1);
    Case("active-research-spending", true, true, false, false, 10, 10, activeResearch: true);
    Case("active-research-completion", true, true, false, false, 365.25 * 8, 365.25 * 8,
        activeResearch: true, completionAdvance: true);
    Case("capability-gates-blocked", true, true, false, false, 1, 1, capabilityGate: true);
    Case("capability-gates-granted", true, true, false, false, 1, 1,
        capabilityGate: true, grantGateCapabilities: true);
    Case("ai-observer-privacy", true, true, false, false, 1, 1, aiPrivacy: true);
    Case("core-failure-missing-economy", false, false, false, false, 1, 1);
    Case("sensor-invalid-end-after-core", false, true, false, false, 1, -1);
    Case("research-invalid-end-after-core", false, true, false, false, 0, -1);
    Case("diplomacy-backward-after-core-research", false, true, false, false, 0, 1, 2);
    Case("invalid-negative-time-core-failure", false, true, false, false, -1, 1);

    var paths = new[]
    {
        "src/Game/Presentation/Main.CoreIntegration.cs",
        "src/Game/Presentation/Main.MassiveCombat.cs",
        "src/Game/Simulation/GalaxySimulationStepCoordinator.cs",
        "src/Game/Simulation/Research/Adaptive/AdaptiveResearchCampaignSimulation.cs",
        "src/Game/Simulation/Research/Adaptive/AdaptiveResearchCampaignState.cs",
        "src/Game/Simulation/Combat/FleetCombatPower.cs",
        "src/Game/Simulation/Diplomacy/DiplomacyCampaignRuntimeCoordinator.cs",
        "src/Game/Simulation/AI/CivilizationStrategicRuntimeCoordinator.cs",
        "src/Game/Simulation/AI/StrategicKnowledgeProvider.cs",
    };
    object Fingerprint(string path) => new
    {
        Path = path,
        Sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(sourceRoot, path)))),
    };
    var sourceFilesBefore = paths.Select(Fingerprint).ToArray();
    var sourceFilesAfter = paths.Select(Fingerprint).ToArray();
    if (JsonSerializer.Serialize(sourceFilesBefore) != JsonSerializer.Serialize(sourceFilesAfter))
        throw new InvalidOperationException("Source bytes changed while the oracle was generated.");

    var fixture = new
    {
        Schema = "stellar-integrated-adaptive-campaign-oracle-v3",
        Culture = "InvariantCulture",
        Scope = "plain C# Main.CoreIntegration composition reconstruction, not Godot Main invocation and not player-save17 restore",
        Boundary = new
        {
            AdvanceLegacyResearch = false, AccrueLegacyScience = false,
            ConstructionAuthority = "AdaptiveResearchConstructionCapabilityView",
            ShipbuildingAuthority = "AdaptiveResearchShipbuildingCapabilityView",
            AiKnowledgeAuthority = "DiplomacyStrategicKnowledgeProvider(observer-filtered)",
            CombatAuthority = "DiplomacyCampaignRuntimeCoordinator.CreateCombatCommandRuntime",
            AdvanceOrder = "core,sensor-contacts,adaptive-research,diplomacy",
        },
        SourceFilesBefore = sourceFilesBefore, SourceFilesAfter = sourceFilesAfter,
        Rows = rows, RowCount = rows.Count,
    };
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    File.WriteAllText(output, JsonSerializer.Serialize(
        fixture, new JsonSerializerOptions { WriteIndented = true }));
    Console.WriteLine($"integrated adaptive campaign oracle: {rows.Count} rows");
    return 0;
}
catch (Exception error)
{
    Console.Error.WriteLine(error);
    Console.Error.WriteLine($"cwd={Environment.CurrentDirectory}");
    Console.Error.WriteLine($"research={(args.Length > 0 ? args[0] : "<missing>")}");
    Console.Error.WriteLine($"source={(args.Length > 1 ? args[1] : "<missing>")}");
    Console.Error.WriteLine($"fixture={(args.Length > 2 ? args[2] : "<missing>")}");
    return 1;
}
