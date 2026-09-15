using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
var options = new JsonSerializerOptions
{
    IncludeFields = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
};
var cases = new List<object>();

StarSystemState System(int id = 7, StarArchetype archetype = StarArchetype.Standard,
    StellarPrimaryClass? primary = StellarPrimaryClass.GYellowDwarf,
    string name = "Target") =>
    new(id, name, new Vector2(12.5f, -3.25f), archetype, true, true, true, true,
        "fixture-preset", primary, StellarPrimaryClass.KOrangeDwarf,
        StellarPrimaryClass.MRedDwarf, 4.5, "fixture-catalog");

PlanetaryBodyState Body(int id, PlanetaryBodyKind kind = PlanetaryBodyKind.Planet,
    double radiation = 0.1, bool anomaly = false, bool rare = false, int systemId = 7) =>
    new(id, systemId, kind == PlanetaryBodyKind.Moon ? 1 : null, id, $"Body {id}", kind,
        1.25, 2.5,
        new PlanetaryEnvironmentState(1.0, 280.0, 101.0,
            PlanetaryAtmosphereRegime.OxygenNitrogen, PlanetarySolventRegime.Water,
            radiation, false, true),
        true, rare, anomaly, false, 0.02, 1.5);

GalaxyState Galaxy(IReadOnlyList<StarSystemState> systems,
    IReadOnlyList<PlanetaryBodyState> bodies) => new()
    {
        Seed = 24,
        Systems = systems,
        PlanetaryBodies = bodies,
        Civilizations = [],
        Fleets = [],
        Colonies = [],
        Economies = [],
        Technologies = [],
        ConstructionStates = [],
        ShipyardStates = [],
        PlayerCivilizationId = 0,
        Knowledge = new CivilizationKnowledgeState(),
    };

void Profile(string name, int systemId, IReadOnlyList<StarSystemState> systems,
    IReadOnlyList<PlanetaryBodyState> bodies)
{
    var arguments = new { SystemId = systemId, Systems = systems, Bodies = bodies };
    var galaxy = Galaxy(systems, bodies);
    SurveyOperationsProfile? profile = null;
    object? error = null;
    try { profile = new SurveyOperationsProfiler().Build(galaxy, systemId); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    object? result = profile is null ? null : new
    {
        profile.SystemId,
        profile.PlanetCount,
        profile.MoonCount,
        profile.EstimatedScienceSurveyDays,
        profile.OperationalHazard,
        profile.ProgressPerDay,
    };
    cases.Add(new { Name = name, Kind = "Profile", Arguments = arguments, Result = result, Error = error });
}

Profile("missing-positive", 99, [System()], []);
Profile("missing-negative", -1, [System()], [Body(1)]);
Profile("empty-minimum", 7, [System()], []);
Profile("one-planet-minimum", 7, [System()], [Body(1)]);
Profile("two-planets", 7, [System()], [Body(1), Body(2)]);
Profile("planet-and-moon", 7, [System()], [Body(1), Body(2, PlanetaryBodyKind.Moon)]);
Profile("dwarf-is-planet", 7, [System()], [Body(1, PlanetaryBodyKind.DwarfPlanet)]);
Profile("unknown-kind-is-planet", 7, [System()], [Body(1, (PlanetaryBodyKind)99)]);

foreach (var archetype in Enum.GetValues<StarArchetype>())
    Profile($"archetype-{archetype}", 7, [System(archetype: archetype, primary: null)], []);
foreach (var primary in Enum.GetValues<StellarPrimaryClass>())
    Profile($"primary-{primary}", 7, [System(primary: primary)], []);
Profile("unknown-archetype-default", 7, [System(archetype: (StarArchetype)99)], []);
Profile("unknown-primary-default", 7, [System(primary: (StellarPrimaryClass)99)], []);
Profile("content-star-maximum-not-sum", 7,
    [System(archetype: StarArchetype.Nebula, primary: StellarPrimaryClass.BlackHole)], []);
Profile("dangerous-beats-giant-work", 7,
    [System(archetype: StarArchetype.Dangerous, primary: StellarPrimaryClass.Giant)], []);

Profile("one-anomaly", 7, [System()], [Body(1, anomaly: true)]);
Profile("three-anomalies", 7, [System()], Enumerable.Range(1, 3).Select(i => Body(i, anomaly: true)).ToArray());
Profile("four-anomalies-cap", 7, [System()], Enumerable.Range(1, 4).Select(i => Body(i, anomaly: true)).ToArray());
Profile("one-rare", 7, [System()], [Body(1, rare: true)]);
Profile("three-rare", 7, [System()], Enumerable.Range(1, 3).Select(i => Body(i, rare: true)).ToArray());
Profile("four-rare-cap", 7, [System()], Enumerable.Range(1, 4).Select(i => Body(i, rare: true)).ToArray());
Profile("combined-followup-caps", 7, [System()], Enumerable.Range(1, 5).Select(i => Body(i, anomaly: true, rare: true)).ToArray());
Profile("maximum-duration", 7, [System()], Enumerable.Range(1, 40).Select(i => Body(i, anomaly: true, rare: true)).ToArray());

Profile("radiation-below-elevated", 7, [System()], [Body(1, radiation: 0.399999999)]);
Profile("radiation-elevated-boundary", 7, [System()], [Body(1, radiation: 0.4)]);
Profile("radiation-below-severe", 7, [System()], [Body(1, radiation: 0.719999999)]);
Profile("radiation-severe-boundary", 7, [System()], [Body(1, radiation: 0.72)]);
Profile("radiation-positive-infinity", 7, [System()], [Body(1, radiation: double.PositiveInfinity)]);
Profile("radiation-negative-infinity", 7, [System()], [Body(1, radiation: double.NegativeInfinity)]);
Profile("radiation-all-nan", 7, [System()], [Body(1, radiation: double.NaN), Body(2, radiation: double.NaN)]);
Profile("radiation-nan-then-elevated", 7, [System()], [Body(1, radiation: double.NaN), Body(2, radiation: 0.5)]);
Profile("radiation-elevated-then-nan", 7, [System()], [Body(1, radiation: 0.5), Body(2, radiation: double.NaN)]);

Profile("duplicate-system-first-routine", 7,
    [System(name: "first"), System(archetype: StarArchetype.Dangerous, name: "second")], []);
Profile("unrelated-body-ignored", 7, [System()],
    [Body(1), Body(2, radiation: double.PositiveInfinity, anomaly: true, rare: true, systemId: 8)]);
Profile("body-order-a", 7, [System()],
    [Body(1, radiation: 0.2, anomaly: true), Body(2, PlanetaryBodyKind.Moon, 0.5, rare: true)]);
Profile("body-order-b-equivalent", 7, [System()],
    [Body(2, PlanetaryBodyKind.Moon, 0.5, rare: true), Body(1, radiation: 0.2, anomaly: true)]);

File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-survey-operations-oracle-v1",
    Cases = cases,
    NativeBoundary = new
    {
        NullGalaxy = "C# throws ArgumentNullException; native typed spans cannot represent null.",
        NullSystemRecord = "C# reference lists can contain null; native StellarSystem spans cannot.",
        NullBodyRecord = "C# reference lists can contain null; native PlanetaryBody spans cannot.",
        NullEnvironment = "C# PlanetaryBodyState can be constructed with a null environment; native embeds it by value.",
    },
}, options) + Environment.NewLine);
