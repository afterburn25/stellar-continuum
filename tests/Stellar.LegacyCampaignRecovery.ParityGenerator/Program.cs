using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Persistence;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
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
        "src/Game/Simulation/Knowledge/CivilizationKnowledgeState.cs",
        "src/Game/Simulation/Generation/TechnologySeeder.cs",
        "src/Game/Simulation/Research/TechnologyRegistry.cs",
        "src/Game/Simulation/Generation/ConstructionSeeder.cs",
        "src/Game/Simulation/Construction/ConstructionRegistry.cs",
        "src/Game/Simulation/Shipbuilding/ShipDesignDefinition.cs",
        "src/Game/Simulation/Combat/CombatProfiles.cs",
        "src/Game/Simulation/Species/SpeciesCatalog.cs",
        "src/Game/Simulation/Models/FleetState.cs",
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
    MethodInfo Method(string name) => typeof(CampaignSaveService).GetMethod(
        name, BindingFlags.NonPublic | BindingFlags.Static)
        ?? throw new MissingMethodException(typeof(CampaignSaveService).FullName, name);
    var initialKnowledge = Method("CreateInitialKnowledge");
    var migratedTechnology = Method("CreateMigratedTechnologyStates");
    var migratedConstruction = Method("CreateMigratedConstructionStates");
    var expansionFleets = Method("EnsureLegacyExpansionFleets");
    object? Invoke(MethodInfo method, params object?[] values)
    {
        try { return method.Invoke(null, values); }
        catch (TargetInvocationException error) when (error.InnerException is not null)
        { throw error.InnerException; }
    }
    object? Failure(Action action)
    {
        try { action(); return null; }
        catch (Exception error) { return new { Type = error.GetType().Name, error.Message }; }
    }

    StarSystemState System(int id, float x) => new(id, $"S{id}", new(x, 0),
        StarArchetype.Standard, false, false, false, false);
    CivilizationState Civ(int id, int home = 1,
        CivilizationDevelopmentStage stage = CivilizationDevelopmentStage.WarpCapable,
        bool ancient = false, bool expansion = true, bool player = false,
        string? name = null) => new(id, name ?? $"C{id}", home,
            CivilizationArchetype.Adaptive, CivilizationTraits.Balanced, player,
            stage, ancient, expansion, SpeciesId: SpeciesCatalog.TerranBaselineId);
    ColonyState Colony(int id, int civilization, double population = 2500,
        string species = SpeciesCatalog.TerranBaselineId) => new()
    {
        Id = id, CivilizationId = civilization, SystemId = 1,
        Name = $"Colony {id}", PopulationMillions = population,
        PopulationSpeciesId = species,
    };
    FleetState Fleet(int id, int civilization, bool active = true,
        double population = 0, string? species = null,
        FleetRole role = FleetRole.Colony) => new()
    {
        Id = id, CivilizationId = civilization, Name = $"Fleet {id}",
        Role = role, Position = Vector2.Zero, CurrentSystemId = 1,
        IsActive = active, EmbarkedPopulationMillions = population,
        EmbarkedPopulationSpeciesId = species,
    };
    object KnowledgeProjection(CivilizationKnowledgeState knowledge,
        IList<CivilizationState> civilizations) => civilizations
        .Select(c => new
        {
            CivilizationId = c.Id,
            KnownSystems = knowledge.GetKnownSystems(c.Id),
            Surveys = knowledge.GetSystemSurveyKnowledge(c.Id),
        }).ToArray();

    var rows = new List<object>();
    void KnowledgeRow(string name, List<StarSystemState> systems,
        List<CivilizationState> civilizations)
    {
        var beforeSystems = Freeze(systems);
        var beforeCivilizations = Freeze(civilizations);
        object? result = null;
        var error = Failure(() =>
        {
            var knowledge = (CivilizationKnowledgeState)Invoke(
                initialKnowledge, systems, civilizations)!;
            result = Freeze(KnowledgeProjection(knowledge, civilizations));
        });
        rows.Add(new
        {
            Name = name, Operation = "InitialKnowledge",
            SystemsBefore = beforeSystems, SystemsAfter = Freeze(systems),
            CivilizationsBefore = beforeCivilizations,
            CivilizationsAfter = Freeze(civilizations), Result = result, Error = error,
        });
    }
    KnowledgeRow("knowledge-empty", new(), new());
    KnowledgeRow("knowledge-ordinary-95-boundary",
        new() { System(1, 0), System(2, 94.999F), System(3, 95F), System(4, 95.001F) },
        new() { Civ(1, ancient: false) });
    KnowledgeRow("knowledge-ancient-420-boundary",
        new() { System(1, 0), System(2, 419.999F), System(3, 420F), System(4, 420.001F) },
        new() { Civ(1, stage: CivilizationDevelopmentStage.AncientSpacefaring, ancient: true) });
    KnowledgeRow("knowledge-seeded-ancient-flag-controls-range",
        new() { System(1, 0), System(2, 200) },
        new() { Civ(1, stage: CivilizationDevelopmentStage.PreWarp, ancient: true) });
    KnowledgeRow("knowledge-duplicate-civilization-id-merges",
        new() { System(1, 0), System(2, 50), System(3, 400) },
        new() { Civ(7, home: 1), Civ(7, home: 3, ancient: true) });
    KnowledgeRow("knowledge-unknown-home-operation",
        new() { System(1, 0) }, new() { Civ(1, home: 99) });

    void StateRow(string name, string operation, List<CivilizationState> civilizations)
    {
        var before = Freeze(civilizations);
        object? result = null;
        var error = Failure(() => result = operation switch
        {
            "MigratedTechnology" => Freeze((IList<TechnologyState>)Invoke(
                migratedTechnology, civilizations)!),
            "MigratedConstruction" => Freeze((IList<ConstructionState>)Invoke(
                migratedConstruction, civilizations)!),
            _ => throw new ArgumentException("Unknown state operation."),
        });
        rows.Add(new
        {
            Name = name, Operation = operation,
            CivilizationsBefore = before, CivilizationsAfter = Freeze(civilizations),
            Result = result, Error = error,
        });
    }
    var stages = new List<CivilizationState>
    {
        Civ(1, stage: CivilizationDevelopmentStage.PreWarp),
        Civ(2, stage: CivilizationDevelopmentStage.WarpCapable),
        Civ(3, stage: CivilizationDevelopmentStage.AncientSpacefaring, ancient: true),
        Civ(4, stage: CivilizationDevelopmentStage.WarpCapable, ancient: true),
        Civ(2, stage: CivilizationDevelopmentStage.WarpCapable),
    };
    StateRow("technology-empty", "MigratedTechnology", new());
    StateRow("technology-stage-and-duplicate-id-matrix", "MigratedTechnology", stages);
    StateRow("construction-empty", "MigratedConstruction", new());
    StateRow("construction-stage-flag-and-duplicate-id-matrix", "MigratedConstruction", stages);

    void ExpansionRow(string name, Action<List<StarSystemState>, List<CivilizationState>,
        List<ColonyState>, List<FleetState>> edit)
    {
        var systems = new List<StarSystemState> { System(1, 12), System(2, 24) };
        var civilizations = new List<CivilizationState> { Civ(1, player: true) };
        var colonies = new List<ColonyState> { Colony(10, 1) };
        var fleets = new List<FleetState>();
        edit(systems, civilizations, colonies, fleets);
        var systemsBefore = Freeze(systems);
        var civilizationsBefore = Freeze(civilizations);
        var coloniesBefore = Freeze(colonies);
        var fleetsBefore = Freeze(fleets);
        var error = Failure(() => Invoke(expansionFleets,
            fleets, systems, civilizations, colonies));
        rows.Add(new
        {
            Name = name, Operation = "ExpansionFleets",
            SystemsBefore = systemsBefore, SystemsAfter = Freeze(systems),
            CivilizationsBefore = civilizationsBefore,
            CivilizationsAfter = Freeze(civilizations),
            ColoniesBefore = coloniesBefore, ColoniesAfter = Freeze(colonies),
            FleetsBefore = fleetsBefore, FleetsAfter = Freeze(fleets), Error = error,
        });
    }
    ExpansionRow("expansion-player-new-defaults", (_, _, _, _) => { });
    ExpansionRow("expansion-ai-name", (_, c, _, _) => c[0] = Civ(1, name: "Aurora"));
    ExpansionRow("expansion-prewarp-noop", (_, c, _, _) =>
        c[0] = Civ(1, stage: CivilizationDevelopmentStage.PreWarp));
    ExpansionRow("expansion-disabled-noop", (_, c, _, _) => c[0] = Civ(1, expansion: false));
    ExpansionRow("expansion-existing-populated-noop", (_, _, _, f) =>
        f.Add(Fleet(7, 1, population: 5, species: SpeciesCatalog.TerranBaselineId)));
    ExpansionRow("expansion-existing-zero-repaired-and-combat-normalized", (_, _, _, f) =>
    {
        var fleet = Fleet(7, 1);
        fleet.Combat = new FleetCombatState { ProfileId = "missing", Shields = -2,
            Armor = 999, Hull = double.NaN, WeaponCooldownRemainingDays = -3,
            RetreatProgressDays = -4, Order = (MilitaryOrderType)999 };
        f.Add(fleet);
    });
    ExpansionRow("expansion-inactive-existing-does-not-block", (_, _, _, f) =>
        f.Add(Fleet(7, 1, active: false)));
    ExpansionRow("expansion-no-source-noop", (_, _, c, _) => c.Clear());
    ExpansionRow("expansion-below-threshold-noop", (_, _, c, _) => c[0].PopulationMillions = 749.999);
    ExpansionRow("expansion-exact-threshold", (_, _, c, _) => c[0].PopulationMillions = 750);
    ExpansionRow("expansion-largest-stable-tie", (_, _, c, _) =>
    {
        c[0].PopulationMillions = 2500; c.Add(Colony(11, 1, 2500));
    });
    ExpansionRow("expansion-finite-precedes-nan", (_, _, c, _) =>
    {
        c[0].PopulationMillions = double.NaN; c.Add(Colony(11, 1, 2500));
    });
    ExpansionRow("expansion-all-nan-selects-first", (_, _, c, _) =>
    {
        c[0].PopulationMillions = double.NaN; c.Add(Colony(11, 1, double.NaN));
    });
    ExpansionRow("expansion-unknown-species-data", (_, _, c, _) =>
        c[0].PopulationSpeciesId = "unknown");
    ExpansionRow("expansion-unicode-blank-species-data", (_, _, c, _) =>
        c[0].PopulationSpeciesId = "\u202F");
    ExpansionRow("expansion-population-deducted-before-missing-home", (s, c, _, _) =>
    {
        s.Clear(); c[0] = Civ(1, home: 99);
    });
    ExpansionRow("expansion-int32-wrap-two-additions", (_, c, colonies, fleets) =>
    {
        fleets.Add(Fleet(int.MaxValue, 99, active: false));
        c.Add(Civ(2, home: 2, name: "Second"));
        colonies.Add(Colony(20, 2));
    });
    ExpansionRow("expansion-nonwarp-before-warp-preserves-next-id", (_, c, colonies, _) =>
    {
        c.Insert(0, Civ(8, stage: CivilizationDevelopmentStage.PreWarp));
        colonies.Add(Colony(80, 8));
    });

    var hashesAfter = Hashes();
    if (!hashesBefore.OrderBy(x => x.Key).SequenceEqual(hashesAfter.OrderBy(x => x.Key)))
        throw new InvalidOperationException("Source changed during generation.");
    var fixture = new
    {
        Schema = "stellar.legacy-campaign-recovery.actual-source.v1",
        SourceHashesBefore = hashesBefore,
        SourceHashesAfter = hashesAfter,
        RowCount = rows.Count,
        Rows = rows,
    };
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    File.WriteAllText(output, JsonSerializer.Serialize(fixture, new JsonSerializerOptions(options)
    { WriteIndented = true }));
    Console.WriteLine($"rows={rows.Count} fixture={Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(output)))}");
}
catch (Exception error)
{
    Console.Error.WriteLine($"legacy campaign recovery oracle failure: {error.GetType()}: {error.Message}");
    Console.Error.WriteLine($"cwd: {Environment.CurrentDirectory}");
    Console.Error.WriteLine($"source root: {(args.Length > 0 ? Path.GetFullPath(args[0]) : "<missing>")}");
    Console.Error.WriteLine($"fixture: {(args.Length > 1 ? Path.GetFullPath(args[1]) : "<missing>")}");
    return 1;
}
return 0;
