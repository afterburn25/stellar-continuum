using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using Game.Campaign;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;
using Game.Simulation.Exploration;

namespace Game.CoreRuntime.Validation;

internal static class SandboxGenerationSetupValidation
{
    internal static void Run()
    {
        Require(CampaignSeed.Parse(long.MinValue.ToString(CultureInfo.InvariantCulture)) == long.MinValue,
            "minimum legacy numeric seed changed");
        Require(CampaignSeed.Parse(long.MaxValue.ToString(CultureInfo.InvariantCulture)) == long.MaxValue,
            "maximum legacy numeric seed changed");
        Require(CampaignSeed.Parse("  Sol-Ascendant-42  ") == CampaignSeed.Parse("sol-ascendant-42"),
            "text seed normalization is not stable across case and surrounding whitespace");
        Require(CampaignSeed.Parse("SOL-ASCENDANT-42") != CampaignSeed.Parse("SOL-ASCENDANT-43"),
            "distinct text seeds resolved to the same test seed");
        Require(long.TryParse(CampaignSeed.CreateRandomNumericText(), NumberStyles.AllowLeadingSign,
            CultureInfo.InvariantCulture, out _), "random seed was not a valid portable numeric seed");

        const string enteredSeed = "SOL-ASCENDANT-42";
        var sessions = new CampaignSessionService();
        // Preserve the historical 100-system generator contract alongside the new catalogue
        // profile, whose default bootstrap is exercised in NearbyCatalogValidation.
        var first = CreateLegacySandbox(enteredSeed);
        var second = CreateLegacySandbox(enteredSeed);
        var metadata = first.Galaxy.GenerationMetadata
            ?? throw new InvalidOperationException("new Sandbox omitted generation metadata");
        Require(metadata.EnteredSeed == enteredSeed && metadata.InternalSeed == first.Seed,
            "Sandbox did not retain both entered and internal seeds");
        Require(metadata.GeneratorVersion == GalaxyGenerationMetadata.CurrentGeneratorVersion &&
            metadata.SystemCount == 100 && metadata.OtherCivilizations == 5 &&
            metadata.AncientCivilizations == "Rare" && metadata.GalaxyShape == "Barred spiral" &&
            metadata.ArtProfileVersion == "milky-way-barred-v1",
            "recommended 100-system setup metadata changed");
        Require(first.Galaxy.Systems.Count == 100 && first.Galaxy.Civilizations.Count == 7,
            "recommended Sandbox did not create the expected player, ordinary and ancient civilizations");
        var core = first.Galaxy.GalacticCore
            ?? throw new InvalidOperationException("new Sandbox omitted its galactic-core landmark");
        Require(core.LandmarkKey == GalacticCoreMetadata.StableLandmarkKey && core.ExclusionRadius > 0 &&
            Math.Abs(core.X + GalaxySpatialLayout.SolOffset(900).X) < .001f &&
            Math.Abs(core.Y + GalaxySpatialLayout.SolOffset(900).Y) < .001f,
            "Sandbox core landmark did not retain its stable centre coordinates");
        Require(first.Galaxy.Systems.Count(system => Vector2.DistanceSquared(system.Position,
                new Vector2(core.X, core.Y)) < core.ExclusionRadius * core.ExclusionRadius) == 0,
            "an ordinary Sandbox system was placed in the galactic-core exclusion region");
        Require(first.Galaxy.GenerationMetadata?.GalacticCore == core,
            "Sandbox generation metadata did not persist the exact core landmark");
        var coreObserver = first.Galaxy.PlayerCivilizationId;
        var otherObserver = first.Galaxy.Civilizations.First(civilization => civilization.Id != coreObserver).Id;
        Require(!first.Galaxy.Knowledge.HasGalacticCoreAccess(coreObserver) &&
                !first.Galaxy.Knowledge.IsGalacticCoreDiscovered(coreObserver),
            "a fresh observer began with the secret galactic core disclosed");
        Require(!first.Galaxy.Knowledge.RecordGalacticCoreExploration(coreObserver) &&
                !first.Galaxy.Knowledge.IsGalacticCoreDiscovered(coreObserver),
            "galactic-core exploration bypassed its authoritative access unlock");
        first.Galaxy.Knowledge.UnlockGalacticCoreAccess(coreObserver);
        Require(first.Galaxy.Knowledge.HasGalacticCoreAccess(coreObserver) &&
                !first.Galaxy.Knowledge.IsGalacticCoreDiscovered(coreObserver),
            "access unlock alone disclosed the unexplored galactic core");
        Require(first.Galaxy.Knowledge.RecordGalacticCoreExploration(coreObserver) &&
                first.Galaxy.Knowledge.IsGalacticCoreDiscovered(coreObserver) &&
                !first.Galaxy.Knowledge.HasGalacticCoreAccess(otherObserver) &&
                !first.Galaxy.Knowledge.IsGalacticCoreDiscovered(otherObserver),
            "galactic-core discovery was not observer-specific");
        foreach (var selectedSpeciesId in new[]
                 {
                     SpeciesCatalog.PelagicHighPressureId,
                     SpeciesCatalog.CompactHighGravityId,
                     SpeciesCatalog.CryogenicHydrocarbonId,
                 })
        {
            var nonhuman = sessions.CreateNew(enteredSeed, selectedSpeciesId);
            var player = nonhuman.Galaxy.Civilizations.Single(civilization => civilization.IsPlayer);
            var humans = nonhuman.Galaxy.Civilizations.Single(civilization =>
                civilization.SpeciesId == SpeciesCatalog.TerranBaselineId);
            Require(player.SpeciesId == selectedSpeciesId && player.HomeSystemId != SolCatalogPreset.SystemId,
                $"selected species {selectedSpeciesId} did not begin on its own homeworld");
            Require(humans.HomeSystemId == SolCatalogPreset.SystemId,
                $"Humanity left Earth/Sol when {selectedSpeciesId} became the Player species");
            Require(nonhuman.Galaxy.GenerationMetadata?.PlayerSpeciesId == selectedSpeciesId,
                $"selected species {selectedSpeciesId} was omitted from generation metadata");
        }
        Require(first.Galaxy.Systems.Select(system => system.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count() == 100 &&
            first.Galaxy.Systems.All(system => !system.Name.StartsWith("SYS-", StringComparison.OrdinalIgnoreCase)),
            "generated Sandbox retained placeholder or duplicate system names");
        foreach (var system in first.Galaxy.Systems.Where(system => system.CatalogPresetId is null))
        {
            var planets = first.Galaxy.PlanetaryBodies.Where(body => body.SystemId == system.Id &&
                body.Kind == PlanetaryBodyKind.Planet).ToArray();
            Require(planets.Select(body => body.Name).Distinct(StringComparer.OrdinalIgnoreCase).Count() == planets.Length &&
                planets.All(body => !body.Name.StartsWith(system.Name + " ", StringComparison.OrdinalIgnoreCase)),
                $"system {system.Name} retained orbital placeholders or duplicate planet names");
            foreach (var moon in first.Galaxy.PlanetaryBodies.Where(body => body.SystemId == system.Id &&
                         body.Kind == PlanetaryBodyKind.Moon))
            {
                var parent = planets.Single(planet => planet.Id == moon.ParentBodyId);
                Require(moon.Name.StartsWith(parent.Name + " ", StringComparison.Ordinal),
                    $"moon {moon.Name} does not retain its named parent relationship");
            }
        }
        var stellarCounts = first.Galaxy.Systems.GroupBy(system => system.StellarClass)
            .ToDictionary(group => group.Key, group => group.Count());
        Require(stellarCounts[StellarPrimaryClass.MRedDwarf] == 48 &&
            stellarCounts[StellarPrimaryClass.KOrangeDwarf] == 20 &&
            stellarCounts[StellarPrimaryClass.GYellowDwarf] == 11 &&
            stellarCounts[StellarPrimaryClass.FYellowWhiteDwarf] == 6 &&
            stellarCounts[StellarPrimaryClass.AWhiteStar] == 3 &&
            stellarCounts[StellarPrimaryClass.HotBlueStar] == 1 &&
            stellarCounts[StellarPrimaryClass.Giant] == 4 &&
            stellarCounts[StellarPrimaryClass.WhiteDwarf] == 3 &&
            stellarCounts[StellarPrimaryClass.NeutronStar] == 2 &&
            stellarCounts[StellarPrimaryClass.BlackHole] == 1 &&
            stellarCounts[StellarPrimaryClass.Protostar] == 1,
            "balanced physical stellar deck does not total the agreed 100-system quotas");
        Require(first.Galaxy.Systems.Single(system => system.CatalogPresetId == SolCatalogPreset.PresetId).StellarClass ==
            StellarPrimaryClass.GYellowDwarf, "authored Sol was not retained as a G-type star");
        Require(first.Galaxy.Systems.Select(system => (system.Name, system.Position, system.Archetype))
            .SequenceEqual(second.Galaxy.Systems.Select(system => (system.Name, system.Position, system.Archetype))),
            "same text seed and setup did not reproduce system names, positions and star types");
        var planetCounts = first.Galaxy.Systems.Select(system => first.Galaxy.PlanetaryBodies.Count(body =>
            body.SystemId == system.Id && body.Kind == PlanetaryBodyKind.Planet)).ToArray();
        Require(planetCounts.Count(count => count == 0) == 18 &&
            planetCounts.Count(count => count is >= 1 and <= 2) == 22 &&
            planetCounts.Count(count => count is >= 3 and <= 6) == 42 &&
            planetCounts.Count(count => count is >= 7 and <= 10) == 14 &&
            planetCounts.Count(count => count is >= 11 and <= 14) == 4,
            "balanced planetary architecture deck does not match the agreed 100-system profile");
        Require(first.Galaxy.PlanetaryBodies.Any(body => body.Kind == PlanetaryBodyKind.Planet &&
                !first.Galaxy.PlanetaryBodies.Any(moon => moon.ParentBodyId == body.Id)) &&
            first.Galaxy.PlanetaryBodies.Any(body => body.Kind == PlanetaryBodyKind.Planet &&
                first.Galaxy.PlanetaryBodies.Count(moon => moon.ParentBodyId == body.Id) > 1),
            "balanced catalog lacks both moonless and multi-moon planets");
        var nonSol = first.Galaxy.Systems.Where(system => system.CatalogPresetId is null).ToArray();
        var armCoverage = new int[4];
        var outerCoverage = new int[4];
        foreach (var star in nonSol)
        {
            var centered = star.Position + GalaxySpatialLayout.SolOffset(900);
            var x = centered.X / 900.0;
            var y = centered.Y / (900.0 * .72);
            var radius = Math.Sqrt(x * x + y * y);
            if (radius < .50) continue;
            var phase = (Math.Atan2(y, x) - radius * Math.PI * 2.35) / (Math.PI * .5);
            var arm = ((int)Math.Round(phase) % 4 + 4) % 4;
            armCoverage[arm]++;
            if (radius > .80) outerCoverage[arm]++;
        }
        Require(armCoverage.All(count => count >= 5) && outerCoverage.All(count => count >= 1),
            "playable systems do not populate all four spiral arms through their outer regions");
        Require(nonSol.Min(system => system.Position.X) < -700 &&
            nonSol.Max(system => system.Position.X) > 350 &&
            nonSol.Min(system => system.Position.Y) < -450 &&
            nonSol.Max(system => system.Position.Y) > 300,
            "barred-spiral systems do not occupy the core, arms and outer map");
        Require(nonSol.Min(system => Vector2.Distance(system.Position, Vector2.Zero)) < 150 &&
            nonSol.Count(system => Vector2.Distance(system.Position, Vector2.Zero) < 300) >= 3,
            "the Sol start has no practical early exploration neighborhood");
        Require(metadata.SpoilerFreeSummary.Contains("100 systems", StringComparison.Ordinal) &&
            !metadata.SpoilerFreeSummary.Contains("Sol", StringComparison.OrdinalIgnoreCase),
            "setup summary is missing its size or reveals generated content");
        var laneNetwork = new InterstellarLaneNetwork();
        var lanes = laneNetwork.Build(first.Galaxy.Systems);
        Require(lanes.Count is >= 99 and <= 260 && lanes.All(lane => lane.LengthLightYears > 0) &&
            lanes.DistinctBy(lane => (lane.FirstSystemId, lane.SecondSystemId)).Count() == lanes.Count,
            "interstellar lane graph is disconnected, duplicated or too dense");
        Require(first.Galaxy.Systems.All(system =>
                laneNetwork.FindShortestRoute(first.Galaxy.Systems, SolCatalogPreset.SystemId, system.Id).Count > 0) &&
            first.Galaxy.Civilizations.All(civilization =>
                lanes.Count(lane => lane.Connects(civilization.HomeSystemId)) >= 2),
            "lane graph does not connect every system or leaves a starting system without alternatives");
        Require(lanes.SequenceEqual(laneNetwork.Build(second.Galaxy.Systems)),
            "same seed and coordinates did not reproduce the lane graph");
        Require(Math.Abs(AstronomicalDistance.LightYearsToParsecs(3.26156) - 1.0) < 1e-10 &&
            Math.Abs(AstronomicalDistance.AuToKilometres(1.0) - 149_597_870.7) < 1e-6,
            "maintained astronomical unit conversions changed");

        var root = Path.Combine(Path.GetTempPath(), $"stellar-continuum-sandbox-setup-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);
        try
        {
            var path = Path.Combine(root, "sandbox.json");
            var rawGeneratedBodies = new PlanetaryBodyGenerator().Generate(first.Galaxy.Seed, first.Galaxy.Systems);
            var rawBodiesById = rawGeneratedBodies.ToDictionary(body => body.Id);
            var guaranteeAlteredBodyCount = first.Galaxy.PlanetaryBodies.Count(body =>
                rawBodiesById.TryGetValue(body.Id, out var raw) &&
                (body.MassEarth != raw.MassEarth || body.Environment != raw.Environment));
            Require(guaranteeAlteredBodyCount > 0,
                "catalog persistence fixture did not contain a guarantee-altered physical body");
            sessions.Save(path, first.Galaxy, first.Diplomacy, first.AdaptiveResearch, 0.0);
            var loaded = sessions.LoadOrCreate(path, fallbackSeed: 1);
            Require(loaded.Galaxy.PlanetaryBodies.SequenceEqual(first.Galaxy.PlanetaryBodies),
                $"save/load discarded {guaranteeAlteredBodyCount} guarantee-altered physical bodies");
            Require(loaded.Galaxy.GenerationMetadata == metadata,
                "entered seed or generation option snapshot did not survive save and load");
            Require(loaded.Galaxy.GalacticCore == core,
                "save/load discarded the galactic-core landmark coordinates");
            Require(loaded.Galaxy.Knowledge.HasGalacticCoreAccess(coreObserver) &&
                    loaded.Galaxy.Knowledge.IsGalacticCoreDiscovered(coreObserver) &&
                    !loaded.Galaxy.Knowledge.HasGalacticCoreAccess(otherObserver) &&
                    !loaded.Galaxy.Knowledge.IsGalacticCoreDiscovered(otherObserver),
                "save/load discarded or leaked observer-specific galactic-core knowledge");
            Require(loaded.Galaxy.Systems.Select(system => system.StellarClass)
                .SequenceEqual(first.Galaxy.Systems.Select(system => system.StellarClass)),
                "physical stellar classes did not survive save and load");
            Require(loaded.Galaxy.Systems.Select(system => system.Position).SequenceEqual(first.Galaxy.Systems.Select(system => system.Position)),
                "loading a campaign moved its saved star coordinates to fit artwork");

            foreach (var invalidCore in new[]
                     {
                         core with { LandmarkKey = "not-a-core" },
                         core with { ExclusionRadius = float.NaN },
                         core with { X = first.Galaxy.Systems[0].Position.X, Y = first.Galaxy.Systems[0].Position.Y },
                     })
            {
                first.Galaxy.GenerationMetadata = metadata with { GalacticCore = invalidCore };
                first.Galaxy.GalacticCore = invalidCore;
                RequireThrowsInvalidData(() => sessions.Save(path, first.Galaxy, first.Diplomacy, first.AdaptiveResearch, 0.0),
                    "malformed galactic-core metadata was accepted for persistence");
            }
            first.Galaxy.GenerationMetadata = metadata;
            first.Galaxy.GalacticCore = core with { X = core.X + 1.0f };
            RequireThrowsInvalidData(() => sessions.Save(path, first.Galaxy, first.Diplomacy, first.AdaptiveResearch, 0.0),
                "disagreeing state and generation core descriptors were accepted for persistence");
            first.Galaxy.GalacticCore = core;
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }

        var legacy = sessions.CreateNew(12345L);
        Require(legacy.Galaxy.Civilizations.Count == 10,
            "numeric campaign creation no longer preserves its established civilization defaults");
        Require(legacy.Galaxy.Systems.Count == 100 && legacy.Galaxy.Systems.All(system => system.StellarClass.HasValue),
            "new legacy-disk campaigns omitted physical stellar classes");
        Require(legacy.Galaxy.GalacticCore is null && legacy.Galaxy.GenerationMetadata?.GalacticCore is null,
            "legacy numeric/disk campaign unexpectedly migrated into the core layout");
        var legacyRoot = Path.Combine(Path.GetTempPath(), $"stellar-continuum-legacy-core-{Guid.NewGuid():N}");
        Directory.CreateDirectory(legacyRoot);
        try
        {
            var legacyPath = Path.Combine(legacyRoot, "legacy.json");
            legacy.Galaxy.GenerationMetadata = null;
            sessions.Save(legacyPath, legacy.Galaxy, legacy.Diplomacy, legacy.AdaptiveResearch, 0.0);
            var reloadedLegacy = sessions.LoadOrCreate(legacyPath, fallbackSeed: 1);
            Require(reloadedLegacy.Galaxy.GenerationMetadata is null && reloadedLegacy.Galaxy.GalacticCore is null &&
                reloadedLegacy.Galaxy.Systems.Select(system => system.Position).SequenceEqual(legacy.Galaxy.Systems.Select(system => system.Position)),
                "metadata-null legacy save was relocated or given a galactic core");
        }
        finally { Directory.Delete(legacyRoot, recursive: true); }
        Require(legacy.Galaxy.Systems.Single(system => system.CatalogPresetId == SolCatalogPreset.PresetId).StellarClass ==
            StellarPrimaryClass.GYellowDwarf, "legacy-disk Sol was not retained as a G-type star");
        var legacyRepeat = sessions.CreateNew(12345L);
        Require(legacy.Galaxy.Systems.Select(system => (system.Name, system.Position, system.Archetype, system.StellarClass))
                .SequenceEqual(legacyRepeat.Galaxy.Systems.Select(system =>
                    (system.Name, system.Position, system.Archetype, system.StellarClass))),
            "same numeric seed did not reproduce legacy-disk physical stellar classes");

        for (var index = 0; index < 12; index++)
            ValidateNearbyWorldGuarantees(CreateLegacySandbox($"FAIR-OPENING-{index}").Galaxy);

        // Random startup seeds are Unix milliseconds.  Exercise a deterministic contiguous
        // sample so an allocation regression reports the exact portable reproduction seed.
        const long sweepStartSeed = 1_789_000_000_000L;
        for (var offset = 0; offset < 256; offset++)
        {
            var seed = sweepStartSeed + offset;
            foreach (var selectedSpecies in SpeciesCatalog.All)
            {
                try
                {
                    var settings = GalaxyGenerationMetadata.Standard100("nearby-world-sweep", seed, selectedSpecies.Id)
                        .ToSettings();
                    ValidateNearbyWorldGuarantees(sessions.CreateNew(seed, settings).Galaxy);
                }
                catch (Exception exception) when (exception is InvalidOperationException)
                {
                    throw new InvalidOperationException(
                        $"Nearby-world startup sweep failed for reproducible seed {seed} / {selectedSpecies.Id}: " +
                        exception.Message, exception);
                }
            }
        }
    }

    private static CampaignBootstrapResult CreateLegacySandbox(string enteredSeed)
    {
        var metadata = GalaxyGenerationMetadata.Standard100(enteredSeed, CampaignSeed.Parse(enteredSeed));
        var result = new CampaignSessionService().CreateNew(metadata.InternalSeed, metadata.ToSettings());
        result.Galaxy.GenerationMetadata = metadata;
        return result;
    }

    private static void ValidateNearbyWorldGuarantees(GalaxyState galaxy)
    {
        var evaluator = new SpeciesPlanetaryHabitabilityEvaluator();
        var homeIds = galaxy.Civilizations.Select(civilization => civilization.HomeSystemId).ToHashSet();
        foreach (var civilization in galaxy.Civilizations.Where(civilization => !civilization.IsSeededAncient))
        {
            var home = galaxy.Systems.Single(system => system.Id == civilization.HomeSystemId);
            var viableSystems = galaxy.Systems.Where(system => !homeIds.Contains(system.Id) &&
                    Vector2.Distance(home.Position, system.Position) <= NearbyHabitableWorldGuaranteePolicy.MaximumOpeningDistance)
                .Count(system => galaxy.PlanetaryBodies.Any(body => body.SystemId == system.Id &&
                    evaluator.Evaluate(body, civilization.SpeciesId).Viability == SpeciesColonizationViability.NaturallyViable));
            Require(viableSystems >= 2,
                $"civilization {civilization.Id} has only {viableSystems} nearby species-compatible expansion worlds");
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private static void RequireThrowsInvalidData(Action action, string message)
    {
        try { action(); }
        catch (InvalidDataException) { return; }
        throw new InvalidOperationException(message);
    }
}
