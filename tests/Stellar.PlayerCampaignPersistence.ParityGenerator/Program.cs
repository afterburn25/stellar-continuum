using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;

internal static class Program
{
    private static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = false,
    };
    private static readonly PropertyInfo PreparedPayload = typeof(PreparedCampaignSave)
        .GetProperty("Payload", BindingFlags.Instance | BindingFlags.NonPublic)!;

    private sealed record Row(string Name, string Operation, JsonNode Input,
        JsonNode Before, JsonNode After, JsonNode? Result, string? ErrorType,
        string? ErrorMessage, string? InnerType, string? InnerMessage);

    private static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        if (args.Length != 3)
        {
            Console.Error.WriteLine("Usage: PlayerCampaignOracle <fixture> <Game source root> <research root>");
            return 1;
        }
        var fixture = Path.GetFullPath(args[0]);
        var sourceRoot = Path.GetFullPath(args[1]);
        var researchRoot = Path.GetFullPath(args[2]);
        try
        {
            var rows = BuildRows(researchRoot);
            string[] files =
            {
                "Persistence/CampaignStatePersistenceService.cs",
                "Persistence/DiplomacyCampaignReferenceValidator.cs",
                "Persistence/CampaignSaveService.cs",
                "Simulation/Diplomacy/DiplomacySnapshotInvariantValidator.cs",
                "Simulation/Diplomacy/DiplomacySystem.cs",
                "Simulation/Research/Adaptive/AdaptiveResearchCampaignState.cs",
                "Presentation/Main.CoreIntegration.cs",
                "Simulation/GalaxySimulationStepCoordinator.cs",
                "Simulation/Research/Adaptive/AdaptiveResearchCampaignSimulation.cs",
                "Simulation/Diplomacy/DiplomacyCampaignRuntimeCoordinator.cs",
                "Simulation/Combat/FleetCombatPower.cs",
            };
            var sources = files.Select(path => new
            {
                Path = path,
                Sha256 = Convert.ToHexString(SHA256.HashData(
                    File.ReadAllBytes(Path.Combine(sourceRoot, path))))
            }).ToArray();
            var document = new
            {
                SchemaVersion = 1,
                Authority = "actual CampaignStatePersistenceService current Player17 typed composition",
                SourceFiles = sources,
                RowCount = rows.Count,
                SourceOnlyRows = 0,
                Rows = rows,
            };
            File.WriteAllText(fixture, JsonSerializer.Serialize(document, Json) + Environment.NewLine,
                new UTF8Encoding(false));
            Console.WriteLine($"Player17 source oracle: {rows.Count}/{rows.Count} rows written.");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            Console.Error.WriteLine($"Working directory: {Environment.CurrentDirectory}");
            Console.Error.WriteLine($"Fixture path: {fixture}");
            Console.Error.WriteLine($"Source root: {sourceRoot}");
            Console.Error.WriteLine($"Research root: {researchRoot}");
            return 1;
        }
    }

    private static List<Row> BuildRows(string researchRoot)
    {
        var runtime = AdaptiveResearchStrategicRuntime.LoadFromDirectory(researchRoot);
        var service = new CampaignStatePersistenceService(
            adaptiveResearchRuntime: runtime);
        var galaxy = new GalaxyGenerator().Generate(912017,
            new GalaxyGenerationSettings
            {
                SystemCount = 20,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 0,
            });
        var diplomacy = new DiplomacyState();
        if (galaxy.Fleets.Count == 0)
        {
            for (var index = 0; index < galaxy.Civilizations.Count; index++)
                galaxy.Civilizations[index] = galaxy.Civilizations[index] with
                {
                    DevelopmentStage = CivilizationDevelopmentStage.WarpCapable,
                };
            foreach (var fleet in new FleetSeeder().Seed(
                         galaxy.Systems, galaxy.Civilizations, galaxy.Colonies))
                galaxy.Fleets.Add(fleet);
        }
        if (galaxy.Fleets.Count == 0)
            throw new InvalidOperationException("Player17 fixture requires a fleet.");
        var research = service.CreateAdaptiveResearchState(galaxy);
        var playerId = galaxy.PlayerCivilizationId;
        var counterpartId = galaxy.Civilizations
            .Select(value => value.Id).First(value => value != playerId);
        var observedSystemId = galaxy.Systems[0].Id;
        var diplomacySimulation = new DiplomacySimulation(diplomacy);
        diplomacySimulation.ProcessContactOpportunity(new FirstContactOpportunity(
            playerId, "populated-player-counterpart", counterpartId, 10,
            observedSystemId, ContactAwareness.ContactEstablished,
            ContactCondition.Active, true, .9));
        diplomacySimulation.ProcessContactOpportunity(new FirstContactOpportunity(
            counterpartId, "populated-counterpart-player", playerId, 10,
            observedSystemId, ContactAwareness.ContactEstablished,
            ContactCondition.Active, true, .9));
        diplomacySimulation.ApplyRelationshipImpact(
            playerId, counterpartId,
            new RelationshipImpact(.2, 0, .1, .3, .4, .25,
                "retained joint survey"), 11);
        var proposalId = diplomacySimulation.SendProposal(
            playerId, counterpartId, DiplomaticProposalKind.Agreement, 12,
            "Retained non-aggression proposal",
            DiplomaticAgreementType.NonAggression);
        diplomacySimulation.RespondToProposal(
            proposalId, counterpartId, accept: true, tick: 13);

        var playerResearch = research.GetCivilization(playerId);
        var candidate = runtime.Authority.BuildView(playerResearch).VisibleNodes
            .First(value => value.State == ResearchMaturity.Investigable &&
                            value.Blockers.Count == 0 &&
                            value.MinimumLabs is not null);
        var playerEconomy = galaxy.Economies.First(
            value => value.CivilizationId == playerId);
        playerEconomy.Credits = 1_000_000;
        var started = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            galaxy, research, playerId, candidate.NodeId,
            candidate.MinimumLabs!.Value,
            research.Starts[playerId].ApplicabilityContextId);
        if (!started.Accepted)
            throw new InvalidOperationException(started.Message);
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            galaxy, research, .25, 42.25);
        var baseline = Payload(service.PrepareSave(galaxy, 42.25, diplomacy, research));
        baseline["SavedAtUtc"] = "2044-05-06T07:08:09+00:00";
        var rows = new List<Row>();

        AddCapture(rows, "capture-valid",
            CaptureInput(baseline, 42.25, developer: false), () =>
            Payload(service.PrepareSave(galaxy, 42.25, diplomacy, research)));
        AddCapture(rows, "capture-invalid-time",
            CaptureInput(baseline, "NaN", developer: false), () =>
            Payload(service.PrepareSave(galaxy, double.NaN, diplomacy, research)));
        galaxy.DeveloperSession = new DeveloperSessionState(true);
        AddCapture(rows, "capture-developer-before-time",
            CaptureInput(baseline, "NaN", developer: true), () =>
            Payload(service.PrepareSave(galaxy, double.NaN, diplomacy, research)));
        galaxy.DeveloperSession = null;

        var originalPriority = galaxy.Economies[0].IndustryPriority;
        galaxy.Economies[0].IndustryPriority = (IndustryPriority)999;
        var badGalaxy = Change(baseline, root =>
            root["Galaxy"]!["Economies"]![0]!["IndustryPriority"] = 999);
        var firstCivilization = galaxy.Civilizations[0].Id;
        var secondCivilization = galaxy.Civilizations[1].Id;
        var invalidDiplomacySnapshot = diplomacy.Snapshot() with
        {
            Relationships = new[]
            {
                new DiplomaticRelationshipSnapshot(
                    firstCivilization, secondCivilization,
                    DiplomaticPoliticalState.Peace, 0, 0, 0, 0, 0,
                    Array.Empty<DiplomaticGrievanceSnapshot>()),
            },
        };
        var invalidDiplomacy = DiplomacyState.Restore(invalidDiplomacySnapshot);
        var relationshipsField = typeof(DiplomacyState).GetField(
            "_relationships", BindingFlags.Instance | BindingFlags.NonPublic)!;
        var relationships = relationshipsField.GetValue(invalidDiplomacy)!;
        var values = relationships.GetType().GetProperty("Values")!
            .GetValue(relationships)! as System.Collections.IEnumerable;
        var relationship = values!.Cast<object>().Single();
        relationship.GetType().GetProperty("Trust")!.SetValue(relationship, 2.0);
        var badDiplomacyInput = CaptureInput(Change(badGalaxy, root =>
            root["Diplomacy"] = JsonSerializer.SerializeToNode(
                invalidDiplomacy.Snapshot(), Json)), 42.25, false);
        badDiplomacyInput["Control"]!["InvalidDiplomacyTrust"] = true;
        badDiplomacyInput["Control"]!["InvalidEconomyPriority"] = true;
        AddCapture(rows, "capture-diplomacy-before-bad-galaxy",
            badDiplomacyInput,
            () => Payload(service.PrepareSave(
                galaxy, 42.25, invalidDiplomacy, research)));

        var originalCombat = galaxy.Fleets[0].Combat;
        galaxy.Fleets[0].Combat = null;
        var partialInput = CaptureInput(Change(badGalaxy, root =>
            root["Galaxy"]!["Fleets"]![0]!["Combat"] = null), 42.25, false);
        partialInput["Control"]!["InvalidEconomyPriority"] = true;
        partialInput["Control"]!["NullFirstFleetCombat"] = true;
        var partialAfter = partialInput.DeepClone().AsObject();
        partialAfter["Galaxy"]!["Fleets"]![0]!["Combat"] =
            baseline["Galaxy"]!["Fleets"]![0]!["Combat"]!.DeepClone();
        AddCapture(rows, "capture-partial-fleet-before-economy-error",
            partialInput, partialAfter, () => Payload(service.PrepareSave(
                galaxy, 42.25, diplomacy, research)));
        if (galaxy.Fleets[0].Combat is null)
            throw new InvalidOperationException("Player17 capture did not normalize fleet combat.");
        galaxy.Fleets[0].Combat = originalCombat;
        galaxy.Economies[0].IndustryPriority = originalPriority;

        var scratch = ExclusiveScratch();
        try
        {
            AddLoad(rows, service, scratch, "restore-valid", baseline);
            AddActivationSuccess(rows, service, scratch, "restore-large-time-returned",
                Change(baseline, root => root["SimulationDays"] = 1e300));
            AddActivationFailure(rows, service, scratch,
                "restore-negative-time-activation-fails",
                Change(baseline, root => root["SimulationDays"] = -1.0));
            AddLoad(rows, service, scratch, "restore-missing-diplomacy",
                Change(baseline, root => root.Remove("Diplomacy")));
            AddLoad(rows, service, scratch, "restore-invalid-diplomacy-structure",
                Change(baseline, root => root["Diplomacy"]!["NextClaimId"] = 0));
            AddLoad(rows, service, scratch, "restore-missing-galaxy-format",
                Change(baseline, root => root.Remove("GalaxyFormatVersion")));
            AddLoad(rows, service, scratch, "restore-wrong-galaxy-format",
                Change(baseline, root => root["GalaxyFormatVersion"] = 15));
            AddLoad(rows, service, scratch, "restore-missing-research",
                Change(baseline, root => root.Remove("AdaptiveResearch")));
            AddLoad(rows, service, scratch, "restore-research-catalog-mismatch",
                Change(baseline, root => root["AdaptiveResearch"]!["CatalogId"] = "wrong"));
            AddLoad(rows, service, scratch, "restore-contact-unknown-observer",
                Change(baseline, root =>
                {
                    var diplomacyNode = root["Diplomacy"]!.AsObject();
                    diplomacyNode["Contacts"] = new JsonArray(new JsonObject
                    {
                        ["ObserverCivilizationId"] = 999999,
                        ["ContactId"] = "unknown-contact",
                        ["TargetCivilizationId"] = null,
                        ["FirstObservedTick"] = 1,
                        ["LastObservedTick"] = 1,
                        ["LastObservedSystemId"] = null,
                        ["Awareness"] = 1,
                        ["Condition"] = 0,
                        ["CommunicationAvailable"] = false,
                        ["Confidence"] = .5,
                    });
                }));
            AddLoad(rows, service, scratch,
                "restore-contact-before-missing-research",
                Change(baseline, root =>
                {
                    root.Remove("AdaptiveResearch");
                    root["Diplomacy"]!["Contacts"] = new JsonArray(new JsonObject
                    {
                        ["ObserverCivilizationId"] = 999999,
                        ["ContactId"] = "unknown-contact-before-research",
                        ["TargetCivilizationId"] = null,
                        ["FirstObservedTick"] = 1,
                        ["LastObservedTick"] = 1,
                        ["LastObservedSystemId"] = null,
                        ["Awareness"] = 1,
                        ["Condition"] = 0,
                        ["CommunicationAvailable"] = false,
                        ["Confidence"] = .5,
                    });
                }));
            AddAdvance(rows, service, runtime, scratch,
                "restore-activate-advance-nonbattle", baseline, 0.25);
        }
        finally { CheckedCleanup(scratch); }
        return rows;
    }

    private static void AddAdvance(List<Row> rows,
        CampaignStatePersistenceService service,
        AdaptiveResearchStrategicRuntime runtime,
        string scratch, string name, JsonObject input, double elapsedDays)
    {
        var path = Path.Combine(scratch, name + ".json");
        File.WriteAllText(path, input.ToJsonString(Json), new UTF8Encoding(false));
        var before = input.DeepClone();
        JsonNode? result = null;
        Exception? error = null;
        try
        {
            var loaded = service.Load(path);
            var constructionCapabilities =
                new AdaptiveResearchConstructionCapabilityView(loaded.AdaptiveResearch);
            var shipbuildingCapabilities =
                new AdaptiveResearchShipbuildingCapabilityView(loaded.AdaptiveResearch);
            var diplomacyRuntime =
                new DiplomacyCampaignRuntimeCoordinator(loaded.Diplomacy);
            diplomacyRuntime.Reset(loaded.SimulationDays, reviewImmediately: true);
            var strategic = new CivilizationStrategicRuntimeCoordinator(
                director: new CivilizationStrategicDirector(
                    new CivilizationStrategicInputBuilder(
                        shipbuildingCapabilities: shipbuildingCapabilities)),
                knowledgeProvider: new DiplomacyStrategicKnowledgeProvider(loaded.Diplomacy));
            var core = new GalaxySimulationStepCoordinator(
                construction: new ConstructionSimulation(constructionCapabilities),
                shipbuilding: new ShipbuildingSimulation(shipbuildingCapabilities),
                strategicAi: strategic,
                combatRuntime: diplomacyRuntime.CreateCombatCommandRuntime(),
                advanceLegacyResearch: false);
            var endDay = loaded.SimulationDays + elapsedDays;
            var coreResult = core.Advance(loaded.Galaxy, elapsedDays);
            foreach (var civilization in loaded.Galaxy.Civilizations.OrderBy(value => value.Id))
            {
                var state = loaded.AdaptiveResearch.Civilizations.TryGetValue(
                    civilization.Id, out var found) ? found : null;
                var scanner = state is not null &&
                    (state.HasCapability("tech:quantum_sensors") ||
                     state.HasCapability("tech:distributed_sensor_network"));
                FleetCombatPower.RecordSensorContacts(
                    loaded.Galaxy, civilization.Id, endDay, scanner);
            }
            _ = new AdaptiveResearchCampaignSimulation().Advance(
                loaded.Galaxy, loaded.AdaptiveResearch, elapsedDays, endDay);
            _ = diplomacyRuntime.Process(
                coreResult.ExplorationEvents, coreResult.CombatEvents, endDay);
            result = Payload(service.PrepareSave(
                loaded.Galaxy, endDay, loaded.Diplomacy,
                loaded.AdaptiveResearch));
        }
        catch (Exception exception) { error = Unwrap(exception); }
        var after = JsonNode.Parse(File.ReadAllText(path))!;
        rows.Add(new(name, "RestoreAdvance", input, before, after, result,
            error?.GetType().Name, error?.Message,
            error?.InnerException?.GetType().Name,
            error?.InnerException?.Message));
    }

    private static void AddActivationFailure(List<Row> rows,
        CampaignStatePersistenceService service, string scratch,
        string name, JsonObject input)
    {
        var path = Path.Combine(scratch, name + ".json");
        File.WriteAllText(path, input.ToJsonString(Json), new UTF8Encoding(false));
        var before = input.DeepClone();
        Exception? error = null;
        try
        {
            var loaded = service.Load(path);
            var runtime = new DiplomacyCampaignRuntimeCoordinator(loaded.Diplomacy);
            runtime.Reset(loaded.SimulationDays, reviewImmediately: true);
        }
        catch (Exception exception) { error = Unwrap(exception); }
        var after = JsonNode.Parse(File.ReadAllText(path))!;
        rows.Add(new(name, "RestoreActivate", input, before, after, null,
            error?.GetType().Name, error?.Message,
            error?.InnerException?.GetType().Name,
            error?.InnerException?.Message));
    }

    private static void AddActivationSuccess(List<Row> rows,
        CampaignStatePersistenceService service, string scratch,
        string name, JsonObject input)
    {
        var path = Path.Combine(scratch, name + ".json");
        File.WriteAllText(path, input.ToJsonString(Json), new UTF8Encoding(false));
        var before = input.DeepClone();
        JsonNode? result = null;
        Exception? error = null;
        try
        {
            var loaded = service.Load(path);
            var runtime = new DiplomacyCampaignRuntimeCoordinator(loaded.Diplomacy);
            runtime.Reset(loaded.SimulationDays, reviewImmediately: true);
            result = Payload(service.PrepareSave(
                loaded.Galaxy, loaded.SimulationDays, loaded.Diplomacy,
                loaded.AdaptiveResearch));
        }
        catch (Exception exception) { error = Unwrap(exception); }
        var after = JsonNode.Parse(File.ReadAllText(path))!;
        rows.Add(new(name, "RestoreActivateSuccess", input, before, after, result,
            error?.GetType().Name, error?.Message,
            error?.InnerException?.GetType().Name,
            error?.InnerException?.Message));
    }

    private static JsonObject Payload(PreparedCampaignSave prepared)
    {
        var payload = PreparedPayload.GetValue(prepared)!;
        var result = JsonSerializer.SerializeToNode(payload, payload.GetType(), Json)!.AsObject();
        result["SavedAtUtc"] = "2044-05-06T07:08:09+00:00";
        return result;
    }

    private static void AddCapture(List<Row> rows, string name, JsonObject input,
        Func<JsonObject> invoke)
        => AddCapture(rows, name, input, input, invoke);

    private static void AddCapture(List<Row> rows, string name, JsonObject input,
        JsonObject after, Func<JsonObject> invoke)
    {
        var before = input.DeepClone();
        JsonNode? result = null;
        Exception? error = null;
        try { result = invoke(); }
        catch (Exception exception) { error = Unwrap(exception); }
        rows.Add(new(name, "Capture", input.DeepClone(), before, after.DeepClone(), result,
            error?.GetType().Name, error?.Message, error?.InnerException?.GetType().Name,
            error?.InnerException?.Message));
    }

    private static void AddLoad(List<Row> rows, CampaignStatePersistenceService service,
        string scratch, string name, JsonObject input)
    {
        var path = Path.Combine(scratch, name + ".json");
        File.WriteAllText(path, input.ToJsonString(Json), new UTF8Encoding(false));
        var before = input.DeepClone();
        JsonNode? result = null;
        Exception? error = null;
        try
        {
            var loaded = service.Load(path);
            result = Payload(service.PrepareSave(loaded.Galaxy, loaded.SimulationDays,
                loaded.Diplomacy, loaded.AdaptiveResearch));
        }
        catch (Exception exception) { error = Unwrap(exception); }
        var after = JsonNode.Parse(File.ReadAllText(path))!;
        rows.Add(new(name, "Restore", input, before, after, result,
            error?.GetType().Name, error?.Message, error?.InnerException?.GetType().Name,
            error?.InnerException?.Message));
    }

    private static JsonObject Change(JsonObject source, Action<JsonObject> change)
    {
        var result = source.DeepClone().AsObject();
        change(result);
        return result;
    }

    private static JsonObject CaptureInput(JsonObject source, object simulationDays,
        bool developer)
    {
        return Change(source, root => root["Control"] = new JsonObject
        {
            ["SimulationDays"] = JsonSerializer.SerializeToNode(simulationDays),
            ["DeveloperProvenance"] = developer,
        });
    }

    private static Exception Unwrap(Exception error) =>
        error is TargetInvocationException { InnerException: not null } target
            ? target.InnerException! : error;

    private static string ExclusiveScratch()
    {
        var parent = Path.GetFullPath(Path.GetTempPath());
        for (var attempt = 0; attempt < 32; attempt++)
        {
            var path = Path.Combine(parent, "stellar-player091-" + Guid.NewGuid().ToString("N"));
            try { Directory.CreateDirectory(path); return path; }
            catch (IOException) { }
        }
        throw new IOException("Could not claim an exclusive Gate091 scratch directory.");
    }

    private static void CheckedCleanup(string path)
    {
        var full = Path.GetFullPath(path);
        var parent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar)
            + Path.DirectorySeparatorChar;
        if (!full.StartsWith(parent, StringComparison.OrdinalIgnoreCase))
            throw new IOException("Refusing to clean an unowned scratch path.");
        Directory.Delete(full, recursive: true);
        if (Directory.Exists(full))
            throw new IOException("Could not clean the owned scratch directory.");
    }
}
