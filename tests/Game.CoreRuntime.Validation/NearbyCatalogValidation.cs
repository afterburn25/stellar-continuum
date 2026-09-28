using System.Diagnostics;
using Game.Campaign;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.CoreRuntime.Validation;

internal static class NearbyCatalogValidation
{
    internal static void Run()
    {
        var sessions = new CampaignSessionService();
        var stopwatch = Stopwatch.StartNew();
        var first = sessions.CreateNearbyCatalog("NEARBY-CATALOG-VALIDATION-A");
        var second = sessions.CreateNearbyCatalog("NEARBY-CATALOG-VALIDATION-B");
        stopwatch.Stop();

        var galaxy = first.Galaxy;
        Require(galaxy.Systems.Count == NearbyStarCatalog.SystemCount && galaxy.Systems.Count == 500,
            "string campaign creation did not use the 500-system nearby-star catalogue");
        Require(stopwatch.Elapsed < TimeSpan.FromSeconds(30),
            $"two bounded 500-system catalog launches exceeded the 30-second validation budget ({stopwatch.Elapsed.TotalSeconds:0.##}s)");
        Require(galaxy.GalacticCore is null && galaxy.GenerationMetadata?.GalacticCore is null &&
                galaxy.Systems.All(system => system.Archetype != StarArchetype.BlackHole),
            "the local nearby-star profile introduced a galactic-centre black-hole landmark");

        var catalogById = NearbyStarCatalog.Stars.ToDictionary(star => $"hyg-v41:{star.HygId}");
        Require(galaxy.Systems.All(system => system.StellarCatalogId is not null) &&
                galaxy.Systems.Select(system => system.StellarCatalogId).Distinct().Count() == 500 &&
                galaxy.Systems.Select(system => system.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count() == 500,
            "nearby campaign did not retain unique HYG identities and catalogue names");
        Require(galaxy.Systems.All(system => system.StellarCatalogId is { } id && catalogById.ContainsKey(id)),
            "nearby campaign contains a stellar identity absent from the bundled catalogue");

        var sol = SystemByName(galaxy, "Sol");
        Require(sol.CatalogPresetId == SolCatalogPreset.PresetId && sol.Position == System.Numerics.Vector2.Zero &&
                sol.GalacticDepthLightYears == 0.0,
            "catalogue campaign did not retain the canonical Sol origin");
        RequireDistance(sol, SystemByName(galaxy, "Proxima Centauri"), 4.23, "Proxima Centauri");
        RequireDistance(sol, SystemByName(galaxy, "Rigil Kentaurus"), 4.32, "Rigil Kentaurus");
        RequireDistance(sol, SystemByName(galaxy, "Barnard's Star"), 5.95, "Barnard's Star");
        RequireDistance(sol, SystemByName(galaxy, "Sirius"), 8.60, "Sirius");

        var proxima = SystemByName(galaxy, "Proxima Centauri");
        var physicalProximaDistance = InterstellarDistance.Between(sol, proxima);
        var flatProximaDistance = System.Numerics.Vector2.Distance(sol.Position, proxima.Position);
        Require(Math.Abs(physicalProximaDistance - flatProximaDistance) > 0.02,
            "catalogue depth did not change a true physical pair distance from its flat chart projection");
        var sirius = SystemByName(galaxy, "Sirius");
        Require(sirius.StellarClass == StellarPrimaryClass.AWhiteStar &&
                sirius.SecondaryStellarClass == StellarPrimaryClass.WhiteDwarf,
            "Sirius A/B spectral classes were not represented as A-white-star plus white dwarf");

        Require(galaxy.Systems.Select(system => (system.StellarCatalogId, system.Name, system.Position, system.GalacticDepthLightYears))
                    .SequenceEqual(second.Galaxy.Systems.Select(system =>
                        (system.StellarCatalogId, system.Name, system.Position, system.GalacticDepthLightYears))),
            "different string seeds changed fixed catalogue identities or star coordinates");
        Require(!galaxy.PlanetaryBodies.SequenceEqual(second.Galaxy.PlanetaryBodies),
            "different string seeds did not change seeded planetary content around the fixed star catalogue");

        var humans = galaxy.Civilizations.Where(civilization => civilization.SpeciesId == SpeciesCatalog.TerranBaselineId).ToArray();
        Require(humans.Length == 1 && humans[0].HomeSystemId == sol.Id &&
                galaxy.Colonies.Any(colony => colony.CivilizationId == humans[0].Id && colony.Name == "Earth"),
            "Humanity was not founded on Earth in canonical Sol");
        foreach (var civilization in galaxy.Civilizations.Where(civilization => civilization.Id != humans[0].Id))
        {
            var home = galaxy.Systems.Single(system => system.Id == civilization.HomeSystemId);
            Require(home.StellarCatalogId is { } id && catalogById[id].Name == home.Name,
                $"nonhuman home {home.Name} lost its catalogue identity");
        }

        var lanes = new InterstellarLaneNetwork().Build(galaxy.Systems);
        Require(lanes.Count >= 499 && lanes.All(lane => double.IsFinite(lane.LengthLightYears) && lane.LengthLightYears > 0.0) &&
                lanes.Select(lane => Math.Round(lane.LengthLightYears, 3)).Distinct().Count() > 10,
            "nearby catalogue lanes are not finite, positive, connected-backbone geometry with variable lengths");
        var nearestToSol = galaxy.Systems.Where(system => system.Id != sol.Id)
            .OrderBy(system => InterstellarDistance.Between(sol, system)).ThenBy(system => system.Id).First();
        var laneNetwork = new InterstellarLaneNetwork();
        Require(lanes.Any(lane => lane.Connects(sol.Id) && lane.Other(sol.Id) == nearestToSol.Id) &&
                galaxy.Systems.All(system => laneNetwork.FindShortestRoute(galaxy.Systems, sol.Id, system.Id).Count > 0),
            "lane graph omitted Sol's nearest known neighbor or disconnected a catalogue system");
        var knownSystems = galaxy.Knowledge.GetKnownSystems(galaxy.PlayerCivilizationId);
        Require(knownSystems.Contains(sol.Id) && knownSystems.Count < galaxy.Systems.Count,
            "initial nearby survey revealed the whole 500-system catalogue");

        SaveLoadPreservesCatalogueAndLegacySessions(sessions, first);
        ValidateReproducibleOpeningGuarantees(sessions);
    }

    private static void SaveLoadPreservesCatalogueAndLegacySessions(
        CampaignSessionService sessions,
        CampaignBootstrapResult first)
    {
        var directory = Path.Combine(Path.GetTempPath(), "stellar-nearby-catalog-validation-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            var nearbyPath = Path.Combine(directory, "nearby-500.json");
            sessions.Save(nearbyPath, first.Galaxy, first.Diplomacy, first.AdaptiveResearch, 0.0);
            var restored = sessions.LoadExisting(nearbyPath).Galaxy;
            Require(restored.Systems.Count == 500 && restored.PlanetaryBodies.SequenceEqual(first.Galaxy.PlanetaryBodies) &&
                    restored.Systems.Select(system => (system.StellarCatalogId, system.Name, system.Position, system.GalacticDepthLightYears))
                        .SequenceEqual(first.Galaxy.Systems.Select(system =>
                            (system.StellarCatalogId, system.Name, system.Position, system.GalacticDepthLightYears))),
                "save/load changed 500-system catalogue identities, positions, depth, names, or planets");

            var legacy = sessions.CreateNew(100_500L);
            var before = legacy.Galaxy.Systems.Select(system =>
                (system.Id, system.Name, system.Position, system.GalacticDepthLightYears, system.StellarCatalogId)).ToArray();
            var legacyPath = Path.Combine(directory, "legacy-100.json");
            sessions.Save(legacyPath, legacy.Galaxy, legacy.Diplomacy, legacy.AdaptiveResearch, 0.0);
            var reloadedLegacy = sessions.LoadExisting(legacyPath).Galaxy;
            Require(legacy.Galaxy.Systems.Count == 100 && reloadedLegacy.Systems.Count == 100 &&
                    before.SequenceEqual(reloadedLegacy.Systems.Select(system =>
                        (system.Id, system.Name, system.Position, system.GalacticDepthLightYears, system.StellarCatalogId))),
                "legacy 100-system campaign changed after nearby catalogue work or its save/load round trip");
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    private static void ValidateReproducibleOpeningGuarantees(CampaignSessionService sessions)
    {
        // Native startup regression: an unknown spectral class can legitimately have no planets.
        var seeds = new[] { "LOCAL-OPENING-01", "LOCAL-OPENING-02", "LOCAL-OPENING-03", "LOCAL-OPENING-04", "LOCAL-OPENING-05", "1789190122096" };
        var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
        foreach (var seed in seeds)
        foreach (var species in SpeciesCatalog.All)
        {
            var galaxy = sessions.CreateNearbyCatalog(seed, species.Id).Galaxy;
            var occupiedHomes = galaxy.Civilizations.Select(civilization => civilization.HomeSystemId).ToHashSet();
            foreach (var civilization in galaxy.Civilizations.Where(civilization => !civilization.IsSeededAncient))
            {
                var home = galaxy.Systems.Single(system => system.Id == civilization.HomeSystemId);
                var expansionSystems = galaxy.Systems.Where(system => !occupiedHomes.Contains(system.Id) &&
                        InterstellarDistance.Between(home, system) <= NearbyHabitableWorldGuaranteePolicy.MaximumOpeningDistance)
                    .Count(system => galaxy.PlanetaryBodies.Any(body => body.SystemId == system.Id &&
                        habitability.Evaluate(body, civilization.SpeciesId).Viability == SpeciesColonizationViability.NaturallyViable));
                Require(expansionSystems >= 2,
                    $"nearby opening guarantee failed for reproducible seed {seed}, player species {species.Id}, civilization {civilization.Id}: {expansionSystems} viable worlds");
            }
        }
    }

    private static StarSystemState SystemByName(GalaxyState galaxy, string name) =>
        galaxy.Systems.Single(system => system.Name == name);

    private static void RequireDistance(StarSystemState origin, StarSystemState target, double expectedLightYears, string name)
    {
        var actual = InterstellarDistance.Between(origin, target);
        Require(Math.Abs(actual - expectedLightYears) <= 0.02,
            $"{name} physical distance changed: expected about {expectedLightYears:0.00} ly, got {actual:0.000} ly");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
