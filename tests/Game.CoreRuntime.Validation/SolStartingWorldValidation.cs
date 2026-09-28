using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation.Exploration;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.CoreRuntime.Validation;

internal static class SolStartingWorldValidation
{
    public static void Run()
    {
        foreach (var seed in new long[] { 20260908, 0x1001, 0x2002, 0x3003, 0x4004, 0xC00C })
            FreshCampaignHasOneHumanOriginAndDistinctForeignHomes(seed);
        CanonicalPhysicalCatalogSurvivesConditioningAndDisplayRenames();
        PresetIdentityFollowsSurveyConfidence();
        CurrentAndLegacySavesKeepTheirOwnCatalogs();
    }

    private static void FreshCampaignHasOneHumanOriginAndDistinctForeignHomes(long seed)
    {
        var generator = new GalaxyGenerator();
        var galaxy = generator.Generate(seed);
        var repeat = generator.Generate(seed);
        var humans = galaxy.Civilizations.Where(c => c.SpeciesId == SpeciesCatalog.TerranBaselineId).ToArray();
        Require(humans.Length == 1 && humans[0].IsPlayer && !humans[0].IsSeededAncient &&
            humans[0].DevelopmentStage == CivilizationDevelopmentStage.PreWarp,
            $"seed {seed}: fresh human origin was repeated, non-player or ancient");
        Require(humans[0].HomeSystemId == SolCatalogPreset.SystemId, "human civilization did not originate in Sol");
        var earthColony = galaxy.Colonies.Single(c => c.CivilizationId == humans[0].Id &&
            c.PlanetaryBodyId == SolCatalogPreset.EarthBodyId);
        Require(earthColony.SystemId == SolCatalogPreset.SystemId && earthColony.PlanetaryBodyId == SolCatalogPreset.EarthBodyId &&
            earthColony.Name == "Earth" && earthColony.PopulationSpeciesId == SpeciesCatalog.TerranBaselineId,
            "human founding population was not anchored to Earth");
        var earth = galaxy.PlanetaryBodies.Single(body => body.Id == SolCatalogPreset.EarthBodyId);
        Require(new PlanetarySpeciesHabitabilityEvaluator().Evaluate(SpeciesCatalog.Get(humans[0].SpeciesId), earth).NaturallyColonizable,
            "Earth required manufactured habitat support for human founding");
        var humanSettlements = galaxy.Colonies.Where(c => c.CivilizationId == humans[0].Id).OrderBy(c => c.Id).ToArray();
        Require(galaxy.Civilizations.Count == 10 && galaxy.Colonies.Count == 12,
            "canonical origins or human Sol settlements were not seeded exactly once");
        Require(humanSettlements.Select(c => c.Name).SequenceEqual(new[] { "Earth", "Luna", "Mars" }) &&
            humanSettlements.Select(c => c.PlanetaryBodyId).SequenceEqual(new int?[] { 3, 9, 4 }) &&
            humanSettlements[1].PopulationMillions == 0.10 && humanSettlements[2].PopulationMillions == 0.25,
            "the 2050 human start lost its Earth, lunar or young Mars settlement identity");
        Require(galaxy.Civilizations.Select(c => c.HomeSystemId).Distinct().Count() == galaxy.Civilizations.Count &&
            galaxy.Colonies.Select(c => c.PlanetaryBodyId).Distinct().Count() == galaxy.Colonies.Count,
            "factions shared a starting system or founding body");
        foreach (var faction in galaxy.Civilizations.Where(c => !c.IsPlayer))
        {
            var home = galaxy.Systems.Single(system => system.Id == faction.HomeSystemId);
            Require(home.CatalogPresetId is null && !home.Name.StartsWith("SYS-", StringComparison.Ordinal),
                "nonhuman faction lost its distinct named procedural home");
            var colony = galaxy.Colonies.Single(c => c.CivilizationId == faction.Id);
            var body = galaxy.PlanetaryBodies.Single(b => b.Id == colony.PlanetaryBodyId);
            Require(new PlanetarySpeciesHabitabilityEvaluator().Evaluate(SpeciesCatalog.Get(faction.SpeciesId), body).NaturallyColonizable,
                "nonhuman founding home was incompatible with its physiology");
        }
        Require(galaxy.Systems.SequenceEqual(repeat.Systems) && galaxy.PlanetaryBodies.SequenceEqual(repeat.PlanetaryBodies) &&
            galaxy.Civilizations.SequenceEqual(repeat.Civilizations), "same seed changed starting catalog or faction homes");
        var economy = galaxy.Economies.Single(e => e.CivilizationId == humans[0].Id);
        Require(economy.Credits == 500 && economy.Industry == 200 && economy.Science == 0 &&
            !galaxy.Fleets.Any(f => f.CivilizationId == humans[0].Id) &&
            galaxy.Technologies.Single(t => t.CivilizationId == humans[0].Id).CompletedTechnologyIds.Count == 0,
            "Earth start bypassed normal economy, research or physical ship prerequisites");
        var openingFlow = EconomySimulation.GetCreditFlow(galaxy, humans[0].Id);
        Require(Math.Abs(openingFlow.ColonyAdministrationPerDay - 1.24) < 0.000001 &&
            openingFlow.HabitatSupportPerDay > 0 &&
            openingFlow.NetCreditsPerDay > 0,
            "dependent Luna/Mars administration either became free or stalled the opening economy");
        Require(!DemoObjectiveView.Build(galaxy, 1).Objective.Contains("complete", StringComparison.OrdinalIgnoreCase),
            "starting Luna/Mars settlements falsely completed the extrasolar campaign objective");
    }

    private static void CanonicalPhysicalCatalogSurvivesConditioningAndDisplayRenames()
    {
        var galaxy = new GalaxyGenerator().Generate(PlayableDemoScenario.Seed);
        var sol = galaxy.Systems.Single(SolCatalogPreset.IsSol);
        Require(sol.Id == 0 && sol.Name == "Sol" && sol.Position == System.Numerics.Vector2.Zero &&
            sol.Archetype == StarArchetype.Standard, "Sol identity, starting position or stellar class changed");
        var bodies = galaxy.PlanetaryBodies.Where(b => b.SystemId == sol.Id).ToArray();
        var planets = bodies.Where(b => b.Kind == PlanetaryBodyKind.Planet).OrderBy(b => b.OrbitIndex).ToArray();
        Require(planets.Select(b => b.Name).SequenceEqual(new[] { "Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune" }),
            "Sol was relabeled from random bodies or lost its eight ordered planets");
        Require(planets.Select(b => b.Id).SequenceEqual(Enumerable.Range(1, 8)), "canonical planet IDs changed");
        var moon = bodies.Single(b => b.Kind == PlanetaryBodyKind.Moon);
        Require(moon.Id == SolCatalogPreset.MoonBodyId && moon.ParentBodyId == SolCatalogPreset.EarthBodyId && moon.Name == "Moon",
            "Earth's Moon lost canonical identity or parentage");
        var pluto = bodies.Single(b => b.Kind == PlanetaryBodyKind.DwarfPlanet);
        Require(bodies.Length == 10 && pluto.Id == SolCatalogPreset.PlutoBodyId && pluto.ParentBodyId is null &&
            pluto.Name == "Pluto" && pluto.OrbitIndex == 8 &&
            Math.Abs(pluto.OrbitalEccentricity - SolCatalogPreset.PlutoOrbitalEccentricity) < 0.000001 &&
            Math.Abs(pluto.OrbitalInclinationDegrees - SolCatalogPreset.PlutoOrbitalInclinationDegrees) < 0.000001 &&
            pluto.Environment.HasSolidSurface && !pluto.LegacyColonizationCandidate,
            "Pluto lost its dwarf-planet identity or distinctive orbital data");
        var earth = planets[2];
        Require(earth.Environment.Atmosphere == PlanetaryAtmosphereRegime.OxygenNitrogen &&
            earth.Environment.AvailableSolvent == PlanetarySolventRegime.Water && earth.Environment.HasSolidSurface &&
            !earth.Environment.IsImmersedEnvironment, "Earth was not a terrestrial land-and-water human environment");
        Require(planets[0].Environment.Atmosphere == PlanetaryAtmosphereRegime.Vacuum &&
            planets[1].Environment.Atmosphere == PlanetaryAtmosphereRegime.CarbonDioxideRich &&
            planets[1].Environment.TemperatureKelvin > 700 && planets[1].Environment.PressureKPa > 9000,
            "Mercury or Venus received procedural habitable-world conditions");
        Require(planets.Skip(4).All(body => !body.Environment.HasSolidSurface && !body.LegacyColonizationCandidate),
            "a gas or ice giant became a solid habitable Earth substitute");
        Require(bodies.SequenceEqual(new PlanetaryEnvironmentalDiversityPolicy().Apply(galaxy.Seed, new[] { sol }, bodies)),
            "diversity conditioning rewrote canonical solar bodies");
        Require(bodies.SequenceEqual(new PlanetaryBodyGenerator().Generate(galaxy.Seed, new[] { sol with { Name = "Display rename" } })),
            "physical catalog identity depended on its display label");
    }

    private static void PresetIdentityFollowsSurveyConfidence()
    {
        var galaxy = new GalaxyGenerator().Generate(PlayableDemoScenario.Seed);
        var observer = galaxy.Civilizations.First(c => !c.IsPlayer).Id;
        galaxy.Knowledge.RevealSystem(observer, SolCatalogPreset.SystemId);
        var detected = new ExplorationReadModel().Build(galaxy, observer).KnownSystems
            .Single(s => s.SystemId == SolCatalogPreset.SystemId);
        Require(detected.PlanetaryBodies.Count == 0,
            "detection exposed Pluto or another undiscovered orbital-catalog body");
        galaxy.Knowledge.RecordReconnaissance(observer, SolCatalogPreset.SystemId, 0.45);
        var read = new ExplorationReadModel();
        var partial = read.Build(galaxy, observer).KnownSystems.Single(s => s.SystemId == SolCatalogPreset.SystemId);
        Require(partial.CatalogPresetId is null && partial.PlanetaryBodies.All(body => !body.HasDetailedEnvironment),
            "reconnaissance exposed the canonical surface/material key");
        var partialPluto = partial.PlanetaryBodies.Single(body => body.BodyId == SolCatalogPreset.PlutoBodyId);
        Require(partialPluto.Kind == PlanetaryBodyKind.DwarfPlanet &&
            partialPluto.OrbitalEccentricity == SolCatalogPreset.PlutoOrbitalEccentricity &&
            partialPluto.OrbitalInclinationDegrees == SolCatalogPreset.PlutoOrbitalInclinationDegrees,
            "reconnaissance orbital catalog lost Pluto's safe classification or orbit");
        galaxy.Knowledge.MarkSystemFullySurveyed(observer, SolCatalogPreset.SystemId);
        var full = read.Build(galaxy, observer).KnownSystems.Single(s => s.SystemId == SolCatalogPreset.SystemId);
        Require(full.CatalogPresetId == SolCatalogPreset.PresetId && full.PlanetaryBodies.All(body => body.HasDetailedEnvironment),
            "completed survey lost the legitimate canonical solar identity");
    }

    private static void CurrentAndLegacySavesKeepTheirOwnCatalogs()
    {
        var sessions = new CampaignSessionService();
        var demo = PlayableDemoScenario.Create(sessions);
        var directory = Path.Combine(Path.GetTempPath(), $"stellar-sol-continuity-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            var currentPath = Path.Combine(directory, "sol.json");
            sessions.Save(currentPath, demo.Galaxy, demo.Diplomacy, 123);
            Require(JsonDocument.Parse(File.ReadAllText(currentPath)).RootElement.GetProperty("FormatVersion").GetInt32() == 17,
                "new Sol campaign could be silently misread by a legacy v9 binary");
            var loaded = sessions.LoadOrCreate(currentPath, 999);
            Require(loaded.WasLoaded && loaded.SimulationDays == 123 &&
                loaded.Galaxy.Systems.SequenceEqual(demo.Galaxy.Systems) &&
                loaded.Galaxy.PlanetaryBodies.SequenceEqual(demo.Galaxy.PlanetaryBodies),
                "save/resume did not reconstruct the exact canonical and procedural catalog");
            var loadedHumanBodies = loaded.Galaxy.Colonies.Where(c => c.CivilizationId == loaded.Galaxy.PlayerCivilizationId)
                .Select(c => c.PlanetaryBodyId).OrderBy(id => id).ToArray();
            Require(loadedHumanBodies.SequenceEqual(new int?[] { 3, 4, 9 }),
                "Earth, Luna or Mars starting settlements moved during resume");

            var prePlutoPath = Path.Combine(directory, "sol-pre-pluto.json");
            var prePluto = JsonNode.Parse(File.ReadAllText(currentPath))!.AsObject();
            var savedBodies = prePluto["Galaxy"]!["PlanetaryBodies"]!.AsArray();
            var plutoNode = savedBodies.Single(node => node!["Id"]!.GetValue<int>() == SolCatalogPreset.PlutoBodyId);
            savedBodies.Remove(plutoNode);
            File.WriteAllText(prePlutoPath, prePluto.ToJsonString());
            var prePlutoBytes = File.ReadAllBytes(prePlutoPath);
            var upgraded = sessions.LoadExisting(prePlutoPath);
            Require(prePlutoBytes.SequenceEqual(File.ReadAllBytes(prePlutoPath)),
                "loading an older canonical Sol catalog rewrote the save file");
            Require(upgraded.Galaxy.PlanetaryBodies.Where(body => body.Id != SolCatalogPreset.PlutoBodyId)
                    .SequenceEqual(demo.Galaxy.PlanetaryBodies.Where(body => body.Id != SolCatalogPreset.PlutoBodyId)) &&
                upgraded.Galaxy.PlanetaryBodies.Single(body => body.Id == SolCatalogPreset.PlutoBodyId) ==
                    demo.Galaxy.PlanetaryBodies.Single(body => body.Id == SolCatalogPreset.PlutoBodyId),
                "older canonical Sol save did not receive only the additive Pluto catalog entry");
            var upgradedPath = Path.Combine(directory, "sol-upgraded.json");
            sessions.Save(upgradedPath, upgraded.Galaxy, upgraded.Diplomacy, upgraded.AdaptiveResearch, upgraded.SimulationDays);
            Require(sessions.LoadExisting(upgradedPath).Galaxy.PlanetaryBodies.SequenceEqual(upgraded.Galaxy.PlanetaryBodies),
                "resaving an upgraded canonical Sol catalog did not persist Pluto exactly");

            var fixture = FindLegacyFixture();
            var legacyPath = Path.Combine(directory, "legacy.json");
            File.Copy(fixture, legacyPath);
            var legacy = sessions.LoadOrCreate(legacyPath, 999);
            Require(legacy.WasLoaded && legacy.Galaxy.Systems.All(s => s.CatalogPresetId is null) &&
                legacy.Galaxy.Systems.All(s => s.Name.StartsWith("SYS-", StringComparison.Ordinal)),
                "legacy procedural save was converted into a fresh Sol start");
            var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(legacy.Galaxy.PlanetaryBodies))));
            Require(hash == "B720C6F22154F193B1F85E995E5AE52BF1B3707AA10504CEF3AAF65056CA66A9",
                "legacy save reconstructed different physical bodies than its pre-Sol generator");
            Require(legacy.Galaxy.Civilizations.Count(c => c.SpeciesId == SpeciesCatalog.TerranBaselineId) == 3,
                "legacy repeated human physiology was reassigned by the new founding policy");
            var oldCampaignPath = Path.Combine(directory, "legacy-campaign.json");
            sessions.Save(oldCampaignPath, legacy.Galaxy, legacy.Diplomacy, legacy.SimulationDays);
            var legacyCampaignDocument = JsonDocument.Parse(File.ReadAllText(oldCampaignPath));
            Require(legacyCampaignDocument.RootElement.GetProperty("FormatVersion").GetInt32() == 17 &&
                    legacyCampaignDocument.RootElement.GetProperty("GalaxyFormatVersion").GetInt32() == 16 &&
                    legacyCampaignDocument.RootElement.GetProperty("Galaxy").TryGetProperty("PlanetaryBodies", out _),
                "resaving a procedural campaign did not capture its exact reconstructed catalog in v17/v16");
            var oldCampaign = sessions.LoadOrCreate(oldCampaignPath, 999);
            Require(oldCampaign.WasLoaded && oldCampaign.Galaxy.PlanetaryBodies.SequenceEqual(legacy.Galaxy.PlanetaryBodies),
                "legacy v9 campaign failed to retain its original procedural catalog");
        }
        finally
        {
            var temp = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            Require(string.Equals(Path.GetDirectoryName(Path.GetFullPath(directory)), temp, StringComparison.OrdinalIgnoreCase),
                "refusing test cleanup outside the named temporary directory");
            Directory.Delete(directory, recursive: true);
        }
    }

    private static string FindLegacyFixture()
    {
        for (var directory = new DirectoryInfo(Directory.GetCurrentDirectory()); directory is not null; directory = directory.Parent)
        {
            var path = Path.Combine(directory.FullName, "tests", "fixtures", "legacy-procedural-v8.json");
            if (File.Exists(path)) return path;
        }
        throw new FileNotFoundException("Run Sol validation from the repository or project directory: legacy fixture was not found.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
