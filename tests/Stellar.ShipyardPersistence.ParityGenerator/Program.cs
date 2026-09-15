using System.Reflection;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using Game.Persistence;
using Game.Simulation.AI;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new()
    {
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
    };
    private static readonly MethodInfo RestoreMethod = typeof(CampaignSaveService)
        .GetMethod("ToShipyardStates", BindingFlags.NonPublic | BindingFlags.Static)!;
    private static readonly MethodInfo CaptureMethod = typeof(CampaignSaveService)
        .GetMethod("ToShipyardDtos", BindingFlags.NonPublic | BindingFlags.Static)!;
    private static readonly FieldInfo QueueField = typeof(ShipyardState)
        .GetField("_queuedBuilds", BindingFlags.NonPublic | BindingFlags.Instance)!;

    private sealed record Row(string Name, string Operation, int SaveFormatVersion,
        JsonNode Input, JsonNode Before, JsonNode After, JsonNode? Result,
        string? ErrorType, string? ErrorMessage, bool SourceOnly = false,
        string? SourceOnlyReason = null);

    private static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        var output = args.Length > 0 ? Path.GetFullPath(args[0]) :
            Path.GetFullPath("shipyard-persistence-fixture.json");
        var sourceRoot = args.Length > 1 ? Path.GetFullPath(args[1]) :
            Path.GetFullPath("src/Game");
        try
        {
            var rows = Rows();
            var sources = new[]
            {
                "Persistence/CampaignSaveService.cs",
                "Simulation/Shipbuilding/ShipyardState.cs",
                "Simulation/Shipbuilding/ShipDesignRegistry.cs",
                "Simulation/Species/SpeciesCatalog.cs",
            }.Select(path => new
            {
                Path = path,
                Sha256 = Convert.ToHexString(SHA256.HashData(
                    File.ReadAllBytes(Path.Combine(sourceRoot, path)))),
            }).ToArray();
            var document = new
            {
                SchemaVersion = 1,
                Authority = "actual private CampaignSaveService shipyard restore/capture helpers",
                SourceFiles = sources,
                RowCount = rows.Count,
                Rows = rows,
            };
            File.WriteAllText(output, JsonSerializer.Serialize(document,
                new JsonSerializerOptions(Json) { WriteIndented = true }) +
                Environment.NewLine, new UTF8Encoding(false));
            Console.WriteLine($"Shipyard persistence source oracle: {rows.Count}/{rows.Count} rows written.");
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
        AddRestore(rows, "restore-valid-current", 16, _ => { });
        AddRestore(rows, "restore-negative-empty-legacy-population", 16, dto =>
        {
            ClearActive(dto); dto.ReservedPopulationMillions = -2;
        });
        AddRestore(rows, "restore-negative-population-with-authorization", 16, dto =>
        {
            ClearActive(dto); dto.ReservedPopulationMillions = -2;
            dto.ActiveAuthorizationCredits = 1;
        });
        AddRestore(rows, "restore-negative-population-with-identity", 16, dto =>
        {
            ClearActive(dto);
            dto.ActiveOrderId = "identity";
            dto.ReservedPopulationMillions = -2;
        });
        AddRestore(rows, "restore-negative-population-with-species", 16, dto =>
        {
            ClearActive(dto); dto.ReservedPopulationMillions = -2;
            dto.ReservedPopulationSpeciesId = SpeciesCatalog.TerranBaselineId;
        });
        AddRestore(rows, "restore-negative-population-with-source-colony", 16, dto =>
        {
            ClearActive(dto); dto.ReservedPopulationMillions = -2;
            dto.ReservedPopulationSourceColonyId = 9;
        });
        AddRestore(rows, "restore-unknown-active-empty-discard", 16, dto =>
        {
            dto.ActiveDesignId = "unknown"; dto.ActiveOrderId = null;
            dto.ActiveBuildProgress = 0; dto.ActiveAuthorizationCredits = 0;
        });
        AddRestore(rows, "restore-unknown-active-authorization-fails", 16, dto =>
        {
            dto.ActiveDesignId = "unknown"; dto.ActiveOrderId = null;
            dto.ActiveBuildProgress = 0; dto.ActiveAuthorizationCredits = 1;
        });
        AddRestore(rows, "restore-unknown-active-identity-fails", 16, dto =>
        {
            dto.ActiveDesignId = "unknown"; dto.ActiveOrderId = "legacy-id";
            dto.ActiveBuildProgress = 0; dto.ActiveAuthorizationCredits = 0;
        });
        AddRestore(rows, "restore-unknown-active-population-fails", 16, dto =>
        {
            dto.ActiveDesignId = "unknown"; dto.ActiveOrderId = null;
            dto.ActiveBuildProgress = 0; dto.ActiveAuthorizationCredits = 0;
            dto.ReservedPopulationMillions = 1;
        });
        AddRestore(rows, "restore-active-population-without-design", 16, dto =>
        {
            dto.ActiveDesignId = null; dto.ActiveOrderId = null;
            dto.ActiveBuildProgress = 0; dto.ActiveAuthorizationCredits = 0;
            dto.ReservedPopulationMillions = 1;
            dto.ReservedPopulationSpeciesId = SpeciesCatalog.TerranBaselineId;
        });
        AddRestore(rows, "restore-current-population-species", 16, dto =>
        {
            dto.ActiveDesignId = ShipDesignRegistry.ColonyShipId;
            dto.ActiveOrderId = null; dto.ActiveBuildProgress = 0;
            dto.ReservedPopulationMillions = 1.25;
            dto.ReservedPopulationSpeciesId = SpeciesCatalog.PelagicHighPressureId;
            dto.ReservedPopulationSourceColonyId = 77;
        });
        AddRestore(rows, "restore-current-missing-species", 16, dto =>
        {
            dto.ActiveDesignId = ShipDesignRegistry.ColonyShipId;
            dto.ActiveOrderId = null; dto.ActiveBuildProgress = 0;
            dto.ReservedPopulationMillions = 1.25;
            dto.ReservedPopulationSpeciesId = null;
        });
        AddRestore(rows, "restore-legacy-species-from-civilization", 7, dto =>
        {
            dto.ActiveDesignId = ShipDesignRegistry.ColonyShipId;
            dto.ActiveOrderId = null; dto.ActiveBuildProgress = 0;
            dto.ReservedPopulationMillions = 1.25;
            dto.ReservedPopulationSpeciesId = "ignored-unknown";
        });
        AddRestore(rows, "restore-legacy-missing-civilization", 7, dto =>
        {
            dto.CivilizationId = 999;
            dto.ActiveDesignId = ShipDesignRegistry.ColonyShipId;
            dto.ActiveOrderId = null; dto.ActiveBuildProgress = 0;
            dto.ReservedPopulationMillions = 1;
        });
        AddRestore(rows, "restore-legacy-active-id", 16, dto => dto.ActiveOrderId = null);
        AddRestore(rows, "restore-accepted-index-legacy-queue-id", 16, dto =>
        {
            dto.ActiveDesignId = null; dto.ActiveOrderId = null;
            dto.ActiveBuildProgress = 0; dto.ActiveAuthorizationCredits = 0;
            dto.QueuedBuilds.Insert(0, new() { DesignId = "unknown" });
            dto.QueuedBuilds[1].OrderId = null;
        });
        AddRestore(rows, "restore-duplicate-explicit-identities", 16, dto =>
            dto.QueuedBuilds[0].OrderId = dto.ActiveOrderId);
        AddRestore(rows, "restore-legacy-id-collision", 16, dto =>
        {
            dto.ActiveOrderId = null;
            dto.QueuedBuilds[0].OrderId = $"legacy-{dto.CivilizationId}-active";
        });
        AddRestore(rows, "restore-canonical-sequence-not-after", 16, dto =>
            dto.NextOrderSequence = 2);
        AddRestore(rows, "restore-null-queue", 16, dto => dto.QueuedBuilds = null!);
        AddRestore(rows, "restore-null-queue-item-source-only", 16,
            dto => dto.QueuedBuilds.Insert(0, null!));
        rows[^1] = rows[^1] with
        {
            SourceOnly = true,
            SourceOnlyReason = "Source DTO lists can contain null reference elements; native typed DTO vectors contain values only."
        };
        AddRestore(rows, "restore-nonfinite-active-progress", 16, dto => dto.ActiveBuildProgress = double.NaN);
        AddRestore(rows, "restore-progress-over-materials", 16, dto => dto.ActiveBuildProgress = 650.0002);
        AddRestore(rows, "restore-invalid-order-before-duplicate", 16, dto =>
        {
            dto.ActiveOrderId = "bad id";
            dto.QueuedBuilds[0].OrderId = "bad id";
        });
        AddRestore(rows, "restore-unknown-queued-empty-discard", 16, dto =>
            dto.QueuedBuilds.Insert(0, new() { DesignId = "unknown" }));
        AddRestore(rows, "restore-unknown-queued-authorization-fails", 16, dto =>
            dto.QueuedBuilds.Insert(0, new() { DesignId = "unknown", AuthorizationCredits = 1 }));
        AddRestore(rows, "restore-unknown-queued-identity-fails", 16, dto =>
            dto.QueuedBuilds.Insert(0, new() { DesignId = "unknown", OrderId = "keep-me" }));
        AddRestore(rows, "restore-unknown-queued-population-fails", 16, dto =>
            dto.QueuedBuilds.Insert(0, new() { DesignId = "unknown", ReservedPopulationMillions = 1,
                ReservedPopulationSpeciesId = SpeciesCatalog.TerranBaselineId }));
        AddRestore(rows, "restore-capacity-counts-accepted-not-raw", 16, dto =>
        {
            dto.ActiveDesignId = null; dto.ActiveOrderId = null;
            dto.ActiveBuildProgress = 0; dto.ActiveAuthorizationCredits = 0;
            dto.QueuedBuilds.Clear();
            dto.QueuedBuilds.Add(new() { DesignId = "unknown" });
            for (var index = 0; index < ShipyardState.MaxPendingBuilds; ++index)
                dto.QueuedBuilds.Add(new() { DesignId = "warp_scout" });
        });
        AddRestore(rows, "restore-capacity-population-overflow-fails", 16, dto =>
        {
            dto.QueuedBuilds.Clear();
            for (var index = 0; index < ShipyardState.MaxPendingBuilds - 1; ++index)
                dto.QueuedBuilds.Add(new() { DesignId = "warp_scout" });
            dto.QueuedBuilds.Add(new() { DesignId = ShipDesignRegistry.ColonyShipId,
                ReservedPopulationMillions = 1,
                ReservedPopulationSpeciesId = SpeciesCatalog.TerranBaselineId });
        });

        AddCapture(rows, "capture-valid-detached", _ => { });
        AddCapture(rows, "capture-valid-population-assets", state =>
        {
            state.ActiveDesignId = ShipDesignRegistry.ColonyShipId;
            state.ReservedPopulationMillions = 1.25;
            state.ReservedPopulationSpeciesId = SpeciesCatalog.PelagicHighPressureId;
            state.ReservedPopulationSourceColonyId = 77;
            RawQueue(state)[0] = new ShipBuildOrderState
            {
                OrderId = "shipyard-3-2", DesignId = ShipDesignRegistry.ColonyShipId,
                AuthorizationCredits = 11, ReservedPopulationMillions = 2.5,
                ReservedPopulationSpeciesId = SpeciesCatalog.TerranBaselineId,
                ReservedPopulationSourceColonyId = 78,
            };
        });
        AddCapture(rows, "capture-active-population-missing-species", state =>
        {
            state.ActiveDesignId = ShipDesignRegistry.ColonyShipId;
            state.ReservedPopulationMillions = 1;
            state.ReservedPopulationSpeciesId = null;
        });
        AddCapture(rows, "capture-queued-population-missing-species", state =>
        {
            RawQueue(state)[0] = new ShipBuildOrderState
            {
                OrderId = "shipyard-3-2", DesignId = ShipDesignRegistry.ColonyShipId,
                ReservedPopulationMillions = 1,
            };
        });
        AddCapture(rows, "capture-negative-population-rejected", state => state.ReservedPopulationMillions = -1);
        AddCapture(rows, "capture-active-accounting-without-design", state =>
        {
            state.ActiveDesignId = null; state.ActiveOrderId = null;
            state.ActiveBuildProgress = 1;
        });
        AddCapture(rows, "capture-unknown-active-empty-discard", state =>
        {
            state.ActiveDesignId = "unknown"; state.ActiveOrderId = null;
            state.ActiveBuildProgress = 0; state.ActiveAuthorizationCredits = 0;
        });
        AddCapture(rows, "capture-unknown-active-authorization-fails", state =>
        {
            state.ActiveDesignId = "unknown"; state.ActiveOrderId = null;
            state.ActiveBuildProgress = 0; state.ActiveAuthorizationCredits = 1;
        });
        AddCapture(rows, "capture-unknown-active-identity-fails", state =>
        {
            state.ActiveDesignId = "unknown"; state.ActiveOrderId = "keep-me";
            state.ActiveBuildProgress = 0; state.ActiveAuthorizationCredits = 0;
        });
        AddCapture(rows, "capture-unknown-active-population-fails", state =>
        {
            state.ActiveDesignId = "unknown"; state.ActiveOrderId = null;
            state.ActiveBuildProgress = 0; state.ActiveAuthorizationCredits = 0;
            state.ReservedPopulationMillions = 1;
        });
        AddCapture(rows, "capture-raw-index-overflow-empty-drop", state =>
        {
            var queue = RawQueue(state); queue.Clear();
            for (var index = 0; index < ShipyardState.MaxPendingBuilds; ++index)
                queue.Add(new() { DesignId = "unknown" });
            queue.Add(new() { DesignId = "warp_scout" });
        });
        AddCapture(rows, "capture-raw-index-overflow-authorization-fails", state =>
        {
            var queue = RawQueue(state); queue.Clear();
            for (var index = 0; index < ShipyardState.MaxPendingBuilds; ++index)
                queue.Add(new() { DesignId = "unknown" });
            queue.Add(new() { DesignId = "warp_scout", AuthorizationCredits = 1 });
        });
        AddCapture(rows, "capture-accepted-capacity-overflow-empty-drop", state =>
        {
            var queue = RawQueue(state); queue.Clear();
            for (var index = 0; index < ShipyardState.MaxPendingBuilds; ++index)
                queue.Add(new() { DesignId = "warp_scout" });
        });
        AddCapture(rows, "capture-accepted-capacity-identity-fails", state =>
        {
            var queue = RawQueue(state); queue.Clear();
            for (var index = 0; index < ShipyardState.MaxPendingBuilds - 1; ++index)
                queue.Add(new() { DesignId = "warp_scout" });
            queue.Add(new() { DesignId = "warp_scout", OrderId = "keep-me" });
        });
        AddCapture(rows, "capture-identity-validation-remains-data-error", state =>
            state.NextOrderSequence = 2);
        AddEmptyRestore(rows);
        AddEmptyCapture(rows);
        return rows;
    }

    private static void AddEmptyRestore(List<Row> rows)
    {
        var dtos = new List<ShipyardSaveDto>();
        var civilizations = Civilizations();
        var input = JsonSerializer.SerializeToNode(new
        {
            Dtos = dtos,
            Civilizations = CivilizationJson(civilizations),
        }, Json)!;
        object? typedResult = null;
        Exception? error = null;
        try
        {
            typedResult = RestoreMethod.Invoke(null,
                new object[] { dtos, civilizations, 16 });
        }
        catch (TargetInvocationException exception)
        {
            error = exception.InnerException ?? exception;
        }
        rows.Add(new("restore-empty", "Restore", 16, input,
            input.DeepClone(), JsonSerializer.SerializeToNode(new
            {
                Dtos = dtos,
                Civilizations = CivilizationJson(civilizations),
            }, Json)!, typedResult is null ? null :
                StateListJson((IList<ShipyardState>)typedResult),
            error?.GetType().Name, error?.Message));
    }

    private static void AddEmptyCapture(List<Row> rows)
    {
        var states = Array.Empty<ShipyardState>();
        var input = StateListJson(states);
        object? typedResult = null;
        Exception? error = null;
        try
        {
            typedResult = CaptureMethod.Invoke(null, new object[] { states });
        }
        catch (TargetInvocationException exception)
        {
            error = exception.InnerException ?? exception;
        }
        JsonNode? result = null;
        if (typedResult is List<ShipyardSaveDto> detached)
        {
            var beforeMutation = JsonSerializer.SerializeToNode(detached, Json)!;
            var afterMutation = JsonSerializer.SerializeToNode(detached, Json)!;
            result = JsonSerializer.SerializeToNode(new
            {
                Dtos = beforeMutation,
                DetachedAfterLiveMutation = afterMutation,
                Stable = JsonNode.DeepEquals(beforeMutation, afterMutation),
            }, Json);
        }
        rows.Add(new("capture-empty", "Capture", 16, input,
            input.DeepClone(), StateListJson(states), result,
            error?.GetType().Name, error?.Message));
    }

    private static void AddRestore(List<Row> rows, string name, int version,
        Action<ShipyardSaveDto> mutate)
    {
        var dto = BaselineDto();
        mutate(dto);
        var civilizations = Civilizations();
        var input = JsonSerializer.SerializeToNode(new { Dtos = new[] { dto }, Civilizations = CivilizationJson(civilizations) }, Json)!;
        var before = input.DeepClone();
        object? typedResult = null;
        Exception? error = null;
        try
        {
            typedResult = RestoreMethod.Invoke(null, new object[] { new List<ShipyardSaveDto> { dto }, civilizations, version });
        }
        catch (TargetInvocationException exception)
        {
            error = exception.InnerException ?? exception;
        }
        var result = typedResult is null ? null : StateListJson((IList<ShipyardState>)typedResult);
        var after = JsonSerializer.SerializeToNode(new { Dtos = new[] { dto }, Civilizations = CivilizationJson(civilizations) }, Json)!;
        rows.Add(new(name, "Restore", version, input, before, after, result,
            error?.GetType().Name, error?.Message));
    }

    private static void AddCapture(List<Row> rows, string name,
        Action<ShipyardState> mutate)
    {
        var state = BaselineState();
        mutate(state);
        var input = StateListJson(new[] { state });
        var before = input.DeepClone();
        object? typedResult = null;
        Exception? error = null;
        try
        {
            typedResult = CaptureMethod.Invoke(null, new object[] { new[] { state } });
        }
        catch (TargetInvocationException exception)
        {
            error = exception.InnerException ?? exception;
        }
        JsonNode? result = null;
        var after = StateListJson(new[] { state });
        if (typedResult is List<ShipyardSaveDto> detached)
        {
            var detachedBeforeMutation = JsonSerializer.SerializeToNode(detached, Json)!;
            state.ActiveDesignId = "mutated-live";
            RawQueue(state).Clear();
            var detachedAfterMutation = JsonSerializer.SerializeToNode(detached, Json)!;
            result = JsonSerializer.SerializeToNode(new
            {
                Dtos = detachedBeforeMutation,
                DetachedAfterLiveMutation = detachedAfterMutation,
                Stable = JsonNode.DeepEquals(detachedBeforeMutation, detachedAfterMutation),
            }, Json);
        }
        rows.Add(new(name, "Capture", 16, input, before, after, result,
            error?.GetType().Name, error?.Message));
    }

    private static ShipyardSaveDto BaselineDto() => new()
    {
        CivilizationId = 3,
        NextOrderSequence = 3,
        ActiveDesignId = "warp_scout",
        ActiveOrderId = "shipyard-3-1",
        ActiveBuildProgress = 100,
        ActiveAuthorizationCredits = 20,
        QueuedBuilds = new()
        {
            new() { OrderId = "shipyard-3-2", DesignId = "science_vessel", AuthorizationCredits = 10 },
        },
    };

    private static void ClearActive(ShipyardSaveDto dto)
    {
        dto.ActiveDesignId = null;
        dto.ActiveOrderId = null;
        dto.ActiveBuildProgress = 0;
        dto.ActiveAuthorizationCredits = 0;
        dto.ReservedPopulationSpeciesId = null;
        dto.ReservedPopulationSourceColonyId = null;
    }

    private static ShipyardState BaselineState()
    {
        var state = new ShipyardState
        {
            CivilizationId = 3,
            NextOrderSequence = 3,
            ActiveDesignId = "warp_scout",
            ActiveOrderId = "shipyard-3-1",
            ActiveBuildProgress = 100,
            ActiveAuthorizationCredits = 20,
        };
        RawQueue(state).Add(new ShipBuildOrderState
        {
            OrderId = "shipyard-3-2", DesignId = "science_vessel",
            AuthorizationCredits = 10,
        });
        return state;
    }

    private static List<CivilizationState> Civilizations() => new()
    {
        new CivilizationState(3, "Owner", 0, CivilizationArchetype.Adaptive,
            CivilizationTraits.Balanced, false, CivilizationDevelopmentStage.WarpCapable,
            SpeciesId: SpeciesCatalog.PelagicHighPressureId),
    };

    private static List<ShipBuildOrderState> RawQueue(ShipyardState state) =>
        (List<ShipBuildOrderState>)QueueField.GetValue(state)!;

    private static JsonNode StateListJson(IEnumerable<ShipyardState> states) =>
        JsonSerializer.SerializeToNode(states.Select(state => new
        {
            state.CivilizationId,
            state.NextOrderSequence,
            state.ActiveDesignId,
            state.ActiveOrderId,
            state.ActiveBuildProgress,
            state.ActiveAuthorizationCredits,
            state.ReservedPopulationMillions,
            state.ReservedPopulationSpeciesId,
            state.ReservedPopulationSourceColonyId,
            QueuedBuilds = RawQueue(state).Select(order => new
            {
                order.OrderId, order.DesignId, order.AuthorizationCredits,
                order.ReservedPopulationMillions,
                order.ReservedPopulationSpeciesId,
                order.ReservedPopulationSourceColonyId,
            }).ToArray(),
        }).ToArray(), Json)!;

    private static object[] CivilizationJson(IEnumerable<CivilizationState> values) =>
        values.Select(value => (object)new { value.Id, value.SpeciesId }).ToArray();
}
