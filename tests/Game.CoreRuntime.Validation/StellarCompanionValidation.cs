using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Presentation.Spatial;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.CoreRuntime.Validation;

internal static class StellarCompanionValidation
{
    [Game.Validation.RegressionCheck]
    internal static void CatalogPersistenceAndVisibility()
    {
        const long seed = 20260908;
        var galaxy = new GalaxyGenerator().Generate(seed);
        var repeated = new GalaxyGenerator().Generate(seed);
        Require(galaxy.Systems.SequenceEqual(repeated.Systems), "same seed changed stellar configuration");
        Require(galaxy.Systems.Any(s => s.SecondaryStellarClass.HasValue && !s.TertiaryStellarClass.HasValue) &&
                galaxy.Systems.Any(s => s.TertiaryStellarClass.HasValue), "catalog is missing binary or triple systems");
        var sol = galaxy.Systems.Single(s => s.CatalogPresetId == SolCatalogPreset.PresetId);
        Require(sol.StellarClass == StellarPrimaryClass.GYellowDwarf &&
                !sol.SecondaryStellarClass.HasValue && !sol.TertiaryStellarClass.HasValue, "Sol gained companions");

        var singles = galaxy.Systems.Select(s => s with { SecondaryStellarClass = null, TertiaryStellarClass = null }).ToArray();
        var rebuilt = singles.ToList();
        StellarCompanionGenerator.Apply(seed, rebuilt);
        Require(rebuilt.SequenceEqual(galaxy.Systems), "companion generation changed IDs, positions, names or primary facts");
        Require(new PlanetaryBodyGenerator().Generate(seed, singles)
                .SequenceEqual(new PlanetaryBodyGenerator().Generate(seed, galaxy.Systems)),
            "stellar catalog decoration altered existing planet or moon facts");
        var alternate = singles.ToList();
        StellarCompanionGenerator.Apply(seed + 1, alternate);
        Require(!alternate.SequenceEqual(rebuilt), "different seeds produced identical companion catalog");

        var triple = galaxy.Systems.First(s => s.TertiaryStellarClass.HasValue &&
            !galaxy.Knowledge.IsSystemFullySurveyed(galaxy.PlayerCivilizationId, s.Id));
        var observer = galaxy.PlayerCivilizationId;
        galaxy.Knowledge.RecordReconnaissance(observer, triple.Id);
        var reconnaissance = new ExplorationReadModel().Build(galaxy, observer).KnownSystems.Single(s => s.SystemId == triple.Id);
        Require(reconnaissance.StellarClass is null && reconnaissance.SecondaryStellarClass is null &&
                reconnaissance.TertiaryStellarClass is null, "reconnaissance exposed detailed stellar facts");
        galaxy.Knowledge.MarkSystemFullySurveyed(observer, triple.Id);
        var detailed = new ExplorationReadModel().Build(galaxy, observer).KnownSystems.Single(s => s.SystemId == triple.Id);
        var spatial = new SystemSpatialProjection().Build(detailed);
        Require(spatial.StellarClass == triple.StellarClass && spatial.SecondaryStellarClass == triple.SecondaryStellarClass &&
                spatial.TertiaryStellarClass == triple.TertiaryStellarClass, "fully surveyed spatial view lost companion stars");

        var directory = Path.Combine(Path.GetTempPath(), "stellar-companion-validation-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            var path = Path.Combine(directory, "catalog.json");
            var persistence = new CampaignSaveService();
            persistence.Save(path, galaxy, 12.5);
            var loaded = persistence.Load(path);
            Require(loaded.Galaxy.Systems.SequenceEqual(galaxy.Systems), "save/load lost or regenerated companions");
            var source = File.ReadAllText(path);
            var old = JsonNode.Parse(source)!;
            foreach (var system in old["Galaxy"]!["Systems"]!.AsArray())
            {
                system!.AsObject().Remove("SecondaryStellarClass");
                system.AsObject().Remove("TertiaryStellarClass");
            }
            var oldPath = Path.Combine(directory, "legacy-absent.json");
            File.WriteAllText(oldPath, old.ToJsonString());
            var legacy = persistence.Load(oldPath);
            Require(legacy.Galaxy.Systems.SequenceEqual(singles), "legacy load invented companions or changed existing system facts");
            Require(legacy.Galaxy.PlanetaryBodies.SequenceEqual(galaxy.PlanetaryBodies), "legacy companion default changed planets");

            Reject("invalid-class", node => node["SecondaryStellarClass"] = 999);
            Reject("missing-secondary", node => { node.AsObject().Remove("SecondaryStellarClass"); node["TertiaryStellarClass"] = 0; });
            Reject("missing-primary", node => node.AsObject().Remove("StellarClass"));
            var badSol = JsonNode.Parse(source)!;
            badSol["Galaxy"]!["Systems"]!.AsArray().Single(s => s!["Id"]!.GetValue<int>() == sol.Id)!["SecondaryStellarClass"] = 0;
            RejectDocument("canonical-sol", badSol);

            void Reject(string name, Action<JsonNode> mutate)
            {
                var document = JsonNode.Parse(source)!;
                var entry = document["Galaxy"]!["Systems"]!.AsArray().Single(s => s!["Id"]!.GetValue<int>() == triple.Id)!;
                mutate(entry);
                RejectDocument(name, document);
            }
            void RejectDocument(string name, JsonNode document)
            {
                var badPath = Path.Combine(directory, name + ".json");
                var json = document.ToJsonString();
                File.WriteAllText(badPath, json);
                try { persistence.Load(badPath); }
                catch (InvalidDataException error)
                {
                    Require(error.Message.Contains("System", StringComparison.Ordinal), "invalid star diagnostic omitted the system");
                    Require(File.ReadAllText(badPath) == json, "rejected load modified source save");
                    return;
                }
                throw new InvalidOperationException("malformed stellar catalog was accepted: " + name);
            }
        }
        finally { Directory.Delete(directory, recursive: true); }
        Console.WriteLine("PASS: deterministic companion stars preserve planets, old saves and observer gates; malformed stars fail cleanly");
    }

    private static void Require(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
}
