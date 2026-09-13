using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Construction;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
    if (args.Length != 2) throw new ArgumentException("Expected source root and fixture path.");
    var sourceRoot = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);
    var sourceFiles = new[]
    {
        "src/Game/Persistence/CampaignSaveService.cs",
        "src/Game/Simulation/Combat/CampaignMassiveCombat.cs",
        "src/Game/Simulation/Combat/Massive/MassiveCombatState.cs",
        "src/Game/Simulation/Combat/Massive/MassiveCombatEquipment.cs",
        "src/Game/Simulation/Construction/SurfaceConstruction.cs",
        "src/Game/Simulation/Economy/ResourceOutpostOperations.cs",
    };
    Dictionary<string, string> Hashes() => sourceFiles.ToDictionary(path => path,
        path => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(sourceRoot, path)))));
    var hashesBefore = Hashes();
    var options = new JsonSerializerOptions
    {
        IncludeFields = true,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    JsonElement Freeze<T>(T value) => JsonSerializer.SerializeToElement(value, options);
    var validate = typeof(Game.Persistence.CampaignSaveService).GetMethod(
        "ValidatePlanetaryReferences", BindingFlags.NonPublic | BindingFlags.Static)
        ?? throw new MissingMethodException("CampaignSaveService.ValidatePlanetaryReferences");
    object? Failure(Action action)
    {
        try { action(); return null; }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        { return new { Type = error.InnerException.GetType().Name, error.InnerException.Message }; }
        catch (Exception error) { return new { Type = error.GetType().Name, error.Message }; }
    }
    CivilizationState Civ(int id) => new(id, $"C{id}", 1,
        CivilizationArchetype.Adaptive, CivilizationTraits.Balanced, true,
        CivilizationDevelopmentStage.WarpCapable, false,
        SpeciesId: SpeciesCatalog.TerranBaselineId);
    PlanetaryBodyState Body(int id = 101, int system = 1, bool solid = true,
        bool rare = true) => new(id, system, null, 1, $"Body {id}",
        PlanetaryBodyKind.Planet, 1, 1,
        new(1, 288, 101, PlanetaryAtmosphereRegime.OxygenNitrogen,
            PlanetarySolventRegime.Water, 0, false, solid),
        true, rare, false, false);
    ColonyState Colony(int id = 20) => new()
    {
        Id = id, CivilizationId = 1, SystemId = 1, PlanetaryBodyId = 101,
        Name = $"Colony {id}", Kind = SettlementKind.Colony,
        PopulationSpeciesId = SpeciesCatalog.TerranBaselineId,
        PopulationMillions = 10, Infrastructure = 1, Stability = 1,
        StoredFoodPopulationDaysMillions = 10,
        StoredWaterPopulationDaysMillions = 10,
        StoredExtractedMaterials = 0, RemainingExtractableMaterials = 100,
        SurfaceHubLevel = 1,
    };
    SurfaceBuildingState Building(int id = 1) => new()
    {
        Id = id, TypeId = "power_generator",
        X = 60 + ((id - 1) % 8) * 50, Z = 60 + ((id - 1) / 8) * 50,
        RotationDegrees = 0, IndustryProgress = 300, IsComplete = true,
        IsEnabled = true, OperatingPriority = 0, Condition = 1,
        StoredPowerDays = 0,
    };
    CivilizationEconomyState Economy(int civ = 1) => new()
    {
        CivilizationId = civ, Credits = 100, Industry = 100, Science = 100,
        LastResearchFundingFraction = 1,
        LastBaseOperationsFundingFraction = 1,
    };
    FleetState Fleet(int id = 10, FleetRole role = FleetRole.Military,
        string? designId = "patrol_corvette", double maximumRange = 360) => new()
    {
        Id = id, CivilizationId = 1, Name = $"Fleet {id}",
        Role = role, DesignId = designId,
        Position = new(0, 0), CurrentSystemId = 1,
        CargoMaterialCapacity = 0, CargoMaterials = 0,
        StrategicSpeed = 22, MaximumLegRangeLightYears = maximumRange,
        FuelCapacityLightYears = 1000, FuelRemainingLightYears = 1000,
        SensorRange = 100, IsActive = true,
    };
    GalaxyState Baseline()
    {
        return new GalaxyState
        {
            Seed = 84,
            Systems = new List<StarSystemState> { new(1, "One", Vector2.Zero,
                StarArchetype.Standard, true, false, true, false),
                new(2, "Two", new(5, 0),
                StarArchetype.Standard, false, false, false, false) },
            PlanetaryBodies = new List<PlanetaryBodyState> { Body() },
            Civilizations = new List<CivilizationState> { Civ(1) },
            Fleets = new List<FleetState> { Fleet() },
            Colonies = new List<ColonyState> { Colony() },
            Economies = new List<CivilizationEconomyState> { Economy() },
            Technologies = new List<TechnologyState>(),
            ConstructionStates = new List<ConstructionState>(),
            ShipyardStates = new List<ShipyardState>(),
            PlayerCivilizationId = 1,
            Knowledge = new CivilizationKnowledgeState(),
        };
    }
    CampaignMassiveEncounter Encounter()
    {
        var formation = new MassiveFormationState
        {
            Id = 1, CivilizationId = 1, FleetId = 10, TaskForceId = 10,
            Name = "Formation", Position = new(0, 0), Velocity = new(0, 0),
            Heading = new(1, 0), Objective = new(0, 0),
            Shape = MassiveFormationShape.Line, Order = MassiveCombatOrderType.Hold,
            Cohesion = 1, Morale = 1, ShieldPool = 35, ArmorPool = 45,
            HullPool = 95, HullLossThresholdPerShip = 95, PowerReserve = 1,
            InitialShipCount = 0, Loadout = new MassiveCombatLoadout(),
        };
        formation.ImportantVessels.Add(new MassiveVesselState
        {
            Id = 10, Name = "Fleet 10", DesignId = "patrol_corvette",
        });
        return new CampaignMassiveEncounter
        {
            SystemId = 1, StartedDay = 2,
            Battle = new MassiveCombatBattleState
            {
                BattleId = new Guid("00112233-4455-6677-8899-aabbccddeeff"),
                Seed = 84, NextEventSequence = 1, NextSalvoId = 1,
                Formations = new() { formation },
            },
            Vessels = new() { new(10, 1) },
        };
    }
    object Projection(GalaxyState galaxy) => new
    {
        galaxy.Systems, Bodies = galaxy.PlanetaryBodies,
        galaxy.Civilizations, galaxy.Colonies, galaxy.Economies, galaxy.Fleets,
        galaxy.CombatIntelligence, galaxy.ActiveCombatEncounter,
    };

    var rows = new List<object>();
    void Add(string name, Action<GalaxyState> edit)
    {
        var galaxy = Baseline(); edit(galaxy);
        var before = Freeze(Projection(galaxy));
        var error = Failure(() => validate.Invoke(null, new object?[] { galaxy }));
        var after = Freeze(Projection(galaxy));
        rows.Add(new { Name = name, Before = before, After = after, Error = error });
    }

    Add("valid-baseline", _ => { });
    Add("encounter-materializes-count", g => g.ActiveCombatEncounter = Encounter());
    Add("encounter-materializes-before-later-intelligence-error", g =>
    { g.ActiveCombatEncounter = Encounter(); g.CombatIntelligence.Add(new(-1, 10, 1, 1, "bad")); });
    Add("encounter-operation-error-first", g =>
    { g.ActiveCombatEncounter = Encounter(); g.ActiveCombatEncounter.Battle.Formations[0].Name = "\u3000"; g.CombatIntelligence.Add(new(-1, 10, 1, 1, "bad")); });
    Add("encounter-duplicate-world-fleet-argument", g =>
    { g.ActiveCombatEncounter = Encounter(); g.Fleets.Add(Fleet()); });
    Add("intelligence-over-limit", g =>
    { for (var i = 0; i < 4097; ++i) g.CombatIntelligence.Add(new(i, 10, 1, 1, "seen")); });
    Add("intelligence-duplicate", g =>
    { g.CombatIntelligence.Add(new(1, 10, 1, 1, "a")); g.CombatIntelligence.Add(new(1, 10, 2, 2, "b")); });
    Add("intelligence-missing-fleet", g => g.CombatIntelligence.Add(new(1, 99, 1, 1, "seen")));
    Add("intelligence-invalid-scalar", g => g.CombatIntelligence.Add(new(1, 10, double.NaN, 1, "seen")));
    Add("intelligence-unicode-blank-evidence", g => g.CombatIntelligence.Add(new(1, 10, 1, 1, "\u202F")));
    Add("surface-invalid-economy-stock", g =>
    { g.Colonies[0].SurfaceBuildings.Add(Building()); g.Economies[0].Credits = double.NaN; });
    Add("surface-missing-civilization-economy", g =>
    { g.Colonies[0].SurfaceBuildings.Add(Building()); ((List<CivilizationEconomyState>)g.Economies).Clear(); });
    Add("surface-duplicate-civilization-economy", g =>
    { g.Colonies[0].SurfaceBuildings.Add(Building()); ((List<CivilizationEconomyState>)g.Economies).Add(Economy()); });
    Add("surface-economy-error-before-duplicate-body", g =>
    { g.Colonies[0].SurfaceBuildings.Add(Building()); g.Economies[0].Science = -1; ((List<PlanetaryBodyState>)g.PlanetaryBodies).Add(Body()); });
    Add("duplicate-body-argument", g => ((List<PlanetaryBodyState>)g.PlanetaryBodies).Add(Body()));
    Add("colony-unknown-kind", g => g.Colonies[0].Kind = (SettlementKind)99);
    Add("colony-invalid-stored-material", g => g.Colonies[0].StoredExtractedMaterials = double.PositiveInfinity);
    Add("colony-invalid-remaining-deposit", g => g.Colonies[0].RemainingExtractableMaterials = -1);
    Add("colony-invalid-hub", g => g.Colonies[0].SurfaceHubLevel = 4);
    Add("colony-invalid-reserves", g => g.Colonies[0].StoredWaterPopulationDaysMillions = double.NaN);
    Add("colony-invalid-surface", g =>
    { g.Colonies[0].SurfaceBuildings.Add(Building()); g.Colonies[0].SurfaceHubUpgradeDaysRemaining = -1; });
    Add("colony-surface-capacity", g =>
    { for (var i = 1; i <= 17; ++i) g.Colonies[0].SurfaceBuildings.Add(Building(i)); });
    Add("colony-surface-without-body", g =>
    { g.Colonies[0].SurfaceBuildings.Add(Building()); g.Colonies[0].PlanetaryBodyId = null; });
    Add("colony-body-outside-system", g => ((List<PlanetaryBodyState>)g.PlanetaryBodies)[0] = Body(system: 2));
    Add("colony-building-without-solid-ground", g =>
    { g.Colonies[0].SurfaceBuildings.Add(Building()); ((List<PlanetaryBodyState>)g.PlanetaryBodies)[0] = Body(solid: false); });
    Add("outpost-storage-capacity", g =>
    { g.Colonies[0].Kind = SettlementKind.ResourceOutpost; g.Colonies[0].StoredExtractedMaterials = 25.000002; });
    Add("outpost-deposit-capacity", g =>
    { g.Colonies[0].Kind = SettlementKind.ResourceOutpost; g.Colonies[0].StoredExtractedMaterials = 1; g.Colonies[0].RemainingExtractableMaterials = 100000; });
    Add("outpost-invalid-funding-range", g =>
    { g.Colonies[0].Kind = SettlementKind.ResourceOutpost; g.Economies[0].LastBaseOperationsFundingFraction = 2; });
    Add("outpost-funding-uses-first-matching-economy", g =>
    { g.Colonies[0].Kind = SettlementKind.ResourceOutpost; var later = Economy(); later.LastBaseOperationsFundingFraction = 2; ((List<CivilizationEconomyState>)g.Economies).Add(later); });
    Add("fleet-invalid-loadout-operation", g =>
    { g.Fleets[0].TacticalLoadout = new(); g.Fleets[0].TacticalLoadout!.HullPerShip = 0; });
    Add("fleet-loadout-slot-sum-overflow", g =>
    {
        g.Fleets[0].TacticalLoadout = new();
        g.Fleets[0].TacticalLoadout!.Modules.Add(new() { Id = "a", Slots = int.MaxValue });
        g.Fleets[0].TacticalLoadout.Modules.Add(new() { Id = "b", Slots = int.MaxValue });
    });
    Add("fleet-invalid-vessel-operation", g => g.Fleets[0].TacticalVessel = new());
    Add("fleet-vessel-identity", g => g.Fleets[0].TacticalVessel = new() { Id = 11, Name = "Other", DesignId = "patrol_corvette" });
    Add("fleet-design-role", g => g.Fleets[0] = Fleet(role: FleetRole.Military, designId: "science_vessel"));
    Add("fleet-invalid-range", g => g.Fleets[0] = Fleet(maximumRange: 0));
    Add("fleet-invalid-fuel", g => g.Fleets[0].FuelRemainingLightYears = 1000.000002);
    Add("fleet-invalid-cargo", g => g.Fleets[0].CargoMaterials = .000002);
    Add("fleet-freight-state-role", g => g.Fleets[0].FreightTargetOutpostId = 20);
    Add("fleet-invalid-freight-outpost", g =>
    { g.Fleets[0] = Fleet(role: FleetRole.Logistics, designId: null); g.Fleets[0].FreightTargetOutpostId = 20; });
    Add("fleet-invalid-freight-home", g =>
    { g.Fleets[0] = Fleet(role: FleetRole.Logistics, designId: null); g.Fleets[0].FreightHomeColonyId = 99; });
    Add("fleet-route-unknown-system", g => { g.Fleets[0].DestinationSystemId = 2; g.Fleets[0].PlannedRouteSystemIds.Add(99); });
    Add("fleet-route-without-destination", g => g.Fleets[0].PlannedRouteSystemIds.Add(2));
    Add("fleet-route-wrong-end", g => { g.Fleets[0].DestinationSystemId = 2; g.Fleets[0].PlannedRouteSystemIds.Add(1); });
    Add("fleet-unsupported-hold", g => g.Fleets[0].HoldRequested = true);
    Add("fleet-unsupported-return", g => g.Fleets[0].ReturnToBaseFailureReason = "failed");
    Add("fleet-inactive-military-orders-retained", g =>
    { g.Fleets[0].IsActive = false; g.Fleets[0].HoldRequested = true; g.Fleets[0].ReturnToBaseRequested = true; });
    Add("fleet-invalid-local-progress", g => g.Fleets[0].ReconnaissanceDaysCompleted = 2.000001);
    Add("fleet-local-work-without-order", g => g.Fleets[0].SettlementDaysCompleted = 1);
    Add("fleet-prevent-automatic-settlement-invalid-role", g => g.Fleets[0].PreventAutomaticSettlement = true);
    Add("fleet-invalid-settlement-site", g =>
    { g.Fleets[0] = Fleet(role: FleetRole.Colony, designId: null); g.Fleets[0].SettlementBodyId = 99; });
    Add("fleet-invalid-recon-site", g =>
    { g.Fleets[0] = Fleet(role: FleetRole.Scout, designId: null); g.Fleets[0].ReconnaissanceSystemId = 99; });
    Add("fleet-body-target-without-destination", g => g.Fleets[0].DestinationPlanetaryBodyId = 101);
    Add("fleet-body-target-outside-destination", g =>
    { g.Fleets[0] = Fleet(role: FleetRole.Colony, designId: null); g.Fleets[0].DestinationSystemId = 2; g.Fleets[0].DestinationPlanetaryBodyId = 101; });
    Add("fleet-valid-settlement-target", g =>
    { g.Fleets[0] = Fleet(role: FleetRole.Colony, designId: null); g.Fleets[0].DestinationSystemId = 1; g.Fleets[0].DestinationPlanetaryBodyId = 101; });
    Add("fleet-valid-arrived-settlement-target", g =>
    { g.Fleets[0] = Fleet(role: FleetRole.Colony, designId: null); g.Fleets[0].SettlementBodyId = 101; g.Fleets[0].DestinationPlanetaryBodyId = 101; });
    Add("source-omits-general-civilization-system-validation", g =>
    { g.Civilizations.Clear(); ((List<StarSystemState>)g.Systems).Add(g.Systems[0]); });

    var document = new
    {
        Schema = "stellar-galaxy-reference-validation-v1",
        SourceHashesBefore = hashesBefore,
        SourceHashesAfter = Hashes(),
        RowCount = rows.Count,
        Rows = rows,
    };
    File.WriteAllText(output, JsonSerializer.Serialize(document,
        new JsonSerializerOptions(options) { WriteIndented = true }));
    Console.WriteLine($"rows={rows.Count} fixture={Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(output)))}");
}
catch (Exception error)
{
    Console.Error.WriteLine($"{error.GetType().FullName}: {error.Message}");
    Console.Error.WriteLine($"Working directory: {Environment.CurrentDirectory}");
    Console.Error.WriteLine($"Source root: {(args.Length > 0 ? Path.GetFullPath(args[0]) : "<missing>")}");
    Console.Error.WriteLine($"Fixture: {(args.Length > 1 ? Path.GetFullPath(args[1]) : "<missing>")}");
    return 1;
}
return 0;
