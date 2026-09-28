using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.Generation;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class Program
{
    private static int Main()
    {
        var tests = new (string Name, Action Run)[]
        {
            ("deterministic galaxy generation", ValidateDeterministicGalaxyGeneration),
            ("simulation clock pause/backlog", ValidateSimulationClock),
            ("save format v8 round trip", ValidateSaveRoundTrip),
            ("v6 to current shipyard/survey/species/combat migration", ValidateV6Migration),
            ("v7 to v8 species migration", ValidateV7SpeciesMigration),
            ("bounded shipyard queue load", ValidateBoundedShipyardQueueLoad),
            ("shipyard reserved population load integrity", ValidateShipyardReservedPopulationLoadIntegrity),
            ("scout vs science survey knowledge", ExplorationColonizationValidation.ValidateScoutAndScienceSurveyRoles),
            ("colonization requires full science survey", ExplorationColonizationValidation.ValidateColonizationRequiresFullSurvey),
            ("colony population and survey persistence", ExplorationColonizationValidation.ValidateColonyPopulationConservationAndPersistence),
            ("shared operational reach gate", OperationalReachValidation.ValidateSharedMissionReachGate),
            ("lane-routed interstellar travel", InterstellarTravelValidation.ValidateLaneRoutingAndPersistence),
            ("physical interstellar depth distance", InterstellarTravelValidation.ValidatePhysicalDepthDistanceAndPersistence),
            ("resource outpost economy and support", OutpostFoundationValidation.ValidateOutpostRulesAndPersistence),
            ("dedicated resource outpost vessel and founding", ResourceOutpostMissionValidation.ValidateDedicatedVesselAndFoundingFlow),
            ("represented outpost freight collection and delivery", OutpostFreightValidation.ValidateRepresentedCollectionAndDelivery),
            ("directional first contact requires presence", FirstContactValidation.ValidateDirectionalContactRequiresPresence),
            ("deterministic planetary catalog", PlanetaryBodyValidation.ValidateDeterministicPhysicalCatalogAndSaveReconstruction),
            ("species-relative body colonization", PlanetaryBodyValidation.ValidateSurveyVisibilityAndBodyLevelColonization),
            ("military ship construction", CombatValidation.ValidateMilitaryShipConstruction),
            ("peaceful fleets do not fight", CombatValidation.ValidatePeacefulFleetsDoNotFight),
            ("deterministic combat destruction", CombatValidation.ValidateDeterministicEngagementAndDestruction),
            ("combat retreat disengagement", CombatValidation.ValidateRetreatDisengagesSurvivor),
            ("combat save and legacy defaults", SpeciesCombatSaveValidation.ValidateCombatStateWithinSharedV8AndV7Defaults),
            ("fair-information military summary", CombatValidation.ValidateFairInformationMilitarySummary),
            ("indexed combat defense targeting", CombatValidation.ValidateIndexedDefenseTargeting),
            ("diplomacy political state controls combat", DiplomacyCombatValidation.ValidatePoliticalStateControlsCombat),
        };

        var failures = Game.Validation.RegressionRunner.Run(typeof(Program).Assembly);
        foreach (var test in tests)
        {
            try
            {
                test.Run();
                Console.WriteLine($"PASS: {test.Name}");
            }
            catch (Exception ex)
            {
                failures++;
                Game.Validation.RegressionRunner.Report(test.Name, ex);
            }
        }

        Console.WriteLine($"Core simulation validation: {tests.Length + Game.Validation.RegressionRunner.Count - failures}/{tests.Length + Game.Validation.RegressionRunner.Count} passed.");
        return failures == 0 ? 0 : 1;
    }

    private static void ValidateDeterministicGalaxyGeneration()
    {
        const long seed = 0x51A7_2026_0907;
        var settings = new GalaxyGenerationSettings
        {
            SystemCount = 48,
            PreWarpCivilizationCount = 5,
            AncientCivilizationCount = 1,
            Radius = 520.0f,
        };

        var generator = new GalaxyGenerator();
        var first = generator.Generate(seed, settings);
        var second = generator.Generate(seed, settings);

        Require(first.Seed == second.Seed, "seed changed between identical generations");
        Require(first.PlayerCivilizationId == second.PlayerCivilizationId, "player civilization changed between identical generations");
        Require(first.Systems.Count == second.Systems.Count, "system count changed between identical generations");
        Require(first.Civilizations.Count == second.Civilizations.Count, "civilization count changed between identical generations");
        Require(first.PlanetaryBodies.SequenceEqual(second.PlanetaryBodies), "planetary catalog changed between identical generations");

        for (var i = 0; i < first.Systems.Count; i++)
        {
            var a = first.Systems[i];
            var b = second.Systems[i];
            Require(a.Id == b.Id && a.Name == b.Name, $"system identity diverged at index {i}");
            Require(a.Position == b.Position, $"system position diverged for {a.Name}");
            Require(a.Archetype == b.Archetype, $"system archetype diverged for {a.Name}");
            Require(a.HasHabitableWorld == b.HasHabitableWorld, $"habitability diverged for {a.Name}");
            Require(a.HasAnomaly == b.HasAnomaly, $"anomaly state diverged for {a.Name}");
            Require(a.HasRareResource == b.HasRareResource, $"resource state diverged for {a.Name}");
            Require(a.HasPreWarpCivilization == b.HasPreWarpCivilization, $"pre-warp state diverged for {a.Name}");
        }

        for (var i = 0; i < first.Civilizations.Count; i++)
        {
            var a = first.Civilizations[i];
            var b = second.Civilizations[i];
            Require(a.Id == b.Id && a.Name == b.Name, $"civilization identity diverged at index {i}");
            Require(a.HomeSystemId == b.HomeSystemId, $"home system diverged for {a.Name}");
            Require(a.Archetype == b.Archetype, $"archetype diverged for {a.Name}");
            Require(a.SpeciesId == b.SpeciesId, $"species assignment diverged for {a.Name}");
            Require(SpeciesCatalog.TryGet(a.SpeciesId, out _), $"{a.Name} references unknown species {a.SpeciesId}");
            Require(a.DevelopmentStage == b.DevelopmentStage, $"development stage diverged for {a.Name}");
            Require(a.IsSeededAncient == b.IsSeededAncient, $"ancient flag diverged for {a.Name}");
        }

        foreach (var colony in first.Colonies)
        {
            var civilization = first.Civilizations.First(c => c.Id == colony.CivilizationId);
            Require(
                colony.PopulationSpeciesId == civilization.SpeciesId,
                $"seeded colony {colony.Id} did not inherit its civilization's population species");
        }
    }

    private static void ValidateSimulationClock()
    {
        var clock = new SimulationClock();
        clock.Restore(42.0);
        clock.SetSpeed(SimulationClock.SpeedLevel.Paused);

        var pausedDelta = clock.Advance(10.0);
        Require(pausedDelta == 0.0, "paused clock accepted simulation time");
        Require(clock.SimulationDays == 42.0, "paused clock changed campaign date");
        Require(clock.EffectiveMultiplier == 0.0, "paused clock reported a nonzero effective multiplier");

        clock.SetSpeed(SimulationClock.SpeedLevel.Maximum);
        var requestedDays = clock.RequestedMultiplier * 1.0;
        var acceptedDays = clock.Advance(1.0);
        Require(acceptedDays > 0.0, "running clock did not advance");
        Require(acceptedDays < requestedDays, "backlog protection failed to throttle an oversized requested step");
        Require(clock.BacklogDays > 0.0, "oversized requested step did not create a bounded scalar backlog");
        Require(double.IsFinite(clock.SimulationDays) && double.IsFinite(clock.BacklogDays), "clock produced non-finite state");

        foreach (var speed in new[]
                 {
                     SimulationClock.SpeedLevel.Normal, SimulationClock.SpeedLevel.Fast,
                     SimulationClock.SpeedLevel.VeryFast, SimulationClock.SpeedLevel.Maximum,
                 })
        {
            clock.SetSpeed(speed);
            clock.SetSpeed(SimulationClock.SpeedLevel.Paused);
            Require(clock.ResumeSpeed == speed, $"paused clock did not expose its remembered {speed} speed");
            clock.Resume();
            Require(clock.Speed == speed, $"pause/resume forgot permitted {speed} speed");
        }

        clock.SetSpeed(SimulationClock.SpeedLevel.Fast);
        clock.SetSpeed(SimulationClock.SpeedLevel.Paused);
        clock.SetSpeed(SimulationClock.SpeedLevel.Paused);
        Require(clock.ResumeSpeed == SimulationClock.SpeedLevel.Fast, "repeated pause hid the last running speed");
        clock.Resume();
        Require(clock.Speed == SimulationClock.SpeedLevel.Fast, "repeated pause erased the last running speed");

        clock.SetSpeed(SimulationClock.SpeedLevel.Demo);
        clock.SetSpeed(SimulationClock.SpeedLevel.Paused);
        clock.Resume();
        Require(clock.Speed == SimulationClock.SpeedLevel.Demo, "Developer speed did not resume in its permitted context");
        clock.SetSpeed(SimulationClock.SpeedLevel.Normal);
        clock.SetSpeed(SimulationClock.SpeedLevel.Paused);
        clock.Resume();
        Require(clock.Speed == SimulationClock.SpeedLevel.Normal, "normal mode reset did not clear Developer speed memory");
    }

    private static void ValidateSaveRoundTrip()
    {
        WithTemporaryDirectory(directory =>
        {
            var galaxy = CreateValidationGalaxy();
            galaxy.Colonies[0].StoredFoodPopulationDaysMillions = 1234.5;
            galaxy.Colonies[0].StoredWaterPopulationDaysMillions = 345.6;
            var service = new CampaignSaveService();
            var path = Path.Combine(directory, "roundtrip.json");
            const double simulationDays = 713.25;

            service.Save(path, galaxy, simulationDays);
            var loaded = service.Load(path);

            Require(CampaignSaveService.CurrentFormatVersion == 16 && CampaignSaveService.SurfaceFormatVersion == 12 && CampaignSaveService.PresetFormatVersion == 10 && CampaignSaveService.LegacyFormatVersion == 8, "planetary catalog persistence version contract changed");
            Require(loaded.Galaxy.Seed == galaxy.Seed, "save/load changed galaxy seed");
            Require(loaded.Galaxy.Systems.Count == galaxy.Systems.Count, "save/load changed system count");
            Require(loaded.Galaxy.PlanetaryBodies.SequenceEqual(galaxy.PlanetaryBodies), "save/load changed reconstructible planetary catalog");
            Require(loaded.Galaxy.Civilizations.Count == galaxy.Civilizations.Count, "save/load changed civilization count");
            Require(loaded.Galaxy.ShipyardStates.Count == galaxy.Civilizations.Count, "save/load lost shipyard state");
            Require(
                loaded.Galaxy.Civilizations.Select(civilization => civilization.SpeciesId)
                    .SequenceEqual(galaxy.Civilizations.Select(civilization => civilization.SpeciesId)),
                "save/load changed civilization species identity");
            Require(
                loaded.Galaxy.Colonies.OrderBy(c => c.Id).Select(c => c.PopulationSpeciesId)
                    .SequenceEqual(galaxy.Colonies.OrderBy(c => c.Id).Select(c => c.PopulationSpeciesId)),
                "save/load changed colony population species identity");
            Require(Math.Abs(loaded.Galaxy.Colonies[0].StoredFoodPopulationDaysMillions - 1234.5) < .000001 &&
                Math.Abs(loaded.Galaxy.Colonies[0].StoredWaterPopulationDaysMillions - 345.6) < .000001,
                "save/load changed colony food or potable-water reserves");
            Require(Math.Abs(loaded.SimulationDays - simulationDays) < 0.000001, "save/load changed simulation date");

            var json = File.ReadAllText(path);
            Require(json.Contains("\"FormatVersion\": 16", StringComparison.Ordinal), "new save file did not declare authoritative catalog format v16");
            Require(json.Contains("\"SpeciesId\"", StringComparison.Ordinal), "save file did not persist civilization species identity");
            Require(json.Contains("\"PopulationSpeciesId\"", StringComparison.Ordinal), "save file did not persist colony population species identity");
            Require(json.Contains("\"PlanetaryBodyId\"", StringComparison.Ordinal), "save file did not expose v8 colony body field");
            Require(json.Contains("\"PlanetaryBodies\"", StringComparison.Ordinal), "save file omitted authoritative planetary catalog");
            Require(!File.Exists(path + ".tmp"), "atomic save left a temporary file behind");
        });
    }

    private static void ValidateV6Migration()
    {
        WithTemporaryDirectory(directory =>
        {
            var galaxy = CreateValidationGalaxy();
            var service = new CampaignSaveService();
            var currentPath = Path.Combine(directory, "source-v8.json");
            var v6Path = Path.Combine(directory, "legacy-v6.json");

            service.Save(currentPath, galaxy, 365.0);
            var root = JsonNode.Parse(File.ReadAllText(currentPath))?.AsObject()
                ?? throw new InvalidOperationException("could not parse generated v8 save");
            root["FormatVersion"] = 6;
            root["Galaxy"]!.AsObject().Remove("PlanetaryBodies");
            var galaxyNode = root["Galaxy"]?.AsObject()
                ?? throw new InvalidOperationException("generated save did not contain Galaxy");
            galaxyNode.Remove("ShipyardStates");

            foreach (var civilization in galaxyNode["Civilizations"]?.AsArray()
                         ?? throw new InvalidOperationException("generated save did not contain civilizations"))
            {
                civilization?.AsObject().Remove("SpeciesId");
            }

            if (galaxyNode["Colonies"] is JsonArray colonies)
            {
                foreach (var item in colonies)
                {
                    item?.AsObject().Remove("PopulationSpeciesId");
                    item?.AsObject().Remove("PlanetaryBodyId");
                }
            }

            // A real v6 save predates staged surveys, physical embarked population, species
            // identity, body targets, shipyards, and explicit persistent combat state.
            if (galaxyNode["Knowledge"] is JsonArray knowledge)
            {
                foreach (var item in knowledge)
                    item?.AsObject().Remove("SystemSurveys");
            }
            if (galaxyNode["Fleets"] is JsonArray fleets)
            {
                foreach (var item in fleets)
                {
                    item?.AsObject().Remove("EmbarkedPopulationMillions");
                    item?.AsObject().Remove("EmbarkedPopulationSpeciesId");
                    item?.AsObject().Remove("DestinationPlanetaryBodyId");
                    item?.AsObject().Remove("Combat");
                }
            }

            File.WriteAllText(
                v6Path,
                root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));

            var migrated = service.Load(v6Path);
            Require(migrated.Galaxy.ShipyardStates.Count == migrated.Galaxy.Civilizations.Count, "v6 migration did not seed one shipyard state per civilization");
            Require(migrated.Galaxy.ShipyardStates.All(state => state.ActiveDesignId is null), "v6 migration invented active ship builds");
            Require(migrated.Galaxy.ShipyardStates.All(state => state.QueuedBuilds.Count == 0), "v6 migration invented queued ship builds");
            Require(migrated.Galaxy.ShipyardStates.All(state => state.ReservedPopulationMillions == 0.0), "v6 migration invented reserved colonists");
            Require(migrated.Galaxy.Fleets.All(fleet => fleet.Combat is not null), "v6 migration did not initialize legacy fleet combat state");
            Require(migrated.Galaxy.Fleets.All(fleet => fleet.DestinationPlanetaryBodyId is null), "v6 migration invented body-specific fleet targets");

            foreach (var civilization in migrated.Galaxy.Civilizations)
            {
                Require(
                    civilization.SpeciesId == SpeciesAssignmentPolicy.Assign(migrated.Galaxy.Seed, civilization.Id),
                    $"v6 migration did not deterministically assign species for civilization {civilization.Id}");
                Require(
                    migrated.Galaxy.Colonies.Where(c => c.CivilizationId == civilization.Id)
                        .All(c => c.PopulationSpeciesId == civilization.SpeciesId),
                    $"v6 migration did not assign colony population species for civilization {civilization.Id}");
            }

            var player = migrated.Galaxy.Civilizations.First(civilization =>
                civilization.Id == migrated.Galaxy.PlayerCivilizationId);
            Require(
                migrated.Galaxy.Knowledge.GetKnownSystems(player.Id)
                    .All(systemId => migrated.Galaxy.Knowledge.IsSystemFullySurveyed(player.Id, systemId)),
                "legacy known-system knowledge was not preserved as full survey knowledge");
        });
    }

    private static void ValidateV7SpeciesMigration()
    {
        WithTemporaryDirectory(directory =>
        {
            var galaxy = CreateValidationGalaxy();
            var service = new CampaignSaveService();
            var currentPath = Path.Combine(directory, "source-v8.json");
            var v7Path = Path.Combine(directory, "legacy-v7.json");

            service.Save(currentPath, galaxy, 512.0);
            var root = JsonNode.Parse(File.ReadAllText(currentPath))?.AsObject()
                ?? throw new InvalidOperationException("could not parse generated v8 save");
            root["FormatVersion"] = 7;
            root["Galaxy"]!.AsObject().Remove("PlanetaryBodies");
            var galaxyNode = root["Galaxy"]?.AsObject()
                ?? throw new InvalidOperationException("generated save did not contain Galaxy");

            foreach (var civilization in galaxyNode["Civilizations"]?.AsArray()
                         ?? throw new InvalidOperationException("generated save did not contain civilizations"))
            {
                civilization?.AsObject().Remove("SpeciesId");
            }

            if (galaxyNode["Colonies"] is JsonArray colonies)
            {
                foreach (var item in colonies)
                {
                    item?.AsObject().Remove("PopulationSpeciesId");
                    item?.AsObject().Remove("PlanetaryBodyId");
                }
            }
            if (galaxyNode["Fleets"] is JsonArray fleets)
            {
                foreach (var item in fleets)
                {
                    item?.AsObject().Remove("EmbarkedPopulationSpeciesId");
                    item?.AsObject().Remove("DestinationPlanetaryBodyId");
                }
            }
            if (galaxyNode["ShipyardStates"] is JsonArray shipyards)
            {
                foreach (var item in shipyards)
                {
                    var shipyard = item?.AsObject();
                    shipyard?.Remove("ReservedPopulationSpeciesId");
                    if (shipyard?["QueuedBuilds"] is JsonArray queued)
                    {
                        foreach (var build in queued)
                            build?.AsObject().Remove("ReservedPopulationSpeciesId");
                    }
                }
            }

            File.WriteAllText(
                v7Path,
                root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));

            var migrated = service.Load(v7Path);
            Require(migrated.Galaxy.ShipyardStates.Count == galaxy.ShipyardStates.Count, "v7 to v8 migration lost existing shipyard state");
            Require(migrated.Galaxy.Fleets.All(fleet => fleet.Combat is not null), "v7 to v8 migration lost or failed to initialize Combat state");
            Require(migrated.Galaxy.Fleets.All(fleet => fleet.DestinationPlanetaryBodyId is null), "v7 to v8 migration invented explicit body targets absent from v7");

            foreach (var civilization in migrated.Galaxy.Civilizations)
            {
                Require(
                    civilization.SpeciesId == SpeciesAssignmentPolicy.Assign(migrated.Galaxy.Seed, civilization.Id),
                    $"v7 to v8 migration did not deterministically assign species for civilization {civilization.Id}");
                Require(
                    migrated.Galaxy.Colonies.Where(c => c.CivilizationId == civilization.Id)
                        .All(c => c.PopulationSpeciesId == civilization.SpeciesId),
                    $"v7 to v8 migration did not reconstruct colony population species for civilization {civilization.Id}");
                Require(
                    migrated.Galaxy.Fleets
                        .Where(f => f.CivilizationId == civilization.Id && f.EmbarkedPopulationMillions > 0.0)
                        .All(f => f.EmbarkedPopulationSpeciesId == civilization.SpeciesId),
                    $"v7 to v8 migration did not reconstruct embarked population species for civilization {civilization.Id}");
            }
        });
    }

    private static void ValidateBoundedShipyardQueueLoad()
    {
        WithTemporaryDirectory(directory =>
        {
            var galaxy = CreateValidationGalaxy();
            var service = new CampaignSaveService();
            var sourcePath = Path.Combine(directory, "bounded-source.json");
            var oversizedPath = Path.Combine(directory, "oversized-queue.json");

            service.Save(sourcePath, galaxy, 100.0);
            var root = JsonNode.Parse(File.ReadAllText(sourcePath))?.AsObject()
                ?? throw new InvalidOperationException("could not parse generated save");
            var shipyards = root["Galaxy"]?["ShipyardStates"]?.AsArray()
                ?? throw new InvalidOperationException("generated save did not contain shipyards");
            var first = shipyards[0]?.AsObject()
                ?? throw new InvalidOperationException("generated save had no shipyard entries");
            var firstCivilizationId = galaxy.ShipyardStates[0].CivilizationId;

            first["ActiveDesignId"] = "warp_scout";
            first["ReservedPopulationMillions"] = -25.0;
            first["ReservedPopulationSpeciesId"] = null;
            var queue = new JsonArray();
            for (var i = 0; i < ShipyardState.MaxPendingBuilds * 4; i++)
            {
                queue.Add(new JsonObject
                {
                    ["DesignId"] = "warp_scout",
                    ["ReservedPopulationMillions"] = i % 2 == 0 ? -5.0 : 0.0,
                    ["ReservedPopulationSpeciesId"] = null,
                });
            }
            first["QueuedBuilds"] = queue;
            File.WriteAllText(
                oversizedPath,
                root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));

            var loaded = service.Load(oversizedPath);
            var loadedState = loaded.Galaxy.ShipyardStates.First(
                state => state.CivilizationId == firstCivilizationId);
            Require(loadedState.PendingBuildCount == ShipyardState.MaxPendingBuilds, "zero-population oversized queue was not clamped to the bounded maximum");
            Require(loadedState.QueuedBuilds.Count == ShipyardState.MaxPendingBuilds - 1, "active build was not counted against bounded queue capacity");
            Require(loadedState.ReservedPopulationMillions == 0.0 && loadedState.ReservedPopulationSpeciesId is null, "negative active reserved population/species was not sanitized");
            Require(loadedState.QueuedBuilds.All(build => build.ReservedPopulationMillions == 0.0), "non-positive queued reserved population was not sanitized to zero");
        });
    }

    private static void ValidateShipyardReservedPopulationLoadIntegrity()
    {
        WithTemporaryDirectory(directory =>
        {
            var galaxy = CreateValidationGalaxy();
            var service = new CampaignSaveService();
            var sourcePath = Path.Combine(directory, "reservation-integrity-source.json");
            service.Save(sourcePath, galaxy, 120.0);

            var colonyDesign = ShipDesignRegistry.All.First(design => design.PopulationCostMillions > 0.0);
            var firstCivilizationId = galaxy.ShipyardStates[0].CivilizationId;
            var speciesId = galaxy.Civilizations.First(c => c.Id == firstCivilizationId).SpeciesId;

            void ExpectRejected(string fileName, Action<JsonObject> mutate, string scenario)
            {
                var root = JsonNode.Parse(File.ReadAllText(sourcePath))?.AsObject()
                    ?? throw new InvalidOperationException("could not parse generated reservation-integrity save");
                var shipyards = root["Galaxy"]?["ShipyardStates"]?.AsArray()
                    ?? throw new InvalidOperationException("generated save did not contain shipyards");
                var first = shipyards[0]?.AsObject()
                    ?? throw new InvalidOperationException("generated save had no shipyard entries");
                mutate(first);

                var path = Path.Combine(directory, fileName);
                File.WriteAllText(path, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
                try
                {
                    service.Load(path);
                    throw new InvalidOperationException($"{scenario} was silently accepted");
                }
                catch (InvalidDataException)
                {
                    // Expected: refusing to load is safer than silently deleting reserved people.
                }
            }

            ExpectRejected(
                "invalid-active-reservation.json",
                first =>
                {
                    first["ActiveDesignId"] = "unknown_population_transport";
                    first["ReservedPopulationMillions"] = colonyDesign.PopulationCostMillions;
                    first["ReservedPopulationSpeciesId"] = speciesId;
                },
                "unknown active design carrying reserved population");

            ExpectRejected(
                "invalid-queued-reservation.json",
                first =>
                {
                    first["ActiveDesignId"] = null;
                    first["ReservedPopulationMillions"] = 0.0;
                    first["ReservedPopulationSpeciesId"] = null;
                    first["QueuedBuilds"] = new JsonArray
                    {
                        new JsonObject
                        {
                            ["DesignId"] = "unknown_population_transport",
                            ["ReservedPopulationMillions"] = colonyDesign.PopulationCostMillions,
                            ["ReservedPopulationSpeciesId"] = speciesId,
                        },
                    };
                },
                "unknown queued design carrying reserved population");

            ExpectRejected(
                "overflow-reservation.json",
                first =>
                {
                    first["ActiveDesignId"] = "warp_scout";
                    first["ReservedPopulationMillions"] = 0.0;
                    first["ReservedPopulationSpeciesId"] = null;
                    var queue = new JsonArray();
                    for (var i = 0; i < ShipyardState.MaxPendingBuilds - 1; i++)
                    {
                        queue.Add(new JsonObject
                        {
                            ["DesignId"] = "warp_scout",
                            ["ReservedPopulationMillions"] = 0.0,
                            ["ReservedPopulationSpeciesId"] = null,
                        });
                    }
                    queue.Add(new JsonObject
                    {
                        ["DesignId"] = colonyDesign.Id,
                        ["ReservedPopulationMillions"] = colonyDesign.PopulationCostMillions,
                        ["ReservedPopulationSpeciesId"] = speciesId,
                    });
                    first["QueuedBuilds"] = queue;
                },
                "overflow queued build carrying reserved population");
        });
    }

    private static Game.Simulation.Models.GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x4D49_4752_4154_45L,
            new GalaxyGenerationSettings
            {
                SystemCount = 36,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 420.0f,
            });

    private static void WithTemporaryDirectory(Action<string> action)
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            "stellar-continuum-validation",
            Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            action(directory);
        }
        finally
        {
            if (Directory.Exists(directory))
                Directory.Delete(directory, recursive: true);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
