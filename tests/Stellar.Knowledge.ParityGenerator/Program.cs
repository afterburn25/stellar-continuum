using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;

var json = new JsonSerializerOptions
{
    IncludeFields = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    WriteIndented = true,
};
var cases = new List<object>();
var observerUniverse = new[] { -2, -1, 0, 1, 2, 3, 99 };
var systemUniverse = new[] { -5, 1, 2, 3, 4, 5, 99 };

StarSystemState System(int id, float x, float y, double? depth = null) =>
    new(id, $"System {id}", new Vector2(x, y), StarArchetype.Standard,
        false, false, false, false, GalacticDepthLightYears: depth);

CivilizationState Civilization(int id, int homeSystemId) =>
    new(id, $"Civilization {id}", homeSystemId, CivilizationArchetype.Scientific,
        CivilizationTraits.Balanced, id == 1, CivilizationDevelopmentStage.WarpCapable);

var sensorSystems = new[]
{
    System(1, 0, 0, 0), System(2, 3, 0, 0), System(3, 3.1f, 0, 0),
    System(4, 0, 0, 4), System(5, 0, 0, 4.1),
};
var flatSensorSystems = new[]
{
    System(1, 0, 0), System(2, 3, 4), System(3, 5.1f, 0),
};

JsonElement Snapshot(object? value) => JsonSerializer.SerializeToElement(value, json);

object Observe(CivilizationKnowledgeState knowledge) => new
{
    Observers = observerUniverse.Select(observer => new
    {
        CivilizationId = observer,
        KnownSystems = knowledge.GetKnownSystems(observer).ToArray(),
        KnownCivilizations = knowledge.GetKnownCivilizations(observer).ToArray(),
        Surveys = knowledge.GetSystemSurveyKnowledge(observer).ToArray(),
        QuerySystems = systemUniverse.Select(system => new
        {
            SystemId = system,
            Known = knowledge.IsSystemKnown(observer, system),
            FullySurveyed = knowledge.IsSystemFullySurveyed(observer, system),
            Level = knowledge.GetSystemSurveyLevel(observer, system),
            Progress = knowledge.GetSystemSurveyProgress(observer, system),
        }).ToArray(),
        CoreAccess = knowledge.HasGalacticCoreAccess(observer),
        CoreDiscovered = knowledge.IsGalacticCoreDiscovered(observer),
    }).ToArray(),
    CoreObservers = knowledge.GetGalacticCoreObservers().ToArray(),
};

Command Reveal(int civilizationId, int systemId) => new("RevealSystem", civilizationId, systemId);
Command Recon(int civilizationId, int systemId, double progress) => new("RecordReconnaissance", civilizationId, systemId, Progress: progress);
Command Advance(int civilizationId, int systemId, double progress) => new("AdvanceSystemSurvey", civilizationId, systemId, Progress: progress);
Command Full(int civilizationId, int systemId) => new("MarkSystemFullySurveyed", civilizationId, systemId);
Command Contact(int observerId, int targetId) => new("RevealCivilization", observerId, TargetCivilizationId: targetId);
Command CoreUnlock(int civilizationId) => new("UnlockGalacticCoreAccess", civilizationId);
Command CoreExplore(int civilizationId) => new("RecordGalacticCoreExploration", civilizationId);
Command Sensor(int civilizationId, int originSystemId, float range) => new("RevealWithinSensorRange", civilizationId, originSystemId, SensorRange: range, Systems: sensorSystems);
Command Query(int civilizationId, int systemId, int targetCivilizationId) => new("Queries", civilizationId, systemId, targetCivilizationId);
Command Initial(StarSystemState[] systems, CivilizationState[] civilizations, float range) => new("CreateInitial", SensorRange: range, Systems: systems, Civilizations: civilizations);

object? Dispatch(CivilizationKnowledgeState knowledge, Command command) => command.Operation switch
{
    "RevealSystem" => knowledge.RevealSystem(command.CivilizationId!.Value, command.SystemId!.Value),
    "RecordReconnaissance" => knowledge.RecordReconnaissance(command.CivilizationId!.Value, command.SystemId!.Value, command.Progress!.Value),
    "AdvanceSystemSurvey" => knowledge.AdvanceSystemSurvey(command.CivilizationId!.Value, command.SystemId!.Value, command.Progress!.Value),
    "MarkSystemFullySurveyed" => knowledge.MarkSystemFullySurveyed(command.CivilizationId!.Value, command.SystemId!.Value),
    "RevealCivilization" => knowledge.RevealCivilization(command.CivilizationId!.Value, command.TargetCivilizationId!.Value),
    "UnlockGalacticCoreAccess" => Unlock(knowledge, command.CivilizationId!.Value),
    "RecordGalacticCoreExploration" => knowledge.RecordGalacticCoreExploration(command.CivilizationId!.Value),
    "RevealWithinSensorRange" => knowledge.RevealWithinSensorRange(command.CivilizationId!.Value, command.SystemId!.Value, command.Systems!, command.SensorRange!.Value),
    "Queries" => new
    {
        Known = knowledge.IsSystemKnown(command.CivilizationId!.Value, command.SystemId!.Value),
        FullySurveyed = knowledge.IsSystemFullySurveyed(command.CivilizationId!.Value, command.SystemId!.Value),
        Level = knowledge.GetSystemSurveyLevel(command.CivilizationId!.Value, command.SystemId!.Value),
        Progress = knowledge.GetSystemSurveyProgress(command.CivilizationId!.Value, command.SystemId!.Value),
        Systems = knowledge.GetKnownSystems(command.CivilizationId!.Value).ToArray(),
        Contact = knowledge.IsCivilizationKnown(command.CivilizationId!.Value, command.TargetCivilizationId!.Value),
        Contacts = knowledge.GetKnownCivilizations(command.CivilizationId!.Value).ToArray(),
    },
    "Snapshot" => knowledge.Snapshot(),
    "CreateInitial" => CivilizationKnowledgeState.CreateInitial(command.Systems!, command.Civilizations!, command.SensorRange!.Value),
    _ => throw new InvalidOperationException($"Unknown source command {command.Operation}."),
};

object? Unlock(CivilizationKnowledgeState knowledge, int civilizationId)
{
    knowledge.UnlockGalacticCoreAccess(civilizationId);
    return null;
}

object Execute(CivilizationKnowledgeState knowledge, Command command)
{
    object? rawResult = null;
    Exception? caught = null;
    try
    {
        rawResult = Dispatch(knowledge, command);
    }
    catch (Exception exception)
    {
        caught = exception;
    }

    var frozenResult = rawResult is CivilizationKnowledgeState created
        ? Snapshot(Observe(created))
        : Snapshot(rawResult);
    return new
    {
        Command = Snapshot(command),
        Result = frozenResult,
        Error = caught is null ? (JsonElement?)null : Snapshot(new { Type = caught.GetType().Name, caught.Message }),
    };
}

void Case(string name, string kind, Command[] setupCommands, Command[] commands)
{
    var knowledge = new CivilizationKnowledgeState();
    var setupResults = new List<object>();
    foreach (var command in setupCommands)
        setupResults.Add(Execute(knowledge, command));
    if (setupResults.Select(Snapshot).Any(result => result.TryGetProperty("Error", out _)))
        throw new InvalidOperationException($"Setup command failed for {name}.");
    var before = Snapshot(Observe(knowledge));
    var commandResults = new List<object>();
    foreach (var command in commands)
        commandResults.Add(Execute(knowledge, command));
    var after = Snapshot(Observe(knowledge));
    cases.Add(new
    {
        Name = name,
        Kind = kind,
        Arguments = new { SetupCommands = setupCommands, Commands = commands },
        SetupResults = setupResults,
        Result = commandResults,
        Before = before,
        After = after,
    });
}

Case("query-empty", "Queries", [], [Query(1, 1, 2)]);
Case("reveal-new", "RevealSystem", [], [Reveal(1, 2)]);
Case("reveal-repeat", "RevealSystem", [Reveal(1, 2)], [Reveal(1, 2)]);
Case("reveal-negative-id", "RevealSystem", [], [Reveal(-1, -5)]);
Case("recon-default", "Reconnaissance", [], [Recon(1, 2, .35)]);
Case("recon-zero-floor", "Reconnaissance", [], [Recon(1, 2, 0)]);
Case("recon-negative-floor", "Reconnaissance", [], [Recon(1, 2, -2)]);
Case("recon-nan-floor", "Reconnaissance", [], [Recon(1, 2, double.NaN)]);
Case("recon-fully-surveyed-noop", "Reconnaissance", [Full(1, 2)], [Recon(1, 2, .9)]);
Case("advance-zero-noop", "AdvanceSystemSurvey", [], [Advance(1, 2, 0)]);
Case("advance-negative-noop", "AdvanceSystemSurvey", [], [Advance(1, 2, -.2)]);
Case("advance-nan-progress", "AdvanceSystemSurvey", [], [Advance(1, 2, double.NaN)]);
Case("advance-partial", "AdvanceSystemSurvey", [], [Advance(1, 2, .4)]);
Case("advance-to-full", "AdvanceSystemSurvey", [Recon(1, 2, .35)], [Advance(1, 2, .7)]);
Case("advance-after-full-noop", "AdvanceSystemSurvey", [Full(1, 2)], [Advance(1, 2, 1)]);
Case("mark-fully-surveyed", "MarkSystemFullySurveyed", [], [Full(1, 3)]);
Case("mark-fully-surveyed-repeat", "MarkSystemFullySurveyed", [Full(1, 3)], [Full(1, 3)]);
Case("contact-self", "RevealCivilization", [], [Contact(1, 1)]);
Case("contact-other-repeat", "RevealCivilization", [], [Contact(1, 2), Contact(1, 2), Contact(2, 1)]);
Case("contact-negative-ids", "RevealCivilization", [], [Contact(-1, -2)]);
Case("core-explore-locked", "CoreExplore", [], [CoreExplore(1)]);
Case("core-unlock-negative-error", "CoreUnlock", [], [CoreUnlock(-1)]);
Case("core-unlock-explore-repeat", "CoreSequence", [], [CoreUnlock(2), CoreExplore(2), CoreExplore(2)]);
Case("sensor-exact-boundary", "SensorReveal", [], [Sensor(1, 1, 3)]);
Case("sensor-depth-boundary", "SensorReveal", [], [Sensor(1, 1, 4)]);
Case("sensor-missing-origin-error", "SensorReveal", [], [Sensor(1, 99, 4)]);
Case("sensor-nan-range", "SensorReveal", [], [Sensor(1, 1, float.NaN)]);
Case("sensor-repeated-noops", "SensorReveal", [], [Sensor(1, 1, 3), Sensor(1, 1, 3)]);
Case("sensor-absent-depth", "SensorReveal", [],
     [new Command("RevealWithinSensorRange", 1, 1, SensorRange: 5, Systems: flatSensorSystems)]);
Case("sensor-negative-range", "SensorReveal", [], [Sensor(1, 1, -3)]);
Case("sensor-infinite-range", "SensorReveal", [], [Sensor(1, 1, float.PositiveInfinity)]);
Case("create-initial-multiple-civilizations", "CreateInitial", [], [Initial(sensorSystems, [Civilization(1, 1), Civilization(2, 4)], 3)]);
Case("snapshot-narrow-state", "Snapshot",
     [Reveal(1, 2), Recon(1, 3, .4), Contact(1, 2), CoreUnlock(1)],
     [new Command("Snapshot")]);
Case("snapshot-observer-insertion-order", "Snapshot",
     [Reveal(3, 1), Reveal(-2, 2), Contact(3, 1), Contact(-2, 0)],
     [new Command("Snapshot")]);

if (cases.Count != 34)
    throw new InvalidOperationException($"Expected 34 cases, got {cases.Count}.");

var revealNew = cases.Select(Snapshot).Single(item =>
    item.GetProperty("Name").GetString() == "reveal-new");
var revealBefore = revealNew.GetProperty("Before").GetProperty("Observers")
    .EnumerateArray().Single(observer => observer.GetProperty("CivilizationId").GetInt32() == 1);
var revealAfter = revealNew.GetProperty("After").GetProperty("Observers")
    .EnumerateArray().Single(observer => observer.GetProperty("CivilizationId").GetInt32() == 1);
if (revealBefore.GetProperty("KnownSystems").GetArrayLength() != 0 ||
    !revealAfter.GetProperty("KnownSystems").EnumerateArray().Any(system => system.GetInt32() == 2))
{
    throw new InvalidOperationException("The reveal-new snapshot must retain its empty pre-state and reveal system 2 afterward.");
}

File.WriteAllText(args[0], JsonSerializer.Serialize(new
{
    Format = "stellar-knowledge-oracle-v2",
    ObserverUniverse = observerUniverse,
    SystemUniverse = systemUniverse,
    Cases = cases,
}, json) + Environment.NewLine);

sealed record Command(
    string Operation,
    int? CivilizationId = null,
    int? SystemId = null,
    int? TargetCivilizationId = null,
    double? Progress = null,
    float? SensorRange = null,
    StarSystemState[]? Systems = null,
    CivilizationState[]? Civilizations = null);
