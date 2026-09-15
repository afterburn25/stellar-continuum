using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Persistence;
using Game.Simulation.Models;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new()
    {
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
        WriteIndented = true,
    };

    private static readonly MethodInfo ToBodies = Method("ToPlanetaryBodies");
    private static readonly MethodInfo ValidateBodies = Method("ValidatePlanetaryCatalog");
    private static readonly MethodInfo ToDtos = Method("ToPlanetaryBodyDtos");

    private sealed record Row(
        string Name,
        string Operation,
        JsonNode Input,
        JsonNode Before,
        JsonNode After,
        JsonNode? Result,
        string? ErrorType,
        string? ErrorMessage,
        string? ErrorInnerType,
        string? ErrorInnerMessage);

    private static MethodInfo Method(string name) => typeof(CampaignSaveService)
        .GetMethods(BindingFlags.NonPublic | BindingFlags.Static)
        .Single(method => method.Name == name);

    private static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        var output = Path.GetFullPath(args.Length > 0 ? args[0] : "campaign-planetary-fixture.json");
        var sourceRoot = Path.GetFullPath(args.Length > 1 ? args[1] : "src/Game");
        try
        {
            var rows = Rows();
            var paths = new[]
            {
                "Persistence/CampaignSaveService.cs",
                "Simulation/Models/PlanetaryBodyState.cs",
                "Simulation/Models/StarSystemState.cs",
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
                Authority = "actual private CampaignSaveService planetary-body adapters",
                SourceFiles = sources,
                RowCount = rows.Count,
                Rows = rows,
            };
            File.WriteAllText(output, JsonSerializer.Serialize(document, Json) +
                Environment.NewLine, new UTF8Encoding(false));
            Console.WriteLine($"Campaign planetary source oracle: {rows.Count}/{rows.Count} rows written.");
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
        AddRestore(rows, "restore-missing-list", null, Systems(1));
        AddRestore(rows, "restore-empty-list", new(), Systems(1));
        AddRestore(rows, "restore-null-item", new() { null }, Systems(1));
        AddRestore(rows, "restore-valid-ordered", new()
        {
            Body(7, 1, PlanetaryBodyKind.Planet),
            Body(8, 1, PlanetaryBodyKind.Moon, 7),
            Body(9, 1, PlanetaryBodyKind.DwarfPlanet),
        }, Systems(1));
        AddRestore(rows, "restore-duplicate-id", Bodies(
            Body(7, 1, PlanetaryBodyKind.Planet),
            Body(7, 1, PlanetaryBodyKind.Planet)), Systems(1));
        AddRestore(rows, "restore-unknown-kind", Bodies(
            Change(Body(), body => body.Kind = (PlanetaryBodyKind)99)), Systems(1));
        AddRestore(rows, "restore-null-environment", Bodies(
            Change(Body(), body => body.Environment = null)), Systems(1));
        AddRestore(rows, "restore-unknown-atmosphere", Bodies(
            Change(Body(), body => body.Environment!.Atmosphere = (PlanetaryAtmosphereRegime)99)), Systems(1));
        AddRestore(rows, "restore-unknown-solvent", Bodies(
            Change(Body(), body => body.Environment!.AvailableSolvent = (PlanetarySolventRegime)(-1))), Systems(1));
        AddRestore(rows, "restore-negative-id", Bodies(
            Change(Body(), body => body.Id = -1)), Systems(1));
        AddRestore(rows, "restore-negative-system-id", Bodies(
            Change(Body(), body => body.SystemId = -1)), Systems(1));
        AddRestore(rows, "restore-negative-orbit", Bodies(
            Change(Body(), body => body.OrbitIndex = -1)), Systems(1));
        AddRestore(rows, "restore-null-name", Bodies(
            Change(Body(), body => body.Name = null!)), Systems(1));
        AddRestore(rows, "restore-unicode-blank-name", Bodies(
            Change(Body(), body => body.Name = "\u2003")), Systems(1));
        AddRestore(rows, "restore-radius-zero", Bodies(
            Change(Body(), body => body.RadiusEarth = 0)), Systems(1));
        AddRestore(rows, "restore-mass-nan", Bodies(
            Change(Body(), body => body.MassEarth = double.NaN)), Systems(1));
        AddRestore(rows, "restore-gravity-negative", Bodies(
            Change(Body(), body => body.Environment!.GravityG = -1)), Systems(1));
        AddRestore(rows, "restore-temperature-infinity", Bodies(
            Change(Body(), body => body.Environment!.TemperatureKelvin = double.PositiveInfinity)), Systems(1));
        AddRestore(rows, "restore-pressure-negative", Bodies(
            Change(Body(), body => body.Environment!.PressureKPa = -1)), Systems(1));
        AddRestore(rows, "restore-radiation-over-one", Bodies(
            Change(Body(), body => body.Environment!.RadiationHazard = 1.01)), Systems(1));
        AddRestore(rows, "restore-eccentricity-one", Bodies(
            Change(Body(), body => body.OrbitalEccentricity = 1)), Systems(1));
        AddRestore(rows, "restore-inclination-over-180", Bodies(
            Change(Body(), body => body.OrbitalInclinationDegrees = 180.01)), Systems(1));
        AddRestore(rows, "restore-unknown-system", Bodies(Body(1, 4)), Systems(1));
        AddRestore(rows, "restore-parent-cycle", Bodies(
            Body(1, 1, PlanetaryBodyKind.Moon, 2),
            Body(2, 1, PlanetaryBodyKind.Moon, 1)), Systems(1));
        AddRestore(rows, "restore-primary-with-parent", Bodies(
            Body(1, 1, PlanetaryBodyKind.Planet, 2),
            Body(2, 1, PlanetaryBodyKind.Planet)), Systems(1));
        AddRestore(rows, "restore-moon-without-parent", Bodies(
            Body(1, 1, PlanetaryBodyKind.Moon)), Systems(1));
        AddRestore(rows, "restore-missing-parent", Bodies(
            Body(1, 1, PlanetaryBodyKind.Moon, 99)), Systems(1));
        AddRestore(rows, "restore-parent-is-moon", Bodies(
            Body(1, 1, PlanetaryBodyKind.Moon, 2),
            Body(2, 1, PlanetaryBodyKind.Moon, 3),
            Body(3, 1, PlanetaryBodyKind.Planet)), Systems(1));
        AddRestore(rows, "restore-cross-system-parent", Bodies(
            Body(1, 1, PlanetaryBodyKind.Moon, 2),
            Body(2, 2, PlanetaryBodyKind.Planet)), Systems(1, 2));
        AddCapture(rows, "capture-valid-ordered", new()
        {
            State(Body(8, 1, PlanetaryBodyKind.Moon, 7)),
            State(Body(7, 1, PlanetaryBodyKind.Planet)),
        }, Systems(1));
        AddCapture(rows, "capture-empty", new(), Systems(1));
        AddCapture(rows, "capture-invalid-before-projection", new()
        {
            State(Change(Body(), body => body.Environment!.RadiationHazard = double.NaN)),
        }, Systems(1));
        return rows;
    }

    private static List<PlanetaryBodySaveDto?> Bodies(params PlanetaryBodySaveDto[] values) =>
        values.Cast<PlanetaryBodySaveDto?>().ToList();

    private static PlanetaryBodySaveDto Change(
        PlanetaryBodySaveDto value,
        Action<PlanetaryBodySaveDto> change)
    {
        change(value);
        return value;
    }

    private static PlanetaryBodySaveDto Body(
        int id = 1,
        int systemId = 1,
        PlanetaryBodyKind kind = PlanetaryBodyKind.Planet,
        int? parent = null) => new()
    {
        Id = id,
        SystemId = systemId,
        ParentBodyId = parent,
        OrbitIndex = id + 2,
        Name = $"Body {id}",
        Kind = kind,
        RadiusEarth = 1.25,
        MassEarth = 2.5,
        Environment = new()
        {
            GravityG = 1.1,
            TemperatureKelvin = 280.25,
            PressureKPa = 101.5,
            Atmosphere = PlanetaryAtmosphereRegime.OxygenNitrogen,
            AvailableSolvent = PlanetarySolventRegime.Water,
            RadiationHazard = .125,
            IsImmersedEnvironment = false,
            HasSolidSurface = true,
        },
        LegacyColonizationCandidate = true,
        HasRareResource = true,
        HasAnomaly = true,
        HasPreWarpCivilization = false,
        OrbitalEccentricity = .25,
        OrbitalInclinationDegrees = 17.5,
    };

    private static PlanetaryBodyState State(PlanetaryBodySaveDto value) => new(
        value.Id,
        value.SystemId,
        value.ParentBodyId,
        value.OrbitIndex,
        value.Name,
        value.Kind,
        value.RadiusEarth,
        value.MassEarth,
        new(
            value.Environment!.GravityG,
            value.Environment.TemperatureKelvin,
            value.Environment.PressureKPa,
            value.Environment.Atmosphere,
            value.Environment.AvailableSolvent,
            value.Environment.RadiationHazard,
            value.Environment.IsImmersedEnvironment,
            value.Environment.HasSolidSurface),
        value.LegacyColonizationCandidate,
        value.HasRareResource,
        value.HasAnomaly,
        value.HasPreWarpCivilization,
        value.OrbitalEccentricity,
        value.OrbitalInclinationDegrees);

    private static List<StarSystemState> Systems(params int[] ids) => ids
        .Select(id => new StarSystemState(
            id,
            $"System {id}",
            new(id, -id),
            StarArchetype.Standard,
            false,
            false,
            false,
            false))
        .ToList();

    private static void AddRestore(
        List<Row> rows,
        string name,
        List<PlanetaryBodySaveDto?>? dtos,
        List<StarSystemState> systems)
    {
        var input = InputNode(dtos, systems);
        var before = input.DeepClone();
        object? typedResult = null;
        Exception? error = null;
        try
        {
            typedResult = ToBodies.Invoke(null, new object?[] { dtos, systems });
        }
        catch (Exception exception)
        {
            error = Unwrap(exception);
        }
        var result = typedResult is null
            ? null
            : Node(((IEnumerable<PlanetaryBodyState>)typedResult).ToArray());
        var after = InputNode(dtos, systems);
        rows.Add(new(name, "Restore", input, before, after, result,
            error?.GetType().Name, error?.Message,
            error?.InnerException?.GetType().Name,
            error?.InnerException?.Message));
    }

    private static void AddCapture(
        List<Row> rows,
        string name,
        List<PlanetaryBodyState> bodies,
        List<StarSystemState> systems)
    {
        var input = InputNode(bodies, systems);
        var before = input.DeepClone();
        object? typedResult = null;
        Exception? error = null;
        try
        {
            ValidateBodies.Invoke(null, new object[] { bodies, systems });
            typedResult = ToDtos.Invoke(null, new object[] { bodies });
        }
        catch (Exception exception)
        {
            error = Unwrap(exception);
        }
        var result = typedResult is null
            ? null
            : Node(((IEnumerable<PlanetaryBodySaveDto?>)typedResult).ToArray());
        var after = InputNode(bodies, systems);
        rows.Add(new(name, "Capture", input, before, after, result,
            error?.GetType().Name, error?.Message,
            error?.InnerException?.GetType().Name,
            error?.InnerException?.Message));
    }

    private static JsonNode InputNode(
        object? bodies,
        IEnumerable<StarSystemState> systems) => Node(new
        {
            BodiesPresent = bodies is not null,
            Bodies = bodies,
            Systems = systems.Select(system => new
            {
                system.Id,
                system.Name,
                X = system.Position.X,
                Y = system.Position.Y,
                system.Archetype,
                system.HasHabitableWorld,
                system.HasAnomaly,
                system.HasRareResource,
                system.HasPreWarpCivilization,
                system.CatalogPresetId,
                system.StellarClass,
                system.SecondaryStellarClass,
                system.TertiaryStellarClass,
                system.GalacticDepthLightYears,
                system.StellarCatalogId,
            }).ToArray(),
        });

    private static JsonNode Node(object? value) =>
        JsonSerializer.SerializeToNode(value, Json) ?? JsonValue.Create((string?)null)!;

    private static Exception Unwrap(Exception exception)
    {
        while (exception is TargetInvocationException { InnerException: not null })
            exception = exception.InnerException;
        return exception;
    }
}
