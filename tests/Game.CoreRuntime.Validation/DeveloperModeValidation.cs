using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.Construction;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Shipbuilding;

namespace Game.CoreRuntime.Validation;

internal static class DeveloperModeValidation
{
    public static void ValidateCommandAuthorityAndIsolation() => WithDirectory(directory =>
    {
        var player = new CampaignSessionService().CreateNew(20260908).Galaxy;
        var original = JsonSerializer.Serialize(player);
        var callbackCount = 0;
        foreach (var command in DeveloperCommandService.Commands)
            Require(!DeveloperCommandService.Execute(player, command.Id, _ => callbackCount++).Accepted,
                "Player mode accepted Developer command " + command.Id);
        Require(callbackCount == 0 && player.DeveloperSession is null && original == JsonSerializer.Serialize(player),
            "a rejected Player command mutated state, called the time callback, or enabled Developer mode");
        var session = new DeveloperCampaignSessionService().CreateNew(20260908);
        var galaxy = session.Galaxy;
        original = JsonSerializer.Serialize(galaxy);
        Require(!DeveloperCommandService.Execute(galaxy, "unknown_command").Accepted &&
            !DeveloperCommandService.Execute(galaxy, "advance_30_days").Accepted &&
            original == JsonSerializer.Serialize(galaxy), "invalid Developer commands mutated or marked the campaign");
        var foreign = ForeignState(galaxy);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == galaxy.PlayerCivilizationId);
        var credits = economy.Credits; var industry = economy.Industry; var science = economy.Science;
        Require(DeveloperCommandService.Execute(galaxy, "grant_resources").Accepted &&
            economy.Credits == credits + 1000 && economy.Industry == industry + 1000 && economy.Science == science &&
            galaxy.DeveloperSession is { ToolsUsed: true } && foreign == ForeignState(galaxy),
            "explicit resource grant did not match its description, mark provenance, or preserve other civilizations");
        var projects = galaxy.ConstructionStates.Single(item => item.CivilizationId == galaxy.PlayerCivilizationId);
        projects.ActiveProjectId = "orbital_launch_complex"; projects.ActiveProjectProgress = 42;
        var projectsBeforeUnlock = JsonSerializer.Serialize(projects);
        Require(DeveloperCommandService.Execute(galaxy, "unlock_research").Accepted &&
            projectsBeforeUnlock == JsonSerializer.Serialize(projects) && foreign == ForeignState(galaxy),
            "research-only unlock completed infrastructure, changed an active timer, or affected a foreign civilization");
        Require(DeveloperCommandService.Execute(galaxy, "unlock_technology").Accepted &&
            TechnologyRegistry.All.All(technology => galaxy.Technologies.Single(item => item.CivilizationId == galaxy.PlayerCivilizationId)
                .CompletedTechnologyIds.Contains(technology.Id)) && foreign == ForeignState(galaxy),
            "technology tool missed its owning civilization or changed another civilization");
        Require(DeveloperCommandService.Execute(galaxy, "reveal_galaxy").Accepted &&
            galaxy.Systems.All(system => galaxy.Knowledge.GetSystemSurveyLevel(galaxy.PlayerCivilizationId, system.Id) == SystemSurveyLevel.FullySurveyed) &&
            foreign == ForeignState(galaxy), "survey tool changed another observer or left its own requested survey incomplete");
        var advanced = 0.0;
        Require(DeveloperCommandService.Execute(galaxy, "advance_30_days", days =>
        {
            Require(galaxy.DeveloperSession is { ToolsUsed: true }, "time callback ran before provenance was recorded");
            callbackCount++; advanced += days;
        }).Accepted && callbackCount == 1 && advanced == 30, "advance tool bypassed or duplicated the campaign's canonical time boundary");
        var path = Path.Combine(directory, DeveloperCampaignSessionService.SaveFileName);
        new DeveloperCampaignPersistenceService().Save(path, galaxy, 30, session.Diplomacy);
        var loaded = new DeveloperCampaignPersistenceService().Load(path);
        Require(loaded.Galaxy.DeveloperSession is { ToolsUsed: true } &&
            loaded.Galaxy.Economies.Single(item => item.CivilizationId == galaxy.PlayerCivilizationId).Industry == economy.Industry,
            "reload cleared ToolsUsed or lost an explicit development grant");
        Require(!DeveloperCommandService.Execute(loaded.Galaxy, "unknown_command").Accepted &&
            loaded.Galaxy.DeveloperSession is { ToolsUsed: true }, "a rejected command cleared saved provenance");
        var adaptiveSession = new DeveloperCampaignSessionService().CreateNew(20260908);
        Require(DeveloperCommandService.Execute(adaptiveSession.Galaxy, "unlock_technology").Accepted,
            "could not prepare the Adaptive Developer round-trip state");
        _ = new AdaptiveResearchCampaignSimulation().Advance(
            adaptiveSession.Galaxy, adaptiveSession.AdaptiveResearch, 30, 30);
        Require(new ShipbuildingSimulation().StartBuild(
                adaptiveSession.Galaxy, adaptiveSession.Galaxy.PlayerCivilizationId, "warp_scout").Accepted,
            "could not prepare the Adaptive Developer ship order");
        AdaptiveResearchCampaignProgression.SynchronizeDevelopmentStages(
            adaptiveSession.Galaxy, adaptiveSession.AdaptiveResearch);
        var adaptivePath = Path.Combine(directory, "adaptive-developer.json");
        var developerPersistence = new DeveloperCampaignPersistenceService();
        developerPersistence.Save(adaptivePath, adaptiveSession.Galaxy, 30, adaptiveSession.Diplomacy,
            adaptiveSession.AdaptiveResearch);
        var adaptiveExpected = SemanticDeveloperSave(adaptivePath);
        var adaptiveLoaded = developerPersistence.Load(adaptivePath);
        AdaptiveResearchCampaignProgression.SynchronizeDevelopmentStages(
            adaptiveLoaded.Galaxy, adaptiveLoaded.AdaptiveResearch);
        developerPersistence.Save(adaptivePath, adaptiveLoaded.Galaxy, adaptiveLoaded.SimulationDays,
            adaptiveLoaded.Diplomacy, adaptiveLoaded.AdaptiveResearch);
        var adaptiveActual = SemanticDeveloperSave(adaptivePath);
        Require(JsonNode.DeepEquals(adaptiveExpected, adaptiveActual),
            "Adaptive AI state changed across a Developer save/load/checkpoint round trip: " +
            FirstJsonDifference(adaptiveExpected.ToJsonString(), adaptiveActual.ToJsonString()));
        var interrupted = new DeveloperCampaignSessionService().CreateNew(20260908).Galaxy;
        Reject(() => DeveloperCommandService.Execute(interrupted, "advance_30_days", _ =>
            throw new InvalidOperationException("deliberate validation callback failure")), "interrupted callback did not fail");
        Require(interrupted.DeveloperSession is { ToolsUsed: true }, "a partially failed explicit command lost provenance");
        Reject(() => new CampaignStatePersistenceService().Save(Path.Combine(directory, "autosave.json"),
            loaded.Galaxy, 30, loaded.Diplomacy), "tainted Developer state could be saved as Player");
    });

    public static void ValidateFinishOrdersScope()
    {
        var galaxy = new DeveloperCampaignSessionService().CreateNew(20260908).Galaxy;
        var playerId = galaxy.PlayerCivilizationId;
        var foreignId = galaxy.Civilizations.First(item => item.Id != playerId).Id;
        foreach (var civilization in new[] { playerId, foreignId })
        {
            Require(new ResearchSimulation().StartResearch(galaxy, civilization, "fusion_propulsion").Accepted,
                "test campaign has no first research project");
            Require(new ConstructionSimulation().StartProject(galaxy, civilization, "research_network").Accepted,
                "test campaign has no first construction project");
            var resources = galaxy.Economies.Single(item => item.CivilizationId == civilization);
            resources.Industry = 10000; resources.Science = 10000;
        }
        var colony = galaxy.Colonies.Where(item => item.CivilizationId == playerId).MaxBy(item => item.PopulationMillions)!;
        Require(SurfaceConstruction.Place(galaxy, playerId, colony.Id, "science_lab", 110.125f, -85.375f, 15).Accepted,
            "could not place the test's ordinary surface order");
        var foreign = ForeignState(galaxy);
        Require(DeveloperCommandService.Execute(galaxy, "finish_orders").Accepted &&
            galaxy.Technologies.Single(item => item.CivilizationId == playerId).CompletedTechnologyIds.Contains("fusion_propulsion") &&
            galaxy.ConstructionStates.Single(item => item.CivilizationId == playerId).CompletedProjectIds.Contains("research_network") &&
            colony.SurfaceBuildings.Single().IsComplete && foreign == ForeignState(galaxy),
            "finish orders missed the owner's pending work or advanced another civilization's funded orders");
    }

    public static void ValidateAlreadyPaidOrderCompletion() => WithDirectory(directory =>
    {
        var session = new DeveloperCampaignSessionService().CreateNew(20260908);
        var galaxy = session.Galaxy;
        var playerId = galaxy.PlayerCivilizationId;
        Require(DeveloperCommandService.Execute(galaxy, "unlock_technology").Accepted,
            "could not establish a valid developed shipbuilding checkpoint");
        var shipbuilding = new ShipbuildingSimulation();
        foreach (var design in new[] { "colony_ship", "colony_ship", "science_vessel" })
            Require(shipbuilding.StartBuild(galaxy, playerId, design).Accepted, "could not establish a real population-reserving queue");
        var research = galaxy.Technologies.Single(item => item.CivilizationId == playerId);
        research.CompletedTechnologyIds.Remove("fusion_propulsion");
        research.ActiveResearchId = "fusion_propulsion";
        research.ActiveResearchProgress = TechnologyRegistry.Get("fusion_propulsion").ResearchCost;
        var project = galaxy.ConstructionStates.Single(item => item.CivilizationId == playerId);
        project.CompletedProjectIds.Remove("research_network");
        project.ActiveProjectId = "research_network";
        project.ActiveProjectProgress = ConstructionRegistry.Get("research_network").IndustryCost;
        galaxy.ShipyardStates.Single(item => item.CivilizationId == playerId).ActiveBuildProgress =
            ShipDesignRegistry.Get("colony_ship").IndustryCost;
        var economy = galaxy.Economies.Single(item => item.CivilizationId == playerId);
        economy.Science = economy.Industry = 0;
        var path = Path.Combine(directory, DeveloperCampaignSessionService.SaveFileName);
        var persistence = new DeveloperCampaignPersistenceService();
        persistence.Save(path, galaxy, 123, session.Diplomacy);
        var restored = persistence.Load(path);
        galaxy = restored.Galaxy;
        research = galaxy.Technologies.Single(item => item.CivilizationId == playerId);
        project = galaxy.ConstructionStates.Single(item => item.CivilizationId == playerId);
        economy = galaxy.Economies.Single(item => item.CivilizationId == playerId);
        var shipyard = galaxy.ShipyardStates.Single(item => item.CivilizationId == playerId);
        var colony = galaxy.Colonies.Where(item => item.CivilizationId == playerId).MaxBy(item => item.PopulationMillions)!;
        var sourcePopulation = colony.PopulationMillions;
        var cargo = shipyard.ReservedPopulationMillions;
        var species = shipyard.ReservedPopulationSpeciesId;
        var next = shipyard.QueuedBuilds[0];
        var trailingQueue = JsonSerializer.Serialize(shipyard.QueuedBuilds.Skip(1));
        var foreign = ForeignState(galaxy);
        var oldFleetIds = galaxy.Fleets.Select(fleet => fleet.Id).ToHashSet();
        var credits = economy.Credits;
        Require(DeveloperCommandService.Execute(galaxy, "finish_orders").Accepted,
            "finish orders rejected a canonical already-paid checkpoint");
        Require(research.ActiveResearchId is null && research.CompletedTechnologyIds.Contains("fusion_propulsion") &&
            project.ActiveProjectId is null && project.CompletedProjectIds.Contains("research_network") &&
            economy.Industry == 0 && economy.Science == 0 && economy.Credits == credits,
            "already-paid research/project stayed active or charged resources twice");
        var completed = galaxy.Fleets.Where(fleet => !oldFleetIds.Contains(fleet.Id)).ToArray();
        Require(completed.Length == 1 && completed[0].CivilizationId == playerId && completed[0].Role == FleetRole.Colony &&
            completed[0].EmbarkedPopulationMillions == cargo && completed[0].EmbarkedPopulationSpeciesId == species &&
            colony.PopulationMillions == sourcePopulation && shipyard.ActiveDesignId == next.DesignId &&
            shipyard.ActiveBuildProgress == 0 && shipyard.ReservedPopulationMillions == next.ReservedPopulationMillions &&
            shipyard.ReservedPopulationSpeciesId == next.ReservedPopulationSpeciesId &&
            trailingQueue == JsonSerializer.Serialize(shipyard.QueuedBuilds) && foreign == ForeignState(galaxy),
            "already-paid ship failed physical cargo handoff, spent queued reservations, or advanced another civilization");
    });

    public static void ValidateUnmodifiedOpening()
    {
        var player = new CampaignSessionService().CreateNew(20260908);
        var developer = new DeveloperCampaignSessionService().CreateNew(20260908);
        Require(player.Galaxy.DeveloperSession is null && developer.Galaxy.DeveloperSession is { ToolsUsed: false },
            "fresh mode provenance is absent, incorrect, or already claims tool use");
        EqualOpening(player, developer);
        var coordinator = new GalaxySimulationStepCoordinator();
        coordinator.Advance(player.Galaxy, .25);
        coordinator.Advance(developer.Galaxy, .25);
        EqualOpening(player, developer);
        Require(developer.Galaxy.DeveloperSession is { ToolsUsed: false }, "ordinary simulation marked tools as used");
    }

    public static void ValidatePlayerSaveBoundary() => WithDirectory(directory =>
    {
        var player = new CampaignSessionService().CreateNew(20260908);
        var developer = new DeveloperCampaignSessionService().CreateNew(20260908);
        var canonical = new CampaignStatePersistenceService();
        var path = Path.Combine(directory, "autosave.json");
        canonical.Save(path, player.Galaxy, 1, player.Diplomacy);
        canonical.Save(path, player.Galaxy, 2, player.Diplomacy);
        var primary = File.ReadAllBytes(path);
        var backup = File.ReadAllBytes(path + ".bak");
        foreach (var save in new Action[]
        {
            () => canonical.Save(path, developer.Galaxy, 30, developer.Diplomacy),
            () => canonical.SavePreservingBackup(path, developer.Galaxy, 30, developer.Diplomacy),
            () => new CampaignSessionService().Save(path, developer.Galaxy, developer.Diplomacy, 30),
            () => new CampaignSaveService().Save(path, developer.Galaxy, 30),
        })
        {
            Reject(save, "canonical Player persistence accepted Developer state");
            Require(primary.SequenceEqual(File.ReadAllBytes(path)) && backup.SequenceEqual(File.ReadAllBytes(path + ".bak")),
                "a rejected Developer save changed the Player primary or backup");
        }
        Reject(() => new DeveloperCampaignSessionService().LoadExisting(
                Path.Combine(directory, DeveloperCampaignSessionService.SaveFileName)),
            "explicit Developer load opened the separate Player slot");
        Require(primary.SequenceEqual(File.ReadAllBytes(path)) && backup.SequenceEqual(File.ReadAllBytes(path + ".bak")),
            "cross-mode explicit Developer load changed the Player save pair");
        Require(Directory.GetFiles(directory).Length == 2, "a rejected cross-mode save left temporary payloads behind");
    });

    public static void ValidateDeveloperSaveContinuity() => WithDirectory(directory =>
    {
        var session = new DeveloperCampaignSessionService().CreateNew(20260908);
        var galaxy = session.Galaxy;
        var player = galaxy.PlayerCivilizationId;
        var colony = galaxy.Colonies.Where(item => item.CivilizationId == player).MaxBy(item => item.PopulationMillions)!;
        Require(SurfaceConstruction.Place(galaxy, player, colony.Id, "science_lab", 100.125f, -80.375f, 22.5f).Accepted,
            "ordinary Developer surface placement was rejected");
        SurfaceConstruction.Advance(galaxy, player, 7.5, .5);
        var before = JsonSerializer.Serialize(colony.SurfaceBuildings);
        var path = Path.Combine(directory, DeveloperCampaignSessionService.SaveFileName);
        var persistence = new DeveloperCampaignPersistenceService();
        persistence.Save(path, galaxy, 17.125, session.Diplomacy);
        var envelope = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        Require(envelope.Count == 4 && envelope["DeveloperFormatVersion"]!.GetValue<int>() == 1 &&
            envelope["Mode"]!.GetValue<string>() == "Developer" && !envelope["ToolsUsed"]!.GetValue<bool>() &&
            envelope["Campaign"]!["FormatVersion"]!.GetValue<int>() == 17,
            "Developer save is not the explicit versioned envelope around the canonical surface campaign");
        var loaded = persistence.Load(path);
        Require(loaded.Galaxy.DeveloperSession is { ToolsUsed: false } && loaded.SimulationDays == 17.125 &&
            before == JsonSerializer.Serialize(loaded.Galaxy.Colonies.Single(item => item.Id == colony.Id).SurfaceBuildings) &&
            JsonSerializer.Serialize(loaded.Diplomacy.Snapshot()) == JsonSerializer.Serialize(session.Diplomacy.Snapshot()),
            "Developer reload lost mode, time, diplomacy, or precise surface progress/coordinates");
        Reject(() => new CampaignStatePersistenceService().Load(path), "Player campaign reader accepted Developer envelope");
        Reject(() => new CampaignSaveService().Load(path), "Player galaxy reader accepted Developer envelope");
        persistence.Save(path, galaxy, 18, session.Diplomacy);
        var backup = File.ReadAllBytes(path + ".bak");
        persistence.Save(path, galaxy, 19, session.Diplomacy, preserveExistingBackup: true);
        Require(backup.SequenceEqual(File.ReadAllBytes(path + ".bak")), "Developer repair save destroyed its known-good backup");
        var primary = File.ReadAllBytes(path);
        var explicitlyLoaded = new DeveloperCampaignSessionService().LoadExisting(path);
        Require(explicitlyLoaded.Source == CampaignBootstrapSource.LoadedSave && explicitlyLoaded.SimulationDays == 19 &&
                explicitlyLoaded.Galaxy.DeveloperSession is { ToolsUsed: false } &&
                primary.SequenceEqual(File.ReadAllBytes(path)) && backup.SequenceEqual(File.ReadAllBytes(path + ".bak")),
            "explicit Developer load changed its save pair or lost mode provenance");
        File.WriteAllText(path, "{broken");
        var corruptPrimary = File.ReadAllBytes(path);
        var explicitRecovery = new DeveloperCampaignSessionService().LoadExisting(path);
        Require(explicitRecovery.RecoveredFromBackup && explicitRecovery.SimulationDays == 17.125 &&
                explicitRecovery.Galaxy.DeveloperSession is { ToolsUsed: false } &&
                corruptPrimary.SequenceEqual(File.ReadAllBytes(path)) && backup.SequenceEqual(File.ReadAllBytes(path + ".bak")),
            "explicit Developer backup recovery modified a save or lost mode provenance");
        var recovered = new DeveloperCampaignSessionService().LoadOrCreate(path, -1);
        Require(recovered.RecoveredFromBackup && recovered.Seed == galaxy.Seed && recovered.SimulationDays == 17.125 &&
            recovered.Galaxy.DeveloperSession is { ToolsUsed: false } &&
            before == JsonSerializer.Serialize(recovered.Galaxy.Colonies.Single(item => item.Id == colony.Id).SurfaceBuildings),
            "Developer recovery lost provenance or restored the wrong generation of surface progress");
        File.WriteAllText(path + ".bak", "{also-broken");
        var brokenPrimary = File.ReadAllBytes(path);
        var brokenBackup = File.ReadAllBytes(path + ".bak");
        Reject(() => new DeveloperCampaignSessionService().LoadExisting(path),
            "explicit Developer load generated a replacement for a corrupt save pair");
        Require(brokenPrimary.SequenceEqual(File.ReadAllBytes(path)) && brokenBackup.SequenceEqual(File.ReadAllBytes(path + ".bak")),
            "failed explicit Developer load modified its corrupt save pair");
    });

    public static void ValidateInvalidEnvelopes() => WithDirectory(directory =>
    {
        var session = new DeveloperCampaignSessionService().CreateNew(20260908);
        var path = Path.Combine(directory, DeveloperCampaignSessionService.SaveFileName);
        var persistence = new DeveloperCampaignPersistenceService();
        persistence.Save(path, session.Galaxy, 5, session.Diplomacy);
        var valid = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        var cases = new List<(string Name, Action<JsonObject> Mutate)>();
        foreach (var field in new[] { "DeveloperFormatVersion", "Mode", "ToolsUsed", "Campaign" })
        {
            var name = field;
            cases.Add(("missing " + name, envelope => envelope.Remove(name)));
            cases.Add(("null " + name, envelope => envelope[name] = null));
        }
        cases.AddRange(new (string, Action<JsonObject>)[]
        {
            ("unknown envelope version", envelope => envelope["DeveloperFormatVersion"] = 2),
            ("Player mode", envelope => envelope["Mode"] = "Player"),
            ("string taint", envelope => envelope["ToolsUsed"] = "false"),
            ("non-object campaign", envelope => envelope["Campaign"] = new JsonArray()),
            ("unknown campaign version", envelope => envelope["Campaign"]!["FormatVersion"] = 999),
            ("standalone instead of diplomacy campaign", envelope => envelope["Campaign"]!["FormatVersion"] = 10),
            ("contradictory nested mode", envelope => envelope["Campaign"]!["Mode"] = "Player"),
            ("nested Developer marker", envelope => envelope["Campaign"]!["Galaxy"]!["DeveloperSession"] =
                new JsonObject { ["ToolsUsed"] = false }),
        });
        foreach (var (name, mutate) in cases)
        {
            var malformed = valid.DeepClone().AsObject();
            mutate(malformed);
            var text = malformed.ToJsonString();
            File.WriteAllText(path, text);
            Reject(() => persistence.Load(path), "Developer reader accepted " + name);
            Require(File.ReadAllText(path) == text, "failed Developer load changed its source file");
        }
    });

    public static void ValidateLegacyImportIsolation() => WithDirectory(directory =>
    {
        var original = new CampaignSessionService().CreateNew(987654321);
        var legacyPath = Path.Combine(directory, "demo-autosave.json");
        var developerPath = Path.Combine(directory, DeveloperCampaignSessionService.SaveFileName);
        var persistence = new CampaignStatePersistenceService();
        persistence.Save(legacyPath, original.Galaxy, 10, original.Diplomacy);
        persistence.Save(legacyPath, original.Galaxy, 20, original.Diplomacy);
        var primary = File.ReadAllBytes(legacyPath);
        var backup = File.ReadAllBytes(legacyPath + ".bak");
        var sessions = new DeveloperCampaignSessionService();
        var imported = sessions.LoadOrCreate(developerPath, -1);
        Require(imported.WasLoaded && imported.Seed == original.Seed && imported.SimulationDays == 20 &&
            imported.Galaxy.DeveloperSession is { ToolsUsed: false } && !File.Exists(developerPath) &&
            primary.SequenceEqual(File.ReadAllBytes(legacyPath)) && backup.SequenceEqual(File.ReadAllBytes(legacyPath + ".bak")),
            "legacy demo import mutated its originals, rewound time, or granted Developer tools");
        new DeveloperCampaignPersistenceService().Save(developerPath, imported.Galaxy, 25, imported.Diplomacy);
        var reopened = sessions.LoadOrCreate(developerPath, -1);
        Require(reopened.WasLoaded && reopened.SimulationDays == 25 &&
            primary.SequenceEqual(File.ReadAllBytes(legacyPath)) && backup.SequenceEqual(File.ReadAllBytes(legacyPath + ".bak")),
            "explicit Developer checkpoint overwrote legacy saves or silently reimported their older state");
        File.WriteAllText(developerPath, "invalid Developer primary");
        var failed = sessions.LoadOrCreate(developerPath, 111);
        Require(failed.RecoveredFromInvalidSave && failed.Seed == 111 && failed.SimulationDays == 0 &&
            failed.Galaxy.DeveloperSession is { ToolsUsed: false } &&
            primary.SequenceEqual(File.ReadAllBytes(legacyPath)) && backup.SequenceEqual(File.ReadAllBytes(legacyPath + ".bak")),
            "corrupt existing Developer state silently resurrected an older legacy demo");
    });

    private static string ForeignState(GalaxyState galaxy) => JsonSerializer.Serialize(new
    {
        Civilizations = galaxy.Civilizations.Where(item => item.Id != galaxy.PlayerCivilizationId),
        Economies = galaxy.Economies.Where(item => item.CivilizationId != galaxy.PlayerCivilizationId),
        Technologies = galaxy.Technologies.Where(item => item.CivilizationId != galaxy.PlayerCivilizationId),
        Construction = galaxy.ConstructionStates.Where(item => item.CivilizationId != galaxy.PlayerCivilizationId),
        Shipyards = galaxy.ShipyardStates.Where(item => item.CivilizationId != galaxy.PlayerCivilizationId),
        Fleets = galaxy.Fleets.Where(item => item.CivilizationId != galaxy.PlayerCivilizationId),
        Colonies = galaxy.Colonies.Where(item => item.CivilizationId != galaxy.PlayerCivilizationId),
        Surveys = galaxy.Civilizations.Where(item => item.Id != galaxy.PlayerCivilizationId).Select(civilization =>
            galaxy.Systems.Select(system => (int)galaxy.Knowledge.GetSystemSurveyLevel(civilization.Id, system.Id)).ToArray()),
    });

    private static JsonObject SemanticDeveloperSave(string path)
    {
        var envelope = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        envelope["Campaign"]!.AsObject().Remove("SavedAtUtc");
        return envelope;
    }

    private static string FirstJsonDifference(string expected, string actual)
    {
        var index = 0;
        while (index < expected.Length && index < actual.Length && expected[index] == actual[index]) index++;
        var start = Math.Max(0, index - 80);
        return $"offset {index}; expected '{expected[start..Math.Min(expected.Length, index + 160)]}'; " +
               $"actual '{actual[start..Math.Min(actual.Length, index + 160)]}'";
    }

    private static void EqualOpening(CampaignBootstrapResult player, CampaignBootstrapResult developer)
    {
        Require(player.Seed == developer.Seed && player.SimulationDays == developer.SimulationDays &&
            JsonSerializer.Serialize(player.Galaxy.Economies) == JsonSerializer.Serialize(developer.Galaxy.Economies) &&
            JsonSerializer.Serialize(player.Galaxy.Technologies) == JsonSerializer.Serialize(developer.Galaxy.Technologies) &&
            JsonSerializer.Serialize(player.Galaxy.Fleets) == JsonSerializer.Serialize(developer.Galaxy.Fleets) &&
            JsonSerializer.Serialize(player.Galaxy.Colonies) == JsonSerializer.Serialize(developer.Galaxy.Colonies),
            "Developer opening gained resources, technology, fleets, or colonies without an explicit command");
    }

    private static void Reject(Action action, string message)
    {
        try { action(); }
        catch (Exception exception) when (exception is InvalidDataException or FileNotFoundException or JsonException or InvalidOperationException or ArgumentException)
        { return; }
        throw new InvalidOperationException(message);
    }

    private static void WithDirectory(Action<string> run)
    {
        var directory = Path.Combine(Path.GetTempPath(), "stellar-developer-validation-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try { run(directory); }
        finally
        {
            var temporaryRoot = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            Require(string.Equals(Path.GetDirectoryName(Path.GetFullPath(directory)), temporaryRoot, StringComparison.OrdinalIgnoreCase),
                "refusing cleanup outside the test's exact temporary directory");
            Directory.Delete(directory, recursive: true);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
