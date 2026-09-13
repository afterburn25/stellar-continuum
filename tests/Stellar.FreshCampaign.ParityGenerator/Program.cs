using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
var options = new JsonSerializerOptions
{
    WriteIndented = false,
    IncludeFields = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
var cases = new List<object>();

GalaxyGenerationSettings Settings(int count, int preWarp, int ancient, string species) => new()
{
    SystemCount = count,
    GalaxyShape = GalaxyShape.FullGalaxy,
    IncludeGalacticCore = true,
    Radius = FullGalaxyStellarPopulation.RadiusFor(count),
    InitialPreWarpSensorRange = 8,
    InitialAncientSensorRange = 25,
    PreWarpCivilizationCount = preWarp,
    AncientCivilizationCount = ancient,
    HabitableChance = .16,
    AnomalyChance = .20,
    RareResourceChance = .12,
    IndependentPreWarpChance = .04,
    PlayerSpeciesId = species,
};

object Project(GalaxyState galaxy) => new
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
    Core = galaxy.GalacticCore,
};

void Fresh(string name, long seed, int count, int preWarp, int ancient, string species)
{
    var arguments = new
    {
        Seed = seed,
        SystemCount = count,
        PreWarpCount = preWarp,
        AncientCount = ancient,
        PlayerSpeciesId = species,
        SettingsProfile = "FullGalaxy + GalaxyGenerationSettings.ToSettings defaults"
    };
    GalaxyGenerationSettings? settings = null;
    GalaxyState? result = null;
    object? error = null;
    string? errorStage = null;
    try { settings = Settings(count, preWarp, ancient, species); }
    catch (Exception exception)
    {
        error = new { Type = exception.GetType().Name, exception.Message };
        errorStage = "Settings";
    }
    if (settings is not null)
    {
        try { result = new GalaxyGenerator().Generate(seed, settings); }
        catch (Exception exception)
        {
            error = new { Type = exception.GetType().Name, exception.Message };
            errorStage = "Generate";
        }
    }
    var projected = result is { } galaxy ? Project(galaxy) : null;
    cases.Add(new
    {
        Name = name,
        Kind = "Fresh",
        Arguments = arguments,
        Result = projected,
        Error = error,
        ErrorStage = errorStage,
    });
}

Fresh("fresh-250-terran-default", 0x220250L, 250, 6, 1, SpeciesCatalog.TerranBaselineId);
Fresh("fresh-500-pelagic-no-ancient", -0x220500L, 500, 4, 0, SpeciesCatalog.PelagicHighPressureId);
Fresh("fresh-1000-compact-two-ancient", 0x221000L, 1000, 3, 2, SpeciesCatalog.CompactHighGravityId);
Fresh("fresh-2500-cryogenic-default", 0x222500L, 2500, 6, 1, SpeciesCatalog.CryogenicHydrocarbonId);
Fresh("fresh-250-minimal-no-ancient", 0x220001L, 250, 1, 0, SpeciesCatalog.TerranBaselineId);
Fresh("fresh-invalid-system-count", 1, 100, 1, 0, SpeciesCatalog.TerranBaselineId);
Fresh("fresh-invalid-civilization-count", 1, 250, 0, 0, SpeciesCatalog.TerranBaselineId);
Fresh("fresh-invalid-player-species", 1, 250, 1, 0, "missing_species");

(long Seed, StarSystemState[] Systems,
 IReadOnlyList<PlanetaryBodyState> Bodies,
 List<CivilizationState> Civilizations,
 IList<ColonyState> Colonies) WarpSetup()
{
    const long seed = 0x22AA55;
    var systems = new[] {
        new StarSystemState(0, "Sol", Vector2.Zero, StarArchetype.Standard, true,
            false, false, false, SolCatalogPreset.PresetId, StellarPrimaryClass.GYellowDwarf),
    };
    var bodies = new PlanetaryBodyGenerator().Generate(seed, systems);
    var civilization = new CivilizationState(
        42, "Warp Reservation", 0, CivilizationArchetype.Adaptive,
        new Game.Simulation.AI.CivilizationTraits(.1, .2, .3, .4, .5, .6, false),
        true, CivilizationDevelopmentStage.WarpCapable, false, true, false,
        SpeciesCatalog.TerranBaselineId)
    {
        Leadership = CivilizationLeadershipState.CreateFoundingRoster(42, true),
    };
    var civilizations = new List<CivilizationState> { civilization };
    var colonies = new ColonySeeder().Seed(civilizations, bodies);
    return (seed, systems, bodies, civilizations, colonies);
}
object? warpResult = null;
object? warpError = null;
var warpSetup = default((long Seed, StarSystemState[] Systems,
    IReadOnlyList<PlanetaryBodyState> Bodies,
    List<CivilizationState> Civilizations,
    IList<ColonyState> Colonies)?);
try { warpSetup = WarpSetup(); }
catch (Exception exception) { warpError = new { Type = exception.GetType().Name, exception.Message }; }
if (warpSetup is { } setup)
{
    var before = JsonSerializer.SerializeToElement(setup.Colonies, options);
    IList<FleetState>? fleets = null;
    try { fleets = new FleetSeeder().Seed(setup.Systems, setup.Civilizations, setup.Colonies); }
    catch (Exception exception) { warpError = new { Type = exception.GetType().Name, exception.Message }; }
    if (warpError is null)
    {
        var typedFleets = fleets!;
        var typedColonies = setup.Colonies;
        warpResult = new
        {
            setup.Seed,
            setup.Systems,
            setup.Bodies,
            setup.Civilizations,
            ColoniesBefore = before,
            Fleets = typedFleets,
            ColoniesAfter = typedColonies,
            PopulationBefore = before.EnumerateArray().Sum(colony => colony.GetProperty("PopulationMillions").GetDouble()),
            PopulationAfter = typedColonies.Sum(colony => colony.PopulationMillions),
            EmbarkedPopulation = typedFleets.Sum(fleet => fleet.EmbarkedPopulationMillions),
        };
    }
}
cases.Add(new
{
    Name = "warp-capable-real-population-reservation",
    Kind = "WarpComposition",
    Arguments = new { Seed = 0x22AA55L },
    Result = warpResult,
    Error = warpError
});

File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-fresh-campaign-oracle-v1",
    Cases = cases,
}, options) + Environment.NewLine);
