using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Campaign;
using Game.Simulation.Generation;
using Game.Simulation.Models;
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
        "src/Game/Campaign/CampaignSessionService.cs",
        "src/Game/Simulation/Generation/FullGalaxyStellarPopulation.cs",
        "src/Game/Simulation/Generation/GalaxyGenerator.cs",
        "src/Game/Simulation/Generation/GalaxyGenerationSettings.cs",
    };
    Dictionary<string, string> SourceHashes() => sourceFiles.ToDictionary(
        path => path,
        path => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(sourceRoot, path)))),
        StringComparer.Ordinal);
    var sourceBefore = SourceHashes();
    var options = new JsonSerializerOptions
    {
        WriteIndented = true,
        IncludeFields = true,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    var cases = new List<object>();

    GalaxyGenerationSettings Settings(long seed, int count, int preWarp, int ancient, string species)
    {
        if (count is not (250 or 500 or 1000 or 2500))
        {
            // CreateNew constructs metadata before generation and rejects this count
            // without consulting the remaining settings.
            return new GalaxyGenerationSettings
            {
                SystemCount = count,
                GalaxyShape = GalaxyShape.FullGalaxy,
                IncludeGalacticCore = true,
                PreWarpCivilizationCount = preWarp,
                AncientCivilizationCount = ancient,
                PlayerSpeciesId = species,
                HabitableChance = .16,
                AnomalyChance = .20,
                InitialPreWarpSensorRange = 8,
                InitialAncientSensorRange = 25,
            };
        }
        var canonical = GalaxyGenerationMetadata.FullGalaxy500(
            seed.ToString(CultureInfo.InvariantCulture), seed, species, count).ToSettings();
        return new GalaxyGenerationSettings
        {
            SystemCount = canonical.SystemCount,
            GalaxyShape = canonical.GalaxyShape,
            IncludeGalacticCore = canonical.IncludeGalacticCore,
            Radius = canonical.Radius,
            InitialPreWarpSensorRange = canonical.InitialPreWarpSensorRange,
            InitialAncientSensorRange = canonical.InitialAncientSensorRange,
            PreWarpCivilizationCount = preWarp,
            AncientCivilizationCount = ancient,
            PlayerSpeciesId = species,
            ArchetypeWeights = canonical.ArchetypeWeights,
            HabitableChance = canonical.HabitableChance,
            AnomalyChance = canonical.AnomalyChance,
            RareResourceChance = canonical.RareResourceChance,
            IndependentPreWarpChance = canonical.IndependentPreWarpChance,
        };
    }

    object ProjectSettings(GalaxyGenerationSettings settings) => new
    {
        settings.SystemCount,
        GalaxyShape = settings.GalaxyShape.ToString(),
        settings.IncludeGalacticCore,
        settings.Radius,
        settings.InitialPreWarpSensorRange,
        settings.InitialAncientSensorRange,
        settings.PreWarpCivilizationCount,
        settings.AncientCivilizationCount,
        settings.PlayerSpeciesId,
        settings.HabitableChance,
        settings.AnomalyChance,
        settings.RareResourceChance,
        settings.IndependentPreWarpChance,
    };
object Project(GalaxyState galaxy, GalaxyGenerationMetadata normalizedMetadata) => new
{
    galaxy.Seed,
    galaxy.Systems,
    Bodies = galaxy.PlanetaryBodies,
    galaxy.Civilizations,
    galaxy.Fleets,
    galaxy.Colonies,
    galaxy.Economies,
    galaxy.Technologies,
    Construction = galaxy.ConstructionStates,
    Shipyards = galaxy.ShipyardStates,
    galaxy.PlayerCivilizationId,
    Knowledge = new
    {
        CoreObservers = galaxy.Knowledge.GetGalacticCoreObservers(),
        Observers = galaxy.Civilizations.Select(civilization => new
        {
            CivilizationId = civilization.Id,
            HasCoreAccess = galaxy.Knowledge.HasGalacticCoreAccess(civilization.Id),
            CoreDiscovered = galaxy.Knowledge.IsGalacticCoreDiscovered(civilization.Id),
            KnownSystems = galaxy.Knowledge.GetKnownSystems(civilization.Id),
            KnownCivilizations = galaxy.Knowledge.GetKnownCivilizations(civilization.Id),
            Survey = galaxy.Knowledge.GetSystemSurveyKnowledge(civilization.Id),
        }).ToArray(),
    },
    GenerationMetadata = normalizedMetadata,
    Core = galaxy.GalacticCore,
};

    void Fresh(string name, long seed, int count, int preWarp, int ancient,
        string species, string normalizedCreatedAt)
    {
        var settings = Settings(seed, count, preWarp, ancient, species);
        var settingsBefore = JsonSerializer.SerializeToElement(ProjectSettings(settings), options);
        GalaxyState? galaxy = null;
        object? error = null;
        try
        {
            var earliestCreation = DateTimeOffset.UtcNow;
            galaxy = new CampaignSessionService().CreateNew(seed, settings).Galaxy;
            var latestCreation = DateTimeOffset.UtcNow;
            var sourceMetadata = galaxy.GenerationMetadata
                ?? throw new InvalidOperationException("CreateNew omitted generation metadata.");
            if (sourceMetadata.CreatedAtUtc < earliestCreation || sourceMetadata.CreatedAtUtc > latestCreation)
                throw new InvalidOperationException("CreateNew metadata clock lies outside invocation bounds.");
        }
        catch (Exception exception)
        {
            error = new { Type = exception.GetType().Name, exception.Message };
        }
        var settingsAfter = JsonSerializer.SerializeToElement(ProjectSettings(settings), options);
        if (!string.Equals(settingsBefore.GetRawText(), settingsAfter.GetRawText(),
            StringComparison.Ordinal))
            throw new InvalidOperationException($"{name}: source mutated generation settings.");
        object? result = null;
        if (galaxy is not null)
        {
            var normalized = galaxy.GenerationMetadata! with
            {
                CreatedAtUtc = DateTimeOffset.Parse(normalizedCreatedAt, CultureInfo.InvariantCulture,
                    DateTimeStyles.None),
            };
            result = Project(galaxy, normalized);
        }
        cases.Add(new
        {
            Name = name,
            Arguments = new
            {
                Seed = seed,
                SystemCount = count,
                PreWarpCount = preWarp,
                AncientCount = ancient,
                PlayerSpeciesId = species,
                CreatedAtUtc = normalizedCreatedAt,
                SettingsProfile = "GalaxyGenerationMetadata.FullGalaxy500.ToSettings + civilization overrides",
            },
            SettingsBefore = settingsBefore,
            SettingsAfter = settingsAfter,
            Result = result,
            Error = error,
        });
    }

    Fresh("persistable-250-terran-default", 0x220250L, 250, 6, 1,
        SpeciesCatalog.TerranBaselineId, "2042-01-02T03:04:05+00:00");
    Fresh("persistable-500-pelagic-no-ancient", -0x220500L, 500, 4, 0,
        SpeciesCatalog.PelagicHighPressureId, "2042-02-03T04:05:06+00:00");
    Fresh("persistable-1000-compact-two-ancient", 0x221000L, 1000, 3, 2,
        SpeciesCatalog.CompactHighGravityId, "2042-03-04T05:06:07+00:00");
    Fresh("persistable-2500-cryogenic-default", 0x222500L, 2500, 6, 1,
        SpeciesCatalog.CryogenicHydrocarbonId, "2042-04-05T06:07:08+00:00");
    Fresh("persistable-minimum-civilizations", long.MinValue, 250, 1, 0,
        SpeciesCatalog.TerranBaselineId, "2042-05-06T07:08:09+00:00");
    Fresh("invalid-system-count-precedes-species", 1, 100, 0, 0,
        "missing_species", "2042-06-07T08:09:10+00:00");
    Fresh("invalid-civilization-count", 1, 250, 0, 0,
        SpeciesCatalog.TerranBaselineId, "2042-07-08T09:10:11+00:00");
    Fresh("invalid-player-species", 1, 250, 1, 0,
        "missing_species", "2042-08-09T10:11:12+00:00");

    var sourceAfter = SourceHashes();
    if (!sourceBefore.OrderBy(pair => pair.Key).SequenceEqual(sourceAfter.OrderBy(pair => pair.Key)))
        throw new InvalidOperationException("Source files changed during generation.");
    var fixture = new
    {
        Schema = "stellar.persistable-fresh-campaign.actual-source.v1",
        SourceHashesBefore = sourceBefore,
        SourceHashesAfter = sourceAfter,
        RowCount = cases.Count,
        Cases = cases,
    };
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    File.WriteAllText(output, JsonSerializer.Serialize(fixture, options) + Environment.NewLine,
        new UTF8Encoding(false));
    Console.WriteLine($"rows={cases.Count} fixture={Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(output)))}");
}
catch (Exception error)
{
    Console.Error.WriteLine($"persistable fresh campaign oracle failure: {error.GetType()}: {error.Message}");
    Console.Error.WriteLine($"cwd: {Environment.CurrentDirectory}");
    Console.Error.WriteLine($"source root: {(args.Length > 0 ? Path.GetFullPath(args[0]) : "<missing>")}");
    Console.Error.WriteLine($"fixture: {(args.Length > 1 ? Path.GetFullPath(args[1]) : "<missing>")}");
    return 1;
}
return 0;
