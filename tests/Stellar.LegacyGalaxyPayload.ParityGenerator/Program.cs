using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Construction;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new() { WriteIndented = true };
    private static readonly MethodInfo CapturePayload = typeof(CampaignSaveService).GetMethod(
        "CapturePayload", BindingFlags.NonPublic | BindingFlags.Instance)!;

    private sealed record Row(string Name, string Operation, JsonNode Input, JsonNode Before,
        JsonNode After, JsonNode? Result, string? ErrorType, string? ErrorMessage,
        string? InnerType, string? InnerMessage);

    private static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        if (args.Length != 2)
        {
            Console.Error.WriteLine("Usage: LegacyGalaxyPayloadOracle <fixture> <Game source root>");
            return 1;
        }

        var output = Path.GetFullPath(args[0]);
        var root = Path.GetFullPath(args[1]);
        try
        {
            var rows = BuildRows();
            string[] paths =
            {
                "Persistence/CampaignSaveService.cs",
                "Simulation/Generation/CivilizationSeeder.cs",
                "Simulation/Generation/PlanetaryBodyGenerator.cs",
                "Simulation/Generation/SolCatalogPreset.cs",
                "Simulation/Generation/FleetSeeder.cs",
                "Simulation/Generation/ColonySeeder.cs",
                "Simulation/Generation/ShipyardSeeder.cs",
                "Simulation/Knowledge/CivilizationKnowledgeState.cs",
                "Simulation/Models/GalaxyState.cs",
            };
            var sources = paths.Select(path => new
            {
                Path = path,
                Sha256 = Convert.ToHexString(SHA256.HashData(
                    File.ReadAllBytes(Path.Combine(root, path))))
            }).ToArray();
            var document = new
            {
                SchemaVersion = 1,
                Authority = "actual CampaignSaveService historical galaxy restore composition",
                SourceFiles = sources,
                RowCount = rows.Count,
                SourceOnlyRows = 0,
                Rows = rows,
            };
            File.WriteAllText(output, JsonSerializer.Serialize(document, Json) + Environment.NewLine,
                new UTF8Encoding(false));
            Console.WriteLine($"Legacy galaxy payload source oracle: {rows.Count}/{rows.Count} rows written.");
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

    private static List<Row> BuildRows()
    {
        var service = new CampaignSaveService();
        var state = new GalaxyGenerator().Generate(871603, new GalaxyGenerationSettings
        {
            SystemCount = 20,
            PreWarpCivilizationCount = 8,
            AncientCivilizationCount = 0,
        });
        MakeFieldComplete(state);
        var scratch = ExclusiveScratch();
        try
        {
            var basePath = Path.Combine(scratch, "base.json");
            service.Save(basePath, state, 37.25);
            var baseline = JsonNode.Parse(File.ReadAllText(basePath))!.AsObject();
            baseline["SavedAtUtc"] = "2043-04-05T06:07:08+00:00";
            baseline["SimulationSeconds"] = 123.75;

            var rows = new List<Row>();
            JsonObject Legacy(int version, Action<JsonObject>? change = null) =>
                Change(baseline, root =>
                {
                    root["FormatVersion"] = version;
                    if (version < 3)
                    {
                        root["Galaxy"]!["ActiveCombatEncounter"] = null;
                        root["Galaxy"]!["CombatIntelligence"] = null;
                    }
                    if (version < 12)
                        foreach (var colony in root["Galaxy"]!["Colonies"]!.AsArray())
                            colony!["SurfaceBuildings"] = new JsonArray();
                    change?.Invoke(root);
                });
            foreach (var version in new[] { 1, 2, 3, 4, 5, 6, 7, 8, 10, 12 })
                AddLoad(rows, service, scratch, $"legacy-format-{version}",
                    Legacy(version));
            AddLoad(rows, service, scratch, "format-1-ignores-null-civilizations",
                Legacy(1, root => root["Galaxy"]!["Civilizations"] = null));
            AddLoad(rows, service, scratch, "format-2-requires-civilizations",
                Legacy(2, root => root["Galaxy"]!["Civilizations"] = null));
            AddLoad(rows, service, scratch, "format-2-ignores-null-fleets",
                Legacy(2, root => root["Galaxy"]!["Fleets"] = null));
            AddLoad(rows, service, scratch, "format-3-requires-fleets",
                Legacy(3, root => root["Galaxy"]!["Fleets"] = null));
            AddLoad(rows, service, scratch, "format-3-preserves-empty-fleets",
                Legacy(3, root => root["Galaxy"]!["Fleets"] = new JsonArray()));
            AddLoad(rows, service, scratch, "format-3-ignores-null-surface",
                Legacy(3, root =>
                {
                    root["Galaxy"]!["Colonies"] = null;
                    root["Galaxy"]!["Economies"] = null;
                }));
            AddLoad(rows, service, scratch, "format-4-requires-colonies",
                Legacy(4, root => root["Galaxy"]!["Colonies"] = null));
            AddLoad(rows, service, scratch, "format-4-empty-colonies-ignore-null-economies",
                Legacy(4, root =>
                {
                    root["Galaxy"]!["Colonies"] = new JsonArray();
                    root["Galaxy"]!["Economies"] = null;
                }));
            AddLoad(rows, service, scratch, "format-4-requires-economies-after-colonies",
                Legacy(4, root => root["Galaxy"]!["Economies"] = null));
            AddLoad(rows, service, scratch, "format-4-empty-economies-reseed",
                Legacy(4, root => root["Galaxy"]!["Economies"] = new JsonArray()));
            AddLoad(rows, service, scratch, "format-4-ignores-null-technologies",
                Legacy(4, root => root["Galaxy"]!["Technologies"] = null));
            AddLoad(rows, service, scratch, "format-5-requires-technologies",
                Legacy(5, root => root["Galaxy"]!["Technologies"] = null));
            AddLoad(rows, service, scratch, "format-5-empty-technologies-migrate",
                Legacy(5, root => root["Galaxy"]!["Technologies"] = new JsonArray()));
            AddLoad(rows, service, scratch, "format-5-ignores-null-construction",
                Legacy(5, root => root["Galaxy"]!["ConstructionStates"] = null));
            AddLoad(rows, service, scratch, "format-6-requires-construction",
                Legacy(6, root => root["Galaxy"]!["ConstructionStates"] = null));
            AddLoad(rows, service, scratch, "format-6-empty-construction-migrates",
                Legacy(6, root => root["Galaxy"]!["ConstructionStates"] = new JsonArray()));
            AddLoad(rows, service, scratch, "format-6-ignores-null-shipyards",
                Legacy(6, root => root["Galaxy"]!["ShipyardStates"] = null));
            AddLoad(rows, service, scratch, "format-7-requires-shipyards",
                Legacy(7, root => root["Galaxy"]!["ShipyardStates"] = null));
            AddLoad(rows, service, scratch, "format-7-empty-shipyards-reseed",
                Legacy(7, root => root["Galaxy"]!["ShipyardStates"] = new JsonArray()));
            AddLoad(rows, service, scratch, "format-10-empty-surface-reseeds",
                Legacy(10, root =>
                {
                    root["Galaxy"]!["Colonies"] = new JsonArray();
                    root["Galaxy"]!["Economies"] = new JsonArray();
                }));
            AddLoad(rows, service, scratch, "format-12-empty-surface-rejected",
                Legacy(12, root =>
                {
                    root["Galaxy"]!["Colonies"] = new JsonArray();
                    root["Galaxy"]!["Economies"] = new JsonArray();
                }));
            AddLoad(rows, service, scratch, "format-12-null-colonies-rejected-first",
                Legacy(12, root =>
                {
                    root["Galaxy"]!["Colonies"] = null;
                    root["Galaxy"]!["Economies"] = null;
                }));
            AddLoad(rows, service, scratch, "format-12-null-economies-rejected",
                Legacy(12, root => root["Galaxy"]!["Economies"] = null));
            AddLoad(rows, service, scratch, "legacy-empty-civilizations-format-8",
                Change(baseline, root =>
                {
                    root["FormatVersion"] = 8;
                    root["Galaxy"]!["Civilizations"] = new JsonArray();
                    foreach (var colony in root["Galaxy"]!["Colonies"]!.AsArray())
                        colony!["SurfaceBuildings"] = new JsonArray();
                }));
            AddLoad(rows, service, scratch, "unsupported-galaxy-format-9",
                Change(baseline, root => root["FormatVersion"] = 9));
            AddLoad(rows, service, scratch, "unsupported-galaxy-format-11",
                Change(baseline, root => root["FormatVersion"] = 11));
            AddLoad(rows, service, scratch, "legacy-null-systems-format-8",
                Change(baseline, root =>
                {
                    root["FormatVersion"] = 8;
                    root["Galaxy"]!["Systems"] = null;
                }));
            return rows;
        }
        finally
        {
            CheckedCleanup(scratch);
        }
    }
    private static void MakeFieldComplete(GalaxyState state)
    {
        state.Civilizations[0] = state.Civilizations[0] with
        {
            DevelopmentStage = CivilizationDevelopmentStage.WarpCapable,
        };
        state.Civilizations[1] = state.Civilizations[1] with
        {
            DevelopmentStage = CivilizationDevelopmentStage.WarpCapable,
        };
        state.Fleets.Clear();
        foreach (var fleet in new FleetSeeder().Seed(state.Systems, state.Civilizations,
                     state.Colonies))
            state.Fleets.Add(fleet);
        if (state.Fleets.Count < 2)
            throw new InvalidOperationException("Field-complete fixture needs two fleets.");

        var travelled = state.Fleets[0];
        var origin = travelled.CurrentSystemId!.Value;
        var destination = state.Systems.First(system => system.Id != origin);
        travelled.CurrentSystemId = null;
        travelled.DestinationSystemId = destination.Id;
        travelled.TransitPhase = FleetTransitPhase.InterstellarWarp;
        travelled.TransitOriginSystemId = origin;
        travelled.TransitTargetSystemId = destination.Id;
        travelled.TransitProgress = .375;
        travelled.PlannedRouteSystemIds = new() { destination.Id };
        travelled.LocalTransitStart = travelled.Position;
        travelled.LocalTransitPosition = travelled.Position;
        travelled.LocalTransitTarget = destination.Position;
        travelled.Position = new((travelled.Position.X + destination.Position.X) / 2,
            (travelled.Position.Y + destination.Position.Y) / 2);
        travelled.HoldRequested = true;
        travelled.MissionOrderRevision = 7;
        travelled.CargoMaterials = Math.Min(3.5, travelled.CargoMaterialCapacity);

        var yard = state.ShipyardStates[0];
        var design = ShipDesignRegistry.All.First();
        yard.NextOrderSequence = 3;
        yard.QueuedBuilds.Add(new ShipBuildOrderState
        {
            OrderId = ShipyardState.FormatOrderId(yard.CivilizationId, 2),
            DesignId = design.Id,
            AuthorizationCredits = 12.5,
        });

        state.Colonies[0].SurfaceBuildings.Add(new SurfaceBuildingState
        {
            Id = 77,
            TypeId = "science_lab",
            X = 120.5f,
            Z = -108.25f,
            RotationDegrees = 135f,
            IndustryProgress = 400,
            IsComplete = true,
            IsEnabled = true,
            OperatingPriority = 1,
            Condition = .875,
            StoredPowerDays = 0,
        });

        state.CombatIntelligence.Add(new FleetPowerObservation(
            state.Civilizations[1].Id, travelled.Id, 123.75, 36.5,
            "Combat scanner"));

        var encounterFleet = state.Fleets[1];
        var profile = CombatProfileRegistry.Get(
            CombatProfileRegistry.DefaultProfileId(encounterFleet.Role));
        var loadout = MassiveCombatLoadouts.FromLegacy(profile);
        var vesselId = Math.Max(1, encounterFleet.Id);
        if (vesselId != encounterFleet.Id)
            throw new InvalidOperationException("Encounter fixture fleet identity must be positive.");
        var vessel = new MassiveVesselState
        {
            Id = vesselId,
            Name = encounterFleet.Name,
            DesignId = encounterFleet.DesignId ?? profile.Id,
            IsFlagship = true,
            BattlesFought = 2,
            ConfirmedKills = 1,
        };
        var formation = new MassiveFormationState
        {
            Id = 101,
            CivilizationId = encounterFleet.CivilizationId,
            FleetId = encounterFleet.Id,
            TaskForceId = 9,
            Name = "Retained formation",
            Position = new(10, 20),
            Objective = new(30, 40),
            Loadout = loadout,
            ImportantVessels = new() { vessel },
        };
        var battle = MassiveCombatBattleState.Create(987, new[] { formation });
        state.ActiveCombatEncounter = new CampaignMassiveEncounter
        {
            SystemId = encounterFleet.CurrentSystemId!.Value,
            StartedDay = 35.25,
            Battle = battle,
            Vessels = new() { new(encounterFleet.Id, formation.Id) },
            LastObservedEventSequence = 0,
        };
    }

    private static void Reverse(JsonArray array)
    {
        var values = array.Select(item => item?.DeepClone()).Reverse().ToArray();
        array.Clear();
        foreach (var value in values) array.Add(value);
    }

    private static void AddCapture(List<Row> rows, CampaignSaveService service,
        string name, GalaxyState state, double simulationDays)
    {
        JsonObject Snapshot()
        {
            var payload = (JsonObject)CapturePayload.Invoke(service,
                new object[] { state, simulationDays, false })!;
            payload["SavedAtUtc"] = "2042-03-04T05:06:07+00:00";
            return payload;
        }
        var input = Snapshot();
        var before = input.DeepClone();
        JsonNode? result = null;
        Exception? error = null;
        try { result = Snapshot(); }
        catch (Exception exception) { error = Unwrap(exception); }
        var after = Snapshot();
        rows.Add(new(name, "Capture", input, before, after, result,
            error?.GetType().Name, error?.Message,
            error?.InnerException?.GetType().Name, error?.InnerException?.Message));
    }

    private static void AddCapturePartialFailure(List<Row> rows,
        CampaignSaveService service, JsonObject baseline)
    {
        var root = ExclusiveScratch();
        try
        {
            var path = Path.Combine(root, "capture-partial-failure.json");
            File.WriteAllText(path, baseline.ToJsonString(Json), new UTF8Encoding(false));
            var loaded = service.Load(path);
            loaded.Galaxy.Fleets[0].Combat = null;
            loaded.Galaxy.Economies[0].IndustryPriority = (IndustryPriority)999;

            JsonObject Projection(bool normalizedCombat)
            {
                var result = baseline.DeepClone().AsObject();
                var galaxy = result["Galaxy"]!.AsObject();
                galaxy["Economies"]!.AsArray()[0]!["IndustryPriority"] = 999;
                if (!normalizedCombat)
                    galaxy["Fleets"]!.AsArray()[0]!["Combat"] = null;
                return result;
            }

            var before = Projection(false);
            JsonNode? result = null;
            Exception? error = null;
            try
            {
                result = CapturePayload.Invoke(service,
                    new object[] { loaded.Galaxy, loaded.SimulationDays, false }) as JsonNode;
            }
            catch (Exception exception)
            {
                error = Unwrap(exception);
            }
            var after = Projection(true);
            if (loaded.Galaxy.Fleets[0].Combat is null)
                throw new InvalidOperationException("Capture did not retain its earlier combat normalization.");
            rows.Add(new("capture-partial-fleet-normalization-before-economy-error",
                "Capture", baseline.DeepClone(), before, after, result,
                error?.GetType().Name, error?.Message,
                error?.InnerException?.GetType().Name,
                error?.InnerException?.Message));
        }
        finally
        {
            CheckedCleanup(root);
        }
    }

    private static void AddLoad(List<Row> rows, CampaignSaveService service, string scratch,
        string name, JsonObject input)
    {
        var path = Path.Combine(scratch, name + ".json");
        File.WriteAllText(path, input.ToJsonString(Json), new UTF8Encoding(false));
        var before = input.DeepClone();
        object? typed = null;
        Exception? error = null;
        try { typed = service.Load(path); }
        catch (Exception exception) { error = Unwrap(exception); }

        JsonNode? result = null;
        if (typed is LoadedCampaign loaded)
        {
            var capture = (JsonObject)CapturePayload.Invoke(service,
                new object[] { loaded.Galaxy, loaded.SimulationDays, false })!;
            result = new JsonObject
            {
                ["SimulationDays"] = loaded.SimulationDays,
                ["GameVersion"] = loaded.GameVersion,
                ["SavedAtUtc"] = loaded.SavedAtUtc.ToString("O", CultureInfo.InvariantCulture),
                ["Galaxy"] = capture["Galaxy"]!.DeepClone(),
                ["RawTechnologyCompletedIds"] = JsonSerializer.SerializeToNode(
                    loaded.Galaxy.Technologies.Select(technology =>
                        technology.CompletedTechnologyIds.ToArray()), Json),
                ["RawConstructionCompletedIds"] = JsonSerializer.SerializeToNode(
                    loaded.Galaxy.ConstructionStates.Select(construction =>
                        construction.CompletedProjectIds.ToArray()), Json),
            };
        }
        var after = JsonNode.Parse(File.ReadAllText(path))!;
        rows.Add(new(name, "Restore", input, before, after, result,
            error?.GetType().Name, error?.Message, error?.InnerException?.GetType().Name,
            error?.InnerException?.Message));
    }

    private static JsonObject Change(JsonObject baseline, Action<JsonObject> change)
    {
        var copy = baseline.DeepClone().AsObject();
        change(copy);
        return copy;
    }

    private static Exception Unwrap(Exception error) =>
        error is TargetInvocationException { InnerException: not null } tie
            ? tie.InnerException!
            : error;

    private static string ExclusiveScratch()
    {
        var parent = Path.GetFullPath(Path.GetTempPath());
        for (var attempt = 0; attempt < 32; attempt++)
        {
            var path = Path.Combine(parent, "stellar-galaxy087-" + Guid.NewGuid().ToString("N"));
            try { Directory.CreateDirectory(path); return path; }
            catch (IOException) { }
        }
        throw new IOException("Could not claim an exclusive Gate087 scratch directory.");
    }

    private static void CheckedCleanup(string path)
    {
        var full = Path.GetFullPath(path);
        var parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar) +
            Path.DirectorySeparatorChar;
        if (!full.StartsWith(parent, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Refusing to clean an unowned Gate087 path.");
        try { Directory.Delete(full, recursive: true); }
        catch (Exception error) { Console.Error.WriteLine($"Gate087 scratch cleanup failed: {error}"); }
    }
}
