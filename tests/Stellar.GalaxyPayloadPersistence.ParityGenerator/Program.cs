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
            Console.Error.WriteLine("Usage: CampaignGalaxyPayloadOracle <fixture> <Game source root>");
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
                Authority = "actual CampaignSaveService current-format capture/load composition",
                SourceFiles = sources,
                RowCount = rows.Count,
                SourceOnlyRows = 0,
                Rows = rows,
            };
            File.WriteAllText(output, JsonSerializer.Serialize(document, Json) + Environment.NewLine,
                new UTF8Encoding(false));
            Console.WriteLine($"Galaxy payload source oracle: {rows.Count}/{rows.Count} rows written.");
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
        var root = ExclusiveScratch();
        try
        {
            var basePath = Path.Combine(root, "base.json");
            service.Save(basePath, state, 37.25);
            var baseline = JsonNode.Parse(File.ReadAllText(basePath))!.AsObject();
            baseline["SavedAtUtc"] = "2042-03-04T05:06:07+00:00";

            var rows = new List<Row>();
            AddLoad(rows, service, root, "valid-current16", baseline);
            AddCapture(rows, service, "capture-field-complete", state, 37.25);
            AddCapturePartialFailure(rows, service, baseline);
            AddLoad(rows, service, root, "reversed-authored-collections", Change(baseline,
                root =>
                {
                    var galaxy = root["Galaxy"]!.AsObject();
                    Reverse(galaxy["Civilizations"]!.AsArray());
                    Reverse(galaxy["Knowledge"]!.AsArray());
                    Reverse(galaxy["Technologies"]!.AsArray());
                    Reverse(galaxy["ConstructionStates"]!.AsArray());
                    Reverse(galaxy["ShipyardStates"]!.AsArray());
                }));
            AddLoad(rows, service, root, "surface-null-colonies", Change(baseline,
                root => root["Galaxy"]!["Colonies"] = null));
            AddLoad(rows, service, root, "surface-null-economies", Change(baseline,
                root => root["Galaxy"]!["Economies"] = null));
            AddLoad(rows, service, root, "surface-null-both-colonies-first", Change(baseline, root =>
            {
                root["Galaxy"]!["Colonies"] = null;
                root["Galaxy"]!["Economies"] = null;
            }));
            AddLoad(rows, service, root, "surface-empty-colonies", Change(baseline,
                root => root["Galaxy"]!["Colonies"] = new JsonArray()));
            AddLoad(rows, service, root, "surface-empty-economies", Change(baseline,
                root => root["Galaxy"]!["Economies"] = new JsonArray()));
            AddLoad(rows, service, root, "null-systems-null-reference", Change(baseline,
                root => root["Galaxy"]!["Systems"] = null));
            AddLoad(rows, service, root, "null-fleets-null-reference", Change(baseline,
                root => root["Galaxy"]!["Fleets"] = null));
            AddLoad(rows, service, root, "metadata-core-fallback", Change(baseline, root =>
            {
                var galaxy = root["Galaxy"]!.AsObject();
                galaxy["GalacticCore"] = null;
                var system = galaxy["Systems"]!.AsArray()[0]!.AsObject();
                var core = new JsonObject
                {
                    ["LandmarkKey"] = "galactic-core-smbh-v1",
                    ["X"] = 5000,
                    ["Y"] = 5000,
                    ["ExclusionRadius"] = 10,
                };
                galaxy["GenerationMetadata"] = new JsonObject
                {
                    ["EnteredSeed"] = "871603", ["InternalSeed"] = 871603,
                    ["GeneratorVersion"] = "galaxy-v4", ["CreatedAtUtc"] = "2042-01-01T00:00:00+00:00",
                    ["SystemCount"] = 20, ["GalaxyShape"] = "Legacy disk",
                    ["StellarVariety"] = "Balanced", ["PlanetBearingSystems"] = "Common",
                    ["HabitableWorlds"] = "Uncommon", ["GuaranteedNearbyHabitableWorlds"] = 0,
                    ["OtherCivilizations"] = 7, ["AncientCivilizations"] = "None",
                    ["SpaceHazards"] = "Standard", ["StartingDevelopment"] = "Early Space Age",
                    ["Difficulty"] = "Standard", ["ArtProfileVersion"] = "legacy-static-v1",
                    ["PlayerSpeciesId"] = "terran_baseline", ["AnomalyFrequency"] = "Standard",
                    ["GalacticCore"] = core,
                };
                // Keep the local assignment used so the compiler cannot elide this row's
                // complete system input while the core is deliberately far away.
                _ = system["Id"];
            }));
            AddLoad(rows, service, root, "player-fallback-unknown-home", Change(baseline, root =>
            {
                var galaxy = root["Galaxy"]!.AsObject();
                galaxy["Knowledge"] = new JsonArray();
                var playerId = galaxy["PlayerCivilizationId"]!.GetValue<int>();
                var player = galaxy["Civilizations"]!.AsArray().Select(x => x!.AsObject())
                    .Single(x => x["Id"]!.GetValue<int>() == playerId);
                player["HomeSystemId"] = 987654321;
            }));
            AddLoad(rows, service, root, "no-civilizations-regenerates-seeding-catalog",
                Change(baseline, root => root["Galaxy"]!["Civilizations"] = new JsonArray()));
            return rows;
        }
        finally
        {
            CheckedCleanup(root);
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
