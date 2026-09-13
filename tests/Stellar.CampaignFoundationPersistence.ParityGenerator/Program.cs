using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Persistence;
using Game.Simulation.Models;
using Game.Simulation.Species;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new()
    {
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    private static readonly MethodInfo ToSystems = Method("ToSystems");
    private static readonly MethodInfo ValidateSystems = Method("ValidateStellarCatalog");
    private static readonly MethodInfo ToSystemDtos = Method("ToSystemDtos");
    private static readonly MethodInfo ToCivilizations = Method("ToCivilizations");
    private static readonly MethodInfo ToCivilizationDtos = Method("ToCivilizationDtos");

    private sealed record Row(string Name, string Operation, JsonNode Input,
        JsonNode Before, JsonNode After, JsonNode? Result,
        string? ErrorType, string? ErrorMessage);

    private static MethodInfo Method(string name) => typeof(CampaignSaveService)
        .GetMethods(BindingFlags.NonPublic | BindingFlags.Static)
        .Single(method => method.Name == name);

    private static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        var output = Path.GetFullPath(args.Length > 0 ? args[0] : "campaign-foundation-fixture.json");
        var sourceRoot = Path.GetFullPath(args.Length > 1 ? args[1] : "src/Game");
        try
        {
            var rows = Rows();
            var paths = new[]
            {
                "Persistence/CampaignSaveService.cs",
                "Simulation/Models/StarSystemState.cs",
                "Simulation/Models/CivilizationState.cs",
                "Simulation/Models/CivilizationLeadershipState.cs",
                "Simulation/Species/SpeciesAssignmentPolicy.cs",
                "Simulation/Species/SpeciesCatalog.cs",
                "Simulation/Generation/SolCatalogPreset.cs",
            };
            var sources = paths.Select(path => new
            {
                Path = path,
                Sha256 = Convert.ToHexString(SHA256.HashData(
                    File.ReadAllBytes(Path.Combine(sourceRoot, path)))),
            }).ToArray();
            var document = new
            {
                SchemaVersion = 1,
                Authority = "actual private CampaignSaveService stellar/civilization adapters",
                SourceFiles = sources,
                RowCount = rows.Count,
                Rows = rows,
            };
            File.WriteAllText(output, JsonSerializer.Serialize(document,
                new JsonSerializerOptions(Json) { WriteIndented = true }) +
                Environment.NewLine, new UTF8Encoding(false));
            Console.WriteLine($"Campaign foundation source oracle: {rows.Count}/{rows.Count} rows written.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            Console.Error.WriteLine($"Working directory: {Environment.CurrentDirectory}");
            Console.Error.WriteLine($"Source root: {sourceRoot}");
            Console.Error.WriteLine($"Fixture path: {output}");
            return 1;
        }
    }

    private static List<Row> Rows()
    {
        var rows = new List<Row>();
        AddSystemRestore(rows, "systems-empty", list => list.Clear());
        AddSystemRestore(rows, "systems-valid-full", list => list.Add(FullSystem(2)));
        AddSystemRestore(rows, "systems-order-and-duplicate-id", list =>
        {
            list.Add(FullSystem(2)); list.Add(BasicSystem(2));
        });
        AddSystemRestore(rows, "systems-depth-nan", list => { var dto = FullSystem(1); dto.GalacticDepthLightYears = double.NaN; list.Add(dto); });
        AddSystemRestore(rows, "systems-depth-infinity", list => { var dto = FullSystem(1); dto.GalacticDepthLightYears = double.PositiveInfinity; list.Add(dto); });
        AddSystemRestore(rows, "systems-primary-unknown", list => { var dto = FullSystem(1); dto.StellarClass = (StellarPrimaryClass)99; list.Add(dto); });
        AddSystemRestore(rows, "systems-secondary-unknown", list => { var dto = FullSystem(1); dto.SecondaryStellarClass = (StellarPrimaryClass)(-1); list.Add(dto); });
        AddSystemRestore(rows, "systems-tertiary-unknown", list => { var dto = FullSystem(1); dto.TertiaryStellarClass = (StellarPrimaryClass)99; list.Add(dto); });
        AddSystemRestore(rows, "systems-secondary-without-primary", list => { var dto = BasicSystem(1); dto.SecondaryStellarClass = StellarPrimaryClass.KOrangeDwarf; list.Add(dto); });
        AddSystemRestore(rows, "systems-tertiary-without-secondary", list => { var dto = BasicSystem(1); dto.StellarClass = StellarPrimaryClass.MRedDwarf; dto.TertiaryStellarClass = StellarPrimaryClass.KOrangeDwarf; list.Add(dto); });
        AddSystemRestore(rows, "systems-sol-companion", list => { var dto = FullSystem(0); dto.CatalogPresetId = "sol-v1"; list.Add(dto); });
        AddSystemCapture(rows, "systems-capture-valid", list => list.Add(SystemState(FullSystem(4))));
        AddSystemCapture(rows, "systems-capture-empty", _ => { });
        AddSystemCapture(rows, "systems-capture-invalid-before-copy", list => { var dto = FullSystem(4); dto.GalacticDepthLightYears = double.NegativeInfinity; list.Add(SystemState(dto)); });

        AddCivilizationRestore(rows, "civilization-current-null-leadership-human", false, 16, 17, dto => dto.Leadership = null);
        AddEmptyCivilizationRestore(rows);
        AddCivilizationRestore(rows, "civilization-current-null-leadership-nonhuman", false, 16, 17, dto => { dto.Id = 8; dto.SpeciesId = SpeciesCatalog.PelagicHighPressureId; dto.Leadership = null; });
        AddCivilizationRestore(rows, "civilization-current-empty-leadership", false, 16, 17, dto => dto.Leadership = new());
        AddCivilizationRestore(rows, "civilization-current-unsorted-leadership", false, 16, 17, dto => dto.Leadership = new()
        {
            ["\U00010000"] = new("supplementary", "Supplementary", "voice", "portrait"),
            ["\uE000"] = new("private", "Private"),
            ["A"] = new("ascii", "ASCII"),
        });
        AddCivilizationRestore(rows, "civilization-legacy-warp-overrides", true, 7, -33, dto =>
        {
            dto.Id = 2; dto.DevelopmentStage = CivilizationDevelopmentStage.AncientSpacefaring;
            dto.IsSeededAncient = true; dto.ExpansionAllowed = false;
            dto.NeutralUnlessProvoked = true; dto.SpeciesId = "unknown-ignored";
        });
        AddCivilizationRestore(rows, "civilization-pre8-species-ignores-saved", false, 7, long.MinValue, dto =>
        {
            dto.Id = 3; dto.SpeciesId = "unknown-ignored"; dto.Leadership = null;
        });
        AddCivilizationRestore(rows, "civilization-current-unknown-species", false, 8, 1, dto => dto.SpeciesId = "unknown");
        AddCivilizationRestore(rows, "civilization-current-blank-species-unicode", false, 8, 1, dto => dto.SpeciesId = "\u2003");
        AddCivilizationRestore(rows, "civilization-pre8-negative-id", false, 7, 1, dto => dto.Id = -1);
        AddCivilizationRestore(rows, "civilization-unknown-enums-accepted", false, 16, 1, dto =>
        {
            dto.Archetype = (CivilizationArchetype)99;
            dto.DevelopmentStage = (CivilizationDevelopmentStage)(-1);
        });
        AddCivilizationRestore(rows, "civilization-leadership-blank-office", false, 16, 1, dto => dto.Leadership = new() { ["\u2003"] = new("id", "name") });
        AddCivilizationRestore(rows, "civilization-leadership-null-character", false, 16, 1, dto => dto.Leadership = new() { ["Office"] = null! });
        AddCivilizationRestore(rows, "civilization-leadership-blank-character-id", false, 16, 1, dto => dto.Leadership = new() { ["Office"] = new(" ", "name") });
        AddCivilizationRestore(rows, "civilization-leadership-blank-display", false, 16, 1, dto => dto.Leadership = new() { ["Office"] = new("id", "\u00a0") });
        AddCivilizationRestore(rows, "civilization-leadership-long-voice", false, 16, 1, dto => dto.Leadership = new() { ["Office"] = new("id", "name", new string('v', 129)) });
        AddCivilizationRestore(rows, "civilization-leadership-long-portrait", false, 16, 1, dto => dto.Leadership = new() { ["Office"] = new("id", "name", null, new string('p', 513)) });
        AddCivilizationRestore(rows, "civilization-leadership-capacity", false, 16, 1, dto =>
        {
            dto.Leadership = Enumerable.Range(0, 33).ToDictionary(i => $"Office{i:00}", i => new CivilizationCharacter($"id{i}", $"name{i}"));
        });
        AddCivilizationCapture(rows, "civilization-capture-valid", list => list.Add(CivilizationState()));
        AddCivilizationCapture(rows, "civilization-capture-empty", _ => { });
        AddCivilizationCapture(rows, "civilization-capture-unknown-species", list => list.Add(CivilizationState() with { SpeciesId = "unknown" }));
        return rows;
    }

    private static void AddEmptyCivilizationRestore(List<Row> rows)
    {
        var dtos = Array.Empty<CivilizationSaveDto>();
        var input = new
        {
            Dtos = dtos,
            LegacyAlreadyWarpCapable = false,
            SaveFormatVersion = 16,
            CampaignSeed = 1L,
        };
        var before = Node(input);
        object? typed = null;
        Exception? error = null;
        try
        {
            typed = ToCivilizations.Invoke(null,
                new object[] { dtos, false, 16, 1L });
        }
        catch (Exception exception) { error = Unwrap(exception); }
        var result = typed is null ? null :
            CivilizationsNode((IList<CivilizationState>)typed);
        rows.Add(new("civilization-restore-empty", "RestoreCivilizations",
            before, before, Node(input), result, error?.GetType().Name,
            error?.Message));
    }

    private static StarSystemSaveDto BasicSystem(int id) => new()
    {
        Id = id, Name = $"System {id}", X = id + .25f, Y = -id - .5f,
        Archetype = StarArchetype.ResourceRich, HasHabitableWorld = true,
        HasAnomaly = true, HasRareResource = true, HasPreWarpCivilization = true,
    };

    private static StarSystemSaveDto FullSystem(int id)
    {
        var result = BasicSystem(id);
        result.CatalogPresetId = "custom";
        result.StellarClass = StellarPrimaryClass.MRedDwarf;
        result.SecondaryStellarClass = StellarPrimaryClass.KOrangeDwarf;
        result.TertiaryStellarClass = StellarPrimaryClass.GYellowDwarf;
        result.GalacticDepthLightYears = 12.5;
        result.StellarCatalogId = "catalog-id";
        return result;
    }

    private static StarSystemState SystemState(StarSystemSaveDto value) => new(
        value.Id, value.Name, new(value.X, value.Y), value.Archetype,
        value.HasHabitableWorld, value.HasAnomaly, value.HasRareResource,
        value.HasPreWarpCivilization, value.CatalogPresetId, value.StellarClass,
        value.SecondaryStellarClass, value.TertiaryStellarClass,
        value.GalacticDepthLightYears, value.StellarCatalogId);

    private static CivilizationSaveDto CivilizationDto() => new()
    {
        Id = 0, Name = "Civilization", HomeSystemId = 4,
        Archetype = CivilizationArchetype.Adaptive, Aggression = .1,
        Territoriality = .2, Greed = .3, ScientificCuriosity = .4,
        RiskTolerance = .5, SurvivalPriority = .6, HonorBound = true,
        IsPlayer = true, DevelopmentStage = CivilizationDevelopmentStage.PreWarp,
        IsSeededAncient = false, ExpansionAllowed = true,
        NeutralUnlessProvoked = false, SpeciesId = SpeciesCatalog.TerranBaselineId,
        Leadership = new() { ["Governor"] = new("id", "Name", "voice", "portrait") },
    };

    private static CivilizationState CivilizationState() => new(
        5, "Civilization", 9, CivilizationArchetype.Scientific,
        new(.1, .2, .3, .4, .5, .6, true), false,
        CivilizationDevelopmentStage.WarpCapable, false, true, false,
        SpeciesCatalog.CompactHighGravityId)
    {
        Leadership = CivilizationLeadershipState.Restore(new Dictionary<string, CivilizationCharacter>
        {
            ["Governor"] = new("id", "Name", "voice", "portrait"),
        }),
    };

    private static void AddSystemRestore(List<Row> rows, string name,
        Action<List<StarSystemSaveDto>> mutate)
    {
        var input = new List<StarSystemSaveDto> { BasicSystem(1) };
        mutate(input);
        var before = Node(input);
        object? typed = null; Exception? error = null;
        try
        {
            typed = ToSystems.Invoke(null, new object[] { input });
            ValidateSystems.Invoke(null, new[] { typed });
        }
        catch (Exception exception) { error = Unwrap(exception); }
        var result = typed is null ? null : SystemsNode((IReadOnlyList<StarSystemState>)typed);
        rows.Add(new(name, "RestoreSystems", before, before, Node(input), result,
            error?.GetType().Name, error?.Message));
    }

    private static void AddSystemCapture(List<Row> rows, string name,
        Action<List<StarSystemState>> mutate)
    {
        var input = new List<StarSystemState>(); mutate(input);
        var before = SystemsNode(input); object? typed = null; Exception? error = null;
        try
        {
            ValidateSystems.Invoke(null, new object[] { input });
            typed = ToSystemDtos.Invoke(null, new object[] { input });
        }
        catch (Exception exception) { error = Unwrap(exception); }
        var result = typed is null ? null : Node(typed);
        rows.Add(new(name, "CaptureSystems", before, before, SystemsNode(input), result,
            error?.GetType().Name, error?.Message));
    }

    private static void AddCivilizationRestore(List<Row> rows, string name,
        bool legacyWarp, int version, long seed, Action<CivilizationSaveDto> mutate)
    {
        var dto = CivilizationDto(); mutate(dto);
        var input = new { Dtos = new[] { dto }, LegacyAlreadyWarpCapable = legacyWarp,
            SaveFormatVersion = version, CampaignSeed = seed };
        var before = Node(input); object? typed = null; Exception? error = null;
        try
        {
            typed = ToCivilizations.Invoke(null,
                new object[] { new[] { dto }, legacyWarp, version, seed });
        }
        catch (Exception exception) { error = Unwrap(exception); }
        var result = typed is null ? null : CivilizationsNode((IList<CivilizationState>)typed);
        rows.Add(new(name, "RestoreCivilizations", before, before, Node(input), result,
            error?.GetType().Name, error?.Message));
    }

    private static void AddCivilizationCapture(List<Row> rows, string name,
        Action<List<CivilizationState>> mutate)
    {
        var input = new List<CivilizationState>(); mutate(input);
        var before = CivilizationsNode(input); object? typed = null; Exception? error = null;
        try { typed = ToCivilizationDtos.Invoke(null, new object[] { input }); }
        catch (Exception exception) { error = Unwrap(exception); }
        var result = typed is null ? null : Node(typed);
        rows.Add(new(name, "CaptureCivilizations", before, before,
            CivilizationsNode(input), result, error?.GetType().Name, error?.Message));
    }

    private static JsonNode Node(object? value) => JsonSerializer.SerializeToNode(value, Json)!;

    private static JsonNode SystemsNode(IEnumerable<StarSystemState> values) => Node(values.Select(value => new
    {
        value.Id, value.Name, X = value.Position.X, Y = value.Position.Y,
        value.Archetype, value.HasHabitableWorld, value.HasAnomaly,
        value.HasRareResource, value.HasPreWarpCivilization, value.CatalogPresetId,
        value.StellarClass, value.SecondaryStellarClass, value.TertiaryStellarClass,
        value.GalacticDepthLightYears, value.StellarCatalogId,
    }));

    private static JsonNode CivilizationsNode(IEnumerable<CivilizationState> values) => Node(values.Select(value => new
    {
        value.Id, value.Name, value.HomeSystemId, value.Archetype,
        Aggression = value.Traits.Aggression,
        Territoriality = value.Traits.Territoriality,
        Greed = value.Traits.Greed,
        ScientificCuriosity = value.Traits.ScientificCuriosity,
        RiskTolerance = value.Traits.RiskTolerance,
        SurvivalPriority = value.Traits.SurvivalPriority,
        HonorBound = value.Traits.HonorBound,
        value.IsPlayer, value.DevelopmentStage, value.IsSeededAncient,
        value.ExpansionAllowed, value.NeutralUnlessProvoked, value.SpeciesId,
        Leadership = value.Leadership.Offices.Select(entry => new
        {
            Office = entry.Key, entry.Value.Id, entry.Value.DisplayName,
            entry.Value.VoiceProfileId, entry.Value.Portrait,
        }).ToArray(),
    }));

    private static Exception Unwrap(Exception exception) =>
        exception is TargetInvocationException { InnerException: not null } invocation
            ? invocation.InnerException! : exception;
}
