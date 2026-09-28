using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Research.Adaptive;
using Game.Simulation.Species;

namespace Game.CoreRuntime.Validation;

internal static class PlanetaryCatalogPersistenceValidation
{
    internal static void Run()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"stellar-planetary-catalog-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            PreservesGuaranteedCatalogAcrossGenerations(directory);
            MigratesHistoricalAdaptiveCatalogExactly(directory);
            RejectsMalformedCatalogWithoutTouchingFiles(directory);
        }
        finally { Directory.Delete(directory, recursive: true); }
    }

    private static void PreservesGuaranteedCatalogAcrossGenerations(string directory)
    {
        var sessions = new CampaignSessionService();
        // This seed specifically reproduces a guarantee-altered planet in the historical
        // barred-spiral generator. Keep that migration fixture independent of new defaults.
        var seed = CampaignSeed.Parse("PLANETARY-CATALOG-RECOVERY");
        var original = sessions.CreateNew(seed,
            GalaxyGenerationMetadata.Standard100("PLANETARY-CATALOG-RECOVERY", seed).ToSettings());
        var raw = new PlanetaryBodyGenerator().Generate(original.Galaxy.Seed, original.Galaxy.Systems)
            .ToDictionary(body => body.Id);
        Require(original.Galaxy.PlanetaryBodies.Any(body => raw.TryGetValue(body.Id, out var generated) &&
                (body.MassEarth != generated.MassEarth || body.Environment != generated.Environment)),
            "fixture did not contain a guarantee-altered world");
        var player = original.Galaxy.Civilizations.Single(civilization => civilization.Id == original.Galaxy.PlayerCivilizationId);
        var expectedBodies = original.Galaxy.PlanetaryBodies.ToArray();
        var expectedHabitability = Habitability(original.Galaxy, player.SpeciesId);
        var expectedKnowledge = Knowledge(original.Galaxy, player.Id);
        var firstPath = Path.Combine(directory, "first-v17.json");
        var secondPath = Path.Combine(directory, "second-v17.json");

        sessions.Save(firstPath, original.Galaxy, original.Diplomacy, original.AdaptiveResearch, 42.5);
        var first = sessions.LoadExisting(firstPath);
        sessions.Save(secondPath, first.Galaxy, first.Diplomacy, first.AdaptiveResearch, first.SimulationDays);
        var second = sessions.LoadExisting(secondPath);

        Require(first.Galaxy.PlanetaryBodies.SequenceEqual(expectedBodies) &&
                second.Galaxy.PlanetaryBodies.SequenceEqual(expectedBodies),
            "two save generations changed the authoritative planetary catalog");
        Require(Habitability(first.Galaxy, player.SpeciesId) == expectedHabitability &&
                Habitability(second.Galaxy, player.SpeciesId) == expectedHabitability,
            "planetary catalog persistence changed species-relative viability");
        Require(Knowledge(first.Galaxy, player.Id) == expectedKnowledge && Knowledge(second.Galaxy, player.Id) == expectedKnowledge,
            "planetary catalog persistence changed survey knowledge");
        using var document = JsonDocument.Parse(File.ReadAllText(secondPath));
        Require(document.RootElement.GetProperty("FormatVersion").GetInt32() == 17 &&
                document.RootElement.GetProperty("GalaxyFormatVersion").GetInt32() == 16 &&
                document.RootElement.GetProperty("Galaxy").GetProperty("PlanetaryBodies").GetArrayLength() == expectedBodies.Length,
            "new campaign did not declare and contain the complete v17/v16 catalog");
        var earth = second.Galaxy.PlanetaryBodies.Single(body => body.Id == SolCatalogPreset.EarthBodyId);
        Require(earth == expectedBodies.Single(body => body.Id == SolCatalogPreset.EarthBodyId),
            "catalog persistence changed canonical Earth facts");
    }

    private static void MigratesHistoricalAdaptiveCatalogExactly(string directory)
    {
        var sessions = new CampaignSessionService();
        var original = sessions.CreateNew(123456789L);
        Require(AdaptiveResearchCampaignCommands.StartDirectedResearch(
                original.Galaxy, original.AdaptiveResearch, original.Galaxy.PlayerCivilizationId, "fusion_power", 4).Accepted,
            "historical v15 fixture could not start bounded Adaptive Research");
        var currentPath = Path.Combine(directory, "historical-source.json");
        var oldPath = Path.Combine(directory, "historical-v15.json");
        sessions.Save(currentPath, original.Galaxy, original.Diplomacy, original.AdaptiveResearch, 19);
        var root = JsonNode.Parse(File.ReadAllText(currentPath))!.AsObject();
        root["FormatVersion"] = CampaignStatePersistenceService.AdaptiveFormatVersion;
        root["GalaxyFormatVersion"] = CampaignSaveService.PresetFormatVersion;
        root["Galaxy"]!.AsObject().Remove("PlanetaryBodies");
        File.WriteAllText(oldPath, root.ToJsonString());

        var loaded = new CampaignStatePersistenceService().Load(oldPath);
        Require(loaded.Galaxy.PlanetaryBodies.SequenceEqual(original.Galaxy.PlanetaryBodies),
            "historical v15 load did not reconstruct its exact saved-system catalog");
        Require(loaded.Galaxy.GenerationMetadata == original.Galaxy.GenerationMetadata,
            "historical v15 load rewrote generation metadata");
        Require(loaded.AdaptiveResearch.GetCivilization(original.Galaxy.PlayerCivilizationId)
                .ActiveProjects.ContainsKey("fusion_power"),
            "historical v15 load lost its Adaptive Research snapshot");
        var resaved = Path.Combine(directory, "historical-resaved-v17.json");
        sessions.Save(resaved, loaded.Galaxy, loaded.Diplomacy, loaded.AdaptiveResearch, loaded.SimulationDays);
        Require(sessions.LoadExisting(resaved).Galaxy.PlanetaryBodies.SequenceEqual(original.Galaxy.PlanetaryBodies),
            "resaving historical v15 did not snapshot its reconstructed catalog exactly");
    }

    private static void RejectsMalformedCatalogWithoutTouchingFiles(string directory)
    {
        var save = new CampaignSaveService();
        var galaxy = new GalaxyGenerator().Generate(778899);
        var path = Path.Combine(directory, "malformed-v16.json");
        save.Save(path, galaxy, 1);
        save.Save(path, galaxy, 2);
        var valid = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        var backupBytes = File.ReadAllBytes(path + ".bak");
        JsonArray Bodies(JsonObject root) => root["Galaxy"]!["PlanetaryBodies"]!.AsArray();
        JsonObject Planet(JsonObject root) => Bodies(root).Select(node => node!.AsObject())
            .First(body => body["Kind"]!.GetValue<int>() == (int)PlanetaryBodyKind.Planet);
        JsonObject Moon(JsonObject root) => Bodies(root).Select(node => node!.AsObject())
            .First(body => body["Kind"]!.GetValue<int>() == (int)PlanetaryBodyKind.Moon);
        var cases = new (string Name, Action<JsonObject> Change)[]
        {
            ("missing", root => root["Galaxy"]!.AsObject().Remove("PlanetaryBodies")),
            ("null", root => root["Galaxy"]!["PlanetaryBodies"] = null),
            ("empty", root => root["Galaxy"]!["PlanetaryBodies"] = new JsonArray()),
            ("null-entry", root => Bodies(root)[0] = null),
            ("duplicate-id", root => Bodies(root).Add(Bodies(root)[0]!.DeepClone())),
            ("unknown-system", root => Bodies(root)[0]!["SystemId"] = int.MaxValue),
            ("planet-parent", root => Planet(root)["ParentBodyId"] = Planet(root)["Id"]!.GetValue<int>()),
            ("moon-without-parent", root => Moon(root)["ParentBodyId"] = null),
            ("cross-system-parent", root => Moon(root)["ParentBodyId"] = Bodies(root).Select(node => node!.AsObject())
                .First(body => body["Kind"]!.GetValue<int>() == (int)PlanetaryBodyKind.Planet &&
                    body["SystemId"]!.GetValue<int>() != Moon(root)["SystemId"]!.GetValue<int>())["Id"]!.GetValue<int>()),
            ("parent-cycle", root =>
            {
                var moons = Bodies(root).Select(node => node!.AsObject()).Where(body =>
                    body["Kind"]!.GetValue<int>() == (int)PlanetaryBodyKind.Moon).Take(2).ToArray();
                moons[0]["ParentBodyId"] = moons[1]["Id"]!.GetValue<int>();
                moons[1]["ParentBodyId"] = moons[0]["Id"]!.GetValue<int>();
            }),
            ("negative-mass", root => Bodies(root)[0]!["MassEarth"] = -1),
            ("invalid-eccentricity", root => Bodies(root)[0]!["OrbitalEccentricity"] = 1.0),
            ("invalid-inclination", root => Bodies(root)[0]!["OrbitalInclinationDegrees"] = 181.0),
            ("missing-parent-field", root => Bodies(root)[0]!.AsObject().Remove("ParentBodyId")),
            ("unknown-kind", root => Bodies(root)[0]!["Kind"] = 999),
            ("missing-environment", root => Bodies(root)[0]!["Environment"] = null),
            ("missing-zero-valid-gravity", root => Bodies(root)[0]!["Environment"]!.AsObject().Remove("GravityG")),
            ("missing-zero-valid-pressure", root => Bodies(root)[0]!["Environment"]!.AsObject().Remove("PressureKPa")),
            ("missing-atmosphere", root => Bodies(root)[0]!["Environment"]!.AsObject().Remove("Atmosphere")),
            ("missing-solvent", root => Bodies(root)[0]!["Environment"]!.AsObject().Remove("AvailableSolvent")),
            ("missing-immersed-flag", root => Bodies(root)[0]!["Environment"]!.AsObject().Remove("IsImmersedEnvironment")),
            ("missing-solid-surface-flag", root => Bodies(root)[0]!["Environment"]!.AsObject().Remove("HasSolidSurface")),
            ("missing-resource-flag", root => Bodies(root)[0]!.AsObject().Remove("HasRareResource")),
            ("unknown-atmosphere", root => Bodies(root)[0]!["Environment"]!["Atmosphere"] = 999),
            ("unknown-solvent", root => Bodies(root)[0]!["Environment"]!["AvailableSolvent"] = 999),
        };
        foreach (var (name, change) in cases)
        {
            var malformed = (JsonObject)valid.DeepClone();
            change(malformed);
            File.WriteAllText(path, malformed.ToJsonString());
            var primaryBytes = File.ReadAllBytes(path);
            Reject(() => save.Load(path), "v16 load accepted " + name + " planetary catalog");
            Require(primaryBytes.SequenceEqual(File.ReadAllBytes(path)) && backupBytes.SequenceEqual(File.ReadAllBytes(path + ".bak")),
                "failed v16 load changed primary or backup for " + name);
        }

        var corruptPrimary = File.ReadAllBytes(path);
        var recovered = new CampaignSessionService().LoadExisting(path);
        Require(recovered.RecoveredFromBackup && recovered.Galaxy.PlanetaryBodies.SequenceEqual(galaxy.PlanetaryBodies),
            "explicit load did not recover the intact v16 catalog backup after malformed primary catalog");
        Require(corruptPrimary.SequenceEqual(File.ReadAllBytes(path)) && backupBytes.SequenceEqual(File.ReadAllBytes(path + ".bak")),
            "catalog backup recovery rewrote the malformed primary or intact backup");

        File.WriteAllText(path, valid.ToJsonString());
        var protectedPrimary = File.ReadAllBytes(path);
        var invalidGalaxy = CloneWithBodies(galaxy, galaxy.PlanetaryBodies.Concat(galaxy.PlanetaryBodies.Take(1)).ToArray());
        Reject(() => save.Save(path, invalidGalaxy, 3), "save accepted duplicate authoritative body IDs");
        Require(protectedPrimary.SequenceEqual(File.ReadAllBytes(path)) && backupBytes.SequenceEqual(File.ReadAllBytes(path + ".bak")),
            "failed catalog save changed primary or backup");
    }

    private static GalaxyState CloneWithBodies(GalaxyState source, PlanetaryBodyState[] bodies) => new()
    {
        DeveloperSession = source.DeveloperSession, GenerationMetadata = source.GenerationMetadata,
        GalacticCore = source.GalacticCore, Seed = source.Seed,
        Systems = source.Systems, PlanetaryBodies = bodies, Civilizations = source.Civilizations, Fleets = source.Fleets,
        Colonies = source.Colonies, Economies = source.Economies, Technologies = source.Technologies,
        ConstructionStates = source.ConstructionStates, ShipyardStates = source.ShipyardStates,
        PlayerCivilizationId = source.PlayerCivilizationId, Knowledge = source.Knowledge,
    };

    private static string Habitability(GalaxyState galaxy, string speciesId)
    {
        var evaluator = new SpeciesPlanetaryHabitabilityEvaluator();
        return string.Join('|', galaxy.PlanetaryBodies.OrderBy(body => body.Id).Select(body =>
            $"{body.Id}:{evaluator.Evaluate(body, speciesId).Viability}:{evaluator.Evaluate(body, speciesId).Environment.NaturalHabitability:R}"));
    }

    private static string Knowledge(GalaxyState galaxy, int civilizationId) => string.Join('|', galaxy.Systems
        .OrderBy(system => system.Id).Select(system =>
            $"{system.Id}:{galaxy.Knowledge.GetSystemSurveyLevel(civilizationId, system.Id)}:{galaxy.Knowledge.GetSystemSurveyProgress(civilizationId, system.Id):R}"));

    private static void Reject(Action action, string message)
    {
        try { action(); }
        catch (InvalidDataException) { return; }
        catch (JsonException) { return; }
        throw new InvalidOperationException(message);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
