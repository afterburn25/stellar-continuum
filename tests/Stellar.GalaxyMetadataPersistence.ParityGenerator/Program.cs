using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Simulation.Generation;
using Game.Simulation.Models;

try
{
    CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
    CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
    if (args.Length != 2)
        throw new ArgumentException("Expected source root and fixture path.");

    var sourceRoot = Path.GetFullPath(args[0]);
    var output = Path.GetFullPath(args[1]);
    var serviceType = typeof(CampaignSaveService);
    var validateMetadata = serviceType.GetMethod("ValidateGenerationMetadata", BindingFlags.NonPublic | BindingFlags.Static)!;
    var validateCore = serviceType.GetMethod("ValidateGalacticCore", BindingFlags.NonPublic | BindingFlags.Static)!;
    var validateAgreement = serviceType.GetMethod("ValidateGalacticCoreAgreement", BindingFlags.NonPublic | BindingFlags.Static)!;

    StarSystemState Star(int id, float x, float y) => new(
        id, $"Star {id}", new Vector2(x, y), StarArchetype.Standard,
        true, false, false, false, null, StellarPrimaryClass.GYellowDwarf,
        null, null, null, $"metadata:{id}");
    var systems = new[] { Star(1, 10, 0), Star(2, 30, 0) };
    var core = new GalacticCoreMetadata(GalacticCoreMetadata.StableLandmarkKey, 0, 0, 10);
    GalaxyGenerationMetadata Metadata() => new(
        "entered seed", 42, "generator-v", DateTimeOffset.Parse("2030-04-05T06:07:08.1234567+02:30", CultureInfo.InvariantCulture),
        2, "Shape", "Variety", "Common", "Rare", 3, 4, "Ancient", "Hazards",
        "Development", "Difficulty", "art-v", "species-v", "High") { GalacticCore = core };

    object? Invoke(MethodInfo method, params object?[] invocationArgs)
    {
        try { return method.Invoke(null, invocationArgs); }
        catch (TargetInvocationException exception) when (exception.InnerException is not null)
        { throw exception.InnerException; }
    }
    object? Error(Action action)
    {
        try { action(); return null; }
        catch (Exception exception) { return new { Type = exception.GetType().Name, exception.Message }; }
    }
    object ProjectCore(GalacticCoreMetadata? value) => value is null ? null! : new
    { value.LandmarkKey, value.X, value.Y, value.ExclusionRadius };
    object ProjectMetadata(GalaxyGenerationMetadata? value) => value is null ? null! : new
    {
        value.EnteredSeed, value.InternalSeed, value.GeneratorVersion, value.CreatedAtUtc,
        value.SystemCount, value.GalaxyShape, value.StellarVariety, value.PlanetBearingSystems,
        value.HabitableWorlds, value.GuaranteedNearbyHabitableWorlds, value.OtherCivilizations,
        value.AncientCivilizations, value.SpaceHazards, value.StartingDevelopment, value.Difficulty,
        value.ArtProfileVersion, value.PlayerSpeciesId, value.AnomalyFrequency,
        GalacticCore = ProjectCore(value.GalacticCore),
    };
    object Input(GalaxyGenerationMetadata? metadata, GalacticCoreMetadata? stateCore,
        IReadOnlyList<StarSystemState>? stars = null, long seed = 42) => new
    {
        Seed = seed,
        Systems = (stars ?? systems).Select(s => new { s.Id, X = s.Position.X, Y = s.Position.Y }).ToArray(),
        Metadata = ProjectMetadata(metadata), StateCore = ProjectCore(stateCore),
    };

    var rows = new List<object>();
    void Add(string name, string operation, GalaxyGenerationMetadata? metadata, GalacticCoreMetadata? stateCore,
        IReadOnlyList<StarSystemState>? stars = null)
    {
        var selected = stars ?? systems;
        object? result = null;
        var error = Error(() =>
        {
            if (operation == "Metadata") result = ProjectMetadata((GalaxyGenerationMetadata?)Invoke(validateMetadata, metadata, 42L, selected));
            else if (operation == "Core") result = ProjectCore((GalacticCoreMetadata?)Invoke(validateCore, stateCore, selected));
            else if (operation == "Agreement")
            {
                Invoke(validateAgreement, metadata?.GalacticCore, stateCore);
                result = null;
            }
            else
            {
                var validMetadata = (GalaxyGenerationMetadata?)Invoke(validateMetadata, metadata, 42L, selected);
                var validCore = (GalacticCoreMetadata?)Invoke(validateCore, stateCore, selected);
                Invoke(validateAgreement, validMetadata?.GalacticCore, validCore);
                result = new { GenerationMetadata = ProjectMetadata(validMetadata), GalacticCore = ProjectCore(validCore) };
            }
        });
        rows.Add(new { Name = name, Operation = operation, Input = Input(metadata, stateCore, selected), Result = result, Error = error });
    }

    Add("legacy-absence", "Capture", null, null);
    Add("all-fields-and-matching-core", "Capture", Metadata(), core);
    Add("metadata-core-only", "Capture", Metadata(), null);
    Add("state-core-only", "Capture", Metadata() with { GalacticCore = null }, core);
    Add("exact-radius-is-accepted", "Core", null, core);
    Add("strict-overlap-rejected", "Core", null, core, new[] { Star(1, 9.999f, 0) });
    Add("wrong-landmark-key", "Core", null, core with { LandmarkKey = "other" });
    Add("zero-radius", "Core", null, core with { ExclusionRadius = 0 });
    Add("nonfinite-coordinate", "Core", null, core with { X = float.PositiveInfinity });
    Add("blank-entered-seed", "Metadata", Metadata() with { EnteredSeed = " \t" }, null);
    Add("unicode-blank-entered-seed", "Metadata", Metadata() with { EnteredSeed = "\u00a0\u2007\u3000" }, null);
    Add("vertical-blank-generator-version", "Metadata", Metadata() with { GeneratorVersion = "\v\f" }, null);
    Add("blank-generator-version", "Metadata", Metadata() with { GeneratorVersion = "" }, null);
    Add("seed-mismatch", "Metadata", Metadata() with { InternalSeed = 41 }, null);
    Add("system-count-mismatch", "Metadata", Metadata() with { SystemCount = 3 }, null);
    Add("negative-other-civilizations", "Metadata", Metadata() with { OtherCivilizations = -1 }, null);
    Add("negative-nearby-worlds", "Metadata", Metadata() with { GuaranteedNearbyHabitableWorlds = -1 }, null);
    Add("core-disagreement", "Capture", Metadata(), core with { Y = 1 });
    Add("metadata-error-precedes-state-core", "Capture", Metadata() with { InternalSeed = 9 }, core with { LandmarkKey = "bad" });
    Add("agreement-dotnet-nan-equality", "Agreement", Metadata() with { GalacticCore = core with { X = float.NaN } }, core with { X = float.NaN });
    Add("agreement-signed-zero-equality", "Agreement", Metadata() with { GalacticCore = core with { X = -0.0f } }, core with { X = 0.0f });

    // Exercise the real detached capture path once, while retaining only this
    // gate's bounded metadata/core projection in the fixture.
    var captureSettings = new GalaxyGenerationSettings
    {
        SystemCount = 250,
        GalaxyShape = GalaxyShape.FullGalaxy,
        IncludeGalacticCore = true,
        PreWarpCivilizationCount = 6,
        AncientCivilizationCount = 1,
    };
    var capturedGalaxy = new GalaxyGenerator().Generate(4242, captureSettings);
    var capturedMetadata = new GalaxyGenerationMetadata(
        "actual capture", 4242, GalaxyGenerationMetadata.FullGalaxyGeneratorVersion,
        DateTimeOffset.Parse("2031-01-02T03:04:05.1234567+00:00", CultureInfo.InvariantCulture),
        250, "Full galaxy", "Dwarf-heavy", "Common", "Uncommon", 2, 5,
        "Rare", "Standard", "Early Space Age", "Standard", "milky-way-full-500-v1",
        "terran_baseline") { GalacticCore = capturedGalaxy.GalacticCore };
    capturedGalaxy.GenerationMetadata = capturedMetadata;
    var capturePayload = serviceType.GetMethod("CapturePayload", BindingFlags.NonPublic | BindingFlags.Instance)!;
    var payload = (JsonObject)capturePayload.Invoke(new CampaignSaveService(), new object?[] { capturedGalaxy, 0.0, false })!;
    var galaxyPayload = payload["Galaxy"]!.AsObject();
    rows.Add(new
    {
        Name = "actual-detached-capture",
        Operation = "Capture",
        Input = Input(capturedMetadata, capturedGalaxy.GalacticCore, capturedGalaxy.Systems, 4242),
        Result = new
        {
            GenerationMetadata = ProjectMetadata(galaxyPayload["GenerationMetadata"]!.Deserialize<GalaxyGenerationMetadata>()),
            GalacticCore = ProjectCore(galaxyPayload["GalacticCore"]!.Deserialize<GalacticCoreMetadata>()),
        },
        Error = (object?)null,
    });

    var hashes = new SortedDictionary<string, string>();
    foreach (var relative in new[] { "src/Game/Persistence/CampaignSaveService.cs", "src/Game/Simulation/Generation/GalaxyGenerationSettings.cs" })
        hashes[relative] = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(sourceRoot, relative.Replace('/', Path.DirectorySeparatorChar)))));
    var fixture = new { Schema = "stellar-galaxy-metadata-persistence-v1", SourceHashes = hashes, RowCount = rows.Count, Rows = rows };
    Directory.CreateDirectory(Path.GetDirectoryName(output)!);
    File.WriteAllText(output, JsonSerializer.Serialize(fixture, new JsonSerializerOptions
    {
        WriteIndented = true,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    }));
    Console.WriteLine($"wrote {rows.Count} galaxy metadata rows");
}
catch (Exception exception)
{
    Console.Error.WriteLine($"galaxy metadata fixture failure: {exception.GetType().Name}: {exception.Message}");
    return 1;
}
return 0;
