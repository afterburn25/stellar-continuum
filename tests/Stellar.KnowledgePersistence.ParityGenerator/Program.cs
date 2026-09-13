using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Persistence;
using Game.Simulation.Knowledge;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new()
    {
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
        WriteIndented = true,
    };
    private static readonly MethodInfo ToKnowledge = Method("ToKnowledge");
    private static readonly MethodInfo ToDtos = Method("ToKnowledgeDtos");
    private sealed record Row(string Name, string Operation, JsonNode Input,
        JsonNode Before, JsonNode After, JsonNode? Result, JsonNode? BeforeNext,
        JsonNode? AfterNext, JsonNode? NextResult, string? ErrorType, string? ErrorMessage);

    private static MethodInfo Method(string name) => typeof(CampaignSaveService)
        .GetMethods(BindingFlags.NonPublic | BindingFlags.Static).Single(x => x.Name == name);

    private static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        var output = Path.GetFullPath(args.Length > 0 ? args[0] : "knowledge-fixture.json");
        var root = Path.GetFullPath(args.Length > 1 ? args[1] : "src/Game");
        try
        {
            var rows = Rows();
            var paths = new[]
            {
                "Persistence/CampaignSaveService.cs",
                "Simulation/Knowledge/CivilizationKnowledgeState.cs",
            };
            var sources = paths.Select(path => new { Path = path, Sha256 = Convert.ToHexString(
                SHA256.HashData(File.ReadAllBytes(Path.Combine(root, path)))) }).ToArray();
            var document = new { SchemaVersion = 1, Authority = "actual CampaignSaveService knowledge adapters",
                SourceFiles = sources, RowCount = rows.Count, Rows = rows };
            File.WriteAllText(output, JsonSerializer.Serialize(document, Json) + Environment.NewLine,
                new UTF8Encoding(false));
            Console.WriteLine($"Campaign knowledge source oracle: {rows.Count}/{rows.Count} rows written.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            Console.Error.WriteLine($"Working directory: {Environment.CurrentDirectory}");
            Console.Error.WriteLine($"Source root: {root}");
            Console.Error.WriteLine($"Fixture path: {output}");
            return 1;
        }
    }

    private static List<Row> Rows()
    {
        var rows = new List<Row>();
        AddRestore(rows, "empty", new());
        AddRestore(rows, "null-top", null);
        AddRestore(rows, "null-entry", new() { null! });
        AddRestore(rows, "explored-without-access", new() { Entry(1, explored: true) });
        AddRestore(rows, "negative-core-unlock", new() { Entry(-1, access: true) });
        AddRestore(rows, "core-only", new() { Entry(8, access: true, explored: true) });
        AddRestore(rows, "legacy-known-systems", new() { Entry(2, knownSystems: new() { 9, 3, 9 }) });
        AddRestore(rows, "repeated-dtos", new()
        {
            Entry(2, knownSystems: new() { 9 }),
            Entry(2, knownSystems: new() { 3 }, knownCivilizations: new() { 7, 7 }),
        });
        AddRestore(rows, "explicit-all-levels", new() { Entry(3,
            knownSystems: new() { 8 }, knownCivilizations: new() { 5, 3 }, surveys: new()
            {
                Survey(1, SystemSurveyLevel.Unknown, .4),
                Survey(2, SystemSurveyLevel.Detected, .4),
                Survey(3, SystemSurveyLevel.PartiallySurveyed, .25),
                Survey(4, SystemSurveyLevel.FullySurveyed, .4),
            }) });
        AddRestore(rows, "unknown-survey-level-noop", new() { Entry(4, surveys: new()
            { Survey(9, (SystemSurveyLevel)99, .5) }) });
        AddRestore(rows, "unknown-only-sparse-observer", new() { Entry(44, surveys: new()
            { Survey(9, SystemSurveyLevel.Unknown, .5) }) });
        AddRestore(rows, "null-surveys-after-core", new() { Change(Entry(5, access: true), x => x.SystemSurveys = null!) });
        AddRestore(rows, "null-known-systems-legacy", new() { Change(Entry(5), x => x.KnownSystemIds = null!) });
        AddRestore(rows, "null-known-systems-explicit", new() { Change(Entry(5,
            surveys: new() { Survey(1, SystemSurveyLevel.Detected, 0) }), x => x.KnownSystemIds = null!) });
        AddRestore(rows, "null-survey-after-reveal", new() { Entry(5, knownSystems: new() { 7 },
            surveys: new() { null! }) });
        AddRestore(rows, "null-known-civilizations-last", new() { Change(Entry(5, knownSystems: new() { 7 },
            surveys: new() { Survey(8, SystemSurveyLevel.Detected, 0) }), x => x.KnownCivilizationIds = null!) });
        AddRestore(rows, "partial-nan", new() { Entry(6, surveys: new()
            { Survey(1, SystemSurveyLevel.PartiallySurveyed, double.NaN) }) });
        AddRestore(rows, "partial-positive-infinity", new() { Entry(6, surveys: new()
            { Survey(1, SystemSurveyLevel.PartiallySurveyed, double.PositiveInfinity) }) });
        AddRestore(rows, "partial-negative-infinity", new() { Entry(6, surveys: new()
            { Survey(1, SystemSurveyLevel.PartiallySurveyed, double.NegativeInfinity) }) });
        AddRestore(rows, "repeated-surveys", new() { Entry(6, surveys: new()
            { Survey(1, SystemSurveyLevel.PartiallySurveyed, .2), Survey(1, SystemSurveyLevel.PartiallySurveyed, .4),
              Survey(1, SystemSurveyLevel.FullySurveyed, 0), Survey(1, SystemSurveyLevel.Detected, 0) }) });
        AddRestore(rows, "success-with-continuation", new() { Entry(7, knownSystems: new() { 3 }) },
            state => state.AdvanceSystemSurvey(7, 4, .375));
        AddCapture(rows, "capture-empty", new());
        AddCapture(rows, "capture-core-only", Build(state => state.UnlockGalacticCoreAccess(12)));
        AddCapture(rows, "capture-mixed", Build(state =>
        {
            state.UnlockGalacticCoreAccess(9); state.RecordGalacticCoreExploration(9);
            state.RevealSystem(3, 8); state.AdvanceSystemSurvey(3, 2, .125);
            state.MarkSystemFullySurveyed(3, 7); state.RevealCivilization(3, 4);
        }));
        return rows;
    }

    private static CivilizationKnowledgeSaveDto Entry(int id, bool access = false, bool explored = false,
        List<int>? knownSystems = default, List<int>? knownCivilizations = default,
        List<SystemSurveySaveDto>? surveys = default)
    {
        return new()
        {
            CivilizationId = id, GalacticCoreAccessUnlocked = access, GalacticCoreExplored = explored,
            KnownSystemIds = knownSystems ?? new(), KnownCivilizationIds = knownCivilizations ?? new(),
            SystemSurveys = surveys ?? new(),
        };
    }

    private static SystemSurveySaveDto Survey(int id, SystemSurveyLevel level, double progress) =>
        new() { SystemId = id, Level = level, Progress = progress };

    private static CivilizationKnowledgeSaveDto Change(CivilizationKnowledgeSaveDto value,
        Action<CivilizationKnowledgeSaveDto> change)
    {
        change(value); return value;
    }

    private static CivilizationKnowledgeState Build(Action<CivilizationKnowledgeState> action)
    {
        var state = new CivilizationKnowledgeState(); action(state); return state;
    }

    private static void AddRestore(List<Row> rows, string name, List<CivilizationKnowledgeSaveDto>? input,
        Func<CivilizationKnowledgeState, object?>? next = null)
    {
        var inputNode = Node(new { EntriesPresent = input is not null, Entries = input });
        var before = inputNode.DeepClone(); object? typed = null; Exception? error = null;
        try { typed = ToKnowledge.Invoke(null, new object?[] { input }); }
        catch (Exception exception) { error = Unwrap(exception); }
        JsonNode? result = typed is null ? null : StateNode((CivilizationKnowledgeState)typed);
        JsonNode? beforeNext = null, afterNext = null, nextResult = null;
        if (typed is CivilizationKnowledgeState state && next is not null)
        {
            beforeNext = StateNode(state); var nextTyped = next(state); afterNext = StateNode(state);
            nextResult = Node(nextTyped);
        }
        rows.Add(new(name, "Restore", inputNode, before, Node(new { EntriesPresent = input is not null, Entries = input }),
            result, beforeNext, afterNext, nextResult, error?.GetType().Name, error?.Message));
    }

    private static void AddCapture(List<Row> rows, string name, CivilizationKnowledgeState state)
    {
        var input = StateNode(state); var before = input.DeepClone(); object? typed = null; Exception? error = null;
        try { typed = ToDtos.Invoke(null, new object[] { state }); }
        catch (Exception exception) { error = Unwrap(exception); }
        var result = typed is null ? null : Node(typed);
        rows.Add(new(name, "Capture", input, before, StateNode(state), result, null, null, null,
            error?.GetType().Name, error?.Message));
    }

    private static JsonNode StateNode(CivilizationKnowledgeState state)
    {
        var snapshot = state.Snapshot();
        var ids = snapshot.Systems.Keys.Concat(snapshot.Civilizations.Keys)
            .Concat(state.GetGalacticCoreObservers()).Distinct().OrderBy(id => id);
        return Node(ids.Select(id => new
        {
            Id = id, CoreAccess = state.HasGalacticCoreAccess(id), CoreExplored = state.IsGalacticCoreDiscovered(id),
            KnownSystems = state.GetKnownSystems(id), KnownCivilizations = state.GetKnownCivilizations(id),
            Surveys = state.GetSystemSurveyKnowledge(id),
        }).ToArray());
    }

    private static JsonNode Node(object? value) => JsonSerializer.SerializeToNode(value, Json)
        ?? JsonValue.Create((string?)null)!;

    private static Exception Unwrap(Exception error)
    {
        while (error is TargetInvocationException { InnerException: not null }) error = error.InnerException;
        return error;
    }
}
