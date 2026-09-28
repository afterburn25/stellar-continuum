using Game.Persistence;
using Game.Simulation.Colonization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class PlanetaryBodyValidation
{
    public static void ValidateDeterministicPhysicalCatalogAndSaveReconstruction()
    {
        const long seed = 0x504C_414E_4554_5359L;
        var settings = new GalaxyGenerationSettings
        {
            SystemCount = 52,
            PreWarpCivilizationCount = 5,
            AncientCivilizationCount = 1,
            Radius = 560.0f,
        };

        var generator = new GalaxyGenerator();
        var first = generator.Generate(seed, settings);
        var second = generator.Generate(seed, settings);

        Require(first.PlanetaryBodies.Count > first.Systems.Count, "planetary catalog did not produce a useful bounded body population");
        Require(first.PlanetaryBodies.SequenceEqual(second.PlanetaryBodies), "identical campaign seed/system catalog produced different planet/moon state");
        Require(first.PlanetaryBodies.Select(body => body.Id).Distinct().Count() == first.PlanetaryBodies.Count, "planetary body IDs are not globally unique");

        var systemsById = first.Systems.ToDictionary(system => system.Id);
        var bodiesById = first.PlanetaryBodies.ToDictionary(body => body.Id);
        foreach (var body in first.PlanetaryBodies)
        {
            body.Validated();
            Require(systemsById.ContainsKey(body.SystemId), $"body {body.Id} references an unknown star system");
            Require(body.Id / 1000 == body.SystemId, $"body {body.Id} does not preserve its stable system ID namespace");

            if (body.Kind != PlanetaryBodyKind.Moon)
                continue;

            if (body.ParentBodyId is not int parentId)
                throw new InvalidOperationException($"moon {body.Id} has no parent body");
            Require(bodiesById.TryGetValue(parentId, out var parent), $"moon {body.Id} parent is missing");
            Require(parent!.Kind == PlanetaryBodyKind.Planet, $"moon {body.Id} parent is not a planet");
            Require(parent.SystemId == body.SystemId, $"moon {body.Id} parent is in a different star system");
        }

        foreach (var system in first.Systems)
        {
            var candidates = first.PlanetaryBodies
                .Where(body => body.SystemId == system.Id && body.LegacyColonizationCandidate)
                .ToArray();
            Require(
                candidates.Length == (system.HasHabitableWorld ? 1 : 0),
                $"system {system.Id} did not preserve exactly the expected legacy compatibility candidate count");
            if (candidates.Length == 1)
            {
                Require(candidates[0].Kind == PlanetaryBodyKind.Planet, "compatibility colony candidate was not a planet");
                Require(candidates[0].Environment.HasSolidSurface, "compatibility colony candidate had no solid surface");
            }
        }

        WithTemporaryDirectory(directory =>
        {
            var path = Path.Combine(directory, "planetary-catalog-roundtrip.json");
            var service = new CampaignSaveService();
            service.Save(path, first, 288.0);

            var json = File.ReadAllText(path);
            Require(json.Contains("\"PlanetaryBodies\"", StringComparison.Ordinal), "v16 save omitted authoritative planetary catalog state");

            var loaded = service.Load(path);
            Require(
                first.PlanetaryBodies.SequenceEqual(loaded.Galaxy.PlanetaryBodies),
                "save/load did not reconstruct the exact deterministic planet/moon catalog");
        });
    }

    public static void ValidateSurveyVisibilityAndBodyLevelColonization()
    {
        WithTemporaryDirectory(directory =>
        {
            var galaxy = new GalaxyGenerator().Generate(
                0x574F_524C_4453_5552L,
                new GalaxyGenerationSettings
                {
                    SystemCount = 48,
                    PreWarpCivilizationCount = 5,
                    AncientCivilizationCount = 1,
                    Radius = 520.0f,
                });

            var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
            var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
            var surveyTarget = galaxy.Systems.First(system =>
                system.Id != player.HomeSystemId &&
                !galaxy.Knowledge.IsSystemFullySurveyed(player.Id, system.Id));

            galaxy.Knowledge.RevealSystem(player.Id, surveyTarget.Id);
            var readModel = new ExplorationReadModel();
            var detected = readModel.Build(galaxy, player.Id).KnownSystems.First(system => system.SystemId == surveyTarget.Id);
            Require(detected.SurveyLevel == SystemSurveyLevel.Detected, "validation system was not detection-level before reconnaissance");
            Require(detected.PlanetaryBodies.Count == 0, "mere star detection leaked the planetary catalog");

            galaxy.Knowledge.RecordReconnaissance(player.Id, surveyTarget.Id);
            var partial = readModel.Build(galaxy, player.Id).KnownSystems.First(system => system.SystemId == surveyTarget.Id);
            var authoritativeBodies = galaxy.PlanetaryBodies.Where(body => body.SystemId == surveyTarget.Id).OrderBy(body => body.Id).ToArray();
            Require(partial.PlanetaryBodies.Count == authoritativeBodies.Length, "scout reconnaissance did not reveal the basic orbital catalog");
            Require(partial.PlanetaryBodies.All(body => body.RadiusEarth > 0.0), "reconnaissance body catalog did not expose rough body size");
            Require(partial.PlanetaryBodies.All(body => body.MassEarth is null && body.GravityG is null && body.Atmosphere is null && body.HasRareResource is null), "scout reconnaissance leaked detailed physical/resource facts");

            galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, surveyTarget.Id);
            var detailed = readModel.Build(galaxy, player.Id).KnownSystems.First(system => system.SystemId == surveyTarget.Id);
            Require(detailed.PlanetaryBodies.Count == authoritativeBodies.Length, "full survey changed orbital catalog membership");
            foreach (var bodyView in detailed.PlanetaryBodies)
            {
                var authoritative = authoritativeBodies.First(body => body.Id == bodyView.BodyId);
                Require(bodyView.HasDetailedEnvironment, $"full survey did not expose environment for body {bodyView.BodyId}");
                Require(Math.Abs(bodyView.GravityG!.Value - authoritative.Environment.GravityG) < 0.0000001, "body gravity view diverged from authoritative physical state");
                Require(Math.Abs(bodyView.TemperatureKelvin!.Value - authoritative.Environment.TemperatureKelvin) < 0.0000001, "body temperature view diverged from authoritative physical state");
                Require(Math.Abs(bodyView.PressureKPa!.Value - authoritative.Environment.PressureKPa) < 0.0000001, "body pressure view diverged from authoritative physical state");
                Require(bodyView.Atmosphere == authoritative.Environment.Atmosphere, "body atmosphere view diverged from authoritative physical state");
                Require(bodyView.AvailableSolvent == authoritative.Environment.AvailableSolvent, "body solvent view diverged from authoritative physical state");
            }

            var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
            var targetGroup = galaxy.PlanetaryBodies
                .Where(body => body.SystemId != player.HomeSystemId)
                .GroupBy(body => body.SystemId)
                .Select(group => new
                {
                    SystemId = group.Key,
                    Viable = group
                        .Select(body => new { Body = body, Assessment = habitability.Evaluate(body, player.SpeciesId) })
                        .Where(entry => entry.Assessment.CanFoundCurrentColony)
                        .OrderByDescending(entry => entry.Assessment.Viability)
                        .ThenByDescending(entry => entry.Assessment.Environment.NaturalHabitability)
                        .ThenBy(entry => entry.Body.Id)
                        .FirstOrDefault(),
                    Unsuitable = group
                        .Select(body => new { Body = body, Assessment = habitability.Evaluate(body, player.SpeciesId) })
                        .Where(entry => !entry.Assessment.CanFoundCurrentColony)
                        .OrderBy(entry => entry.Body.Id)
                        .FirstOrDefault(),
                })
                .FirstOrDefault(group =>
                    group.Viable is not null &&
                    group.Unsuitable is not null &&
                    !galaxy.Colonies.Any(colony => colony.SystemId == group.SystemId))
                ?? throw new InvalidOperationException("validation galaxy did not contain a system with both viable and unsuitable species-relative colony bodies");

            var colonySystem = galaxy.Systems.First(system => system.Id == targetGroup.SystemId);
            var viableBody = targetGroup.Viable!.Body;
            var viableAssessment = targetGroup.Viable.Assessment;
            var rejectedBody = targetGroup.Unsuitable!.Body;
            galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, colonySystem.Id);

            var fleet = new FleetState
            {
                Id = galaxy.Fleets.Count == 0 ? 6000 : galaxy.Fleets.Max(candidate => candidate.Id) + 6000,
                CivilizationId = player.Id,
                Name = "Planetary Target Validation Pioneer",
                Role = FleetRole.Colony,
                Position = home.Position,
                CurrentSystemId = home.Id,
                StrategicSpeed = 14.0,
                SensorRange = 80.0f,
                IsActive = true,
                EmbarkedPopulationMillions = 250.0,
                EmbarkedPopulationSpeciesId = player.SpeciesId,
            };
            galaxy.Fleets.Add(fleet);

            var colonization = new ColonizationSimulation();
            var rejected = colonization.IssuePlayerColonyOrder(galaxy, player.Id, colonySystem.Id, rejectedBody.Id);
            Require(!rejected.Accepted, "species-relative colony model approved a body that its population assessment marked unusable");
            Require(fleet.DestinationSystemId is null && fleet.DestinationPlanetaryBodyId is null, "rejected body-level colony order still changed fleet target state");

            var accepted = colonization.IssuePlayerColonyOrder(galaxy, player.Id, colonySystem.Id, viableBody.Id);
            Require(accepted.Accepted, "species-relative body-level colony order rejected a viable surveyed body");
            Require(fleet.DestinationSystemId == colonySystem.Id, "accepted body-level order did not set the physical destination system");
            Require(fleet.DestinationPlanetaryBodyId == viableBody.Id, "accepted body-level order did not retain the exact planetary-body target");
            Require(accepted.Message.Contains(viableBody.Name, StringComparison.Ordinal), "accepted colony order did not identify the selected physical world");
            Require(
                accepted.Message.Contains(
                    viableAssessment.Viability == SpeciesColonizationViability.NaturallyViable ? "naturally viable" : "habitat-supported",
                    StringComparison.OrdinalIgnoreCase),
                "accepted colony order did not identify whether viability was natural or compatibility-supported");

            var mission = readModel.Build(galaxy, player.Id).ActiveMissions.First(candidate => candidate.FleetId == fleet.Id);
            Require(mission.TargetPlanetaryBodyId == viableBody.Id, "observer-local mission view did not expose the exact selected body");

            var path = Path.Combine(directory, "body-target-v8.json");
            var save = new CampaignSaveService();
            save.Save(path, galaxy, 500.0);
            var loaded = save.Load(path);
            var loadedFleet = loaded.Galaxy.Fleets.First(candidate => candidate.Id == fleet.Id);
            Require(loadedFleet.DestinationSystemId == colonySystem.Id, "v8 save/load lost colony mission destination system");
            Require(loadedFleet.DestinationPlanetaryBodyId == viableBody.Id, "v8 save/load lost exact colony mission body target");
            Require(loadedFleet.EmbarkedPopulationSpeciesId == player.SpeciesId, "v8 save/load changed colony mission population species");

            loadedFleet.Position = loaded.Galaxy.Systems.First(system => system.Id == colonySystem.Id).Position;
            loadedFleet.CurrentSystemId = colonySystem.Id;
            loadedFleet.DestinationSystemId = null;
            Require(colonization.Advance(loaded.Galaxy).Count == 0, "arrival completed colony construction instantly");
        var events = colonization.Advance(loaded.Galaxy, ColonizationSimulation.ColonyEstablishmentDays);
            var founded = loaded.Galaxy.Colonies.FirstOrDefault(colony =>
                colony.CivilizationId == player.Id && colony.SystemId == colonySystem.Id)
                ?? throw new InvalidOperationException("arrival at the selected species-relative world did not found a colony");

            Require(founded.PlanetaryBodyId == viableBody.Id, "founded colony did not retain the exact selected body ID");
            Require(founded.PopulationSpeciesId == player.SpeciesId, "founded colony changed the transported population species");
            Require(Math.Abs(founded.PopulationMillions - 250.0) < 0.0000001, "body-level settlement founding changed the carried population");
            Require(
                !loadedFleet.IsActive &&
                Math.Abs(loadedFleet.EmbarkedPopulationMillions) < 0.0000001 &&
                loadedFleet.EmbarkedPopulationSpeciesId is null &&
                loadedFleet.DestinationPlanetaryBodyId is null,
                "body-level founding did not consume/clear the colony ship population and body-target payload");
            Require(events.Any(evt => evt.ColonyId == founded.Id && evt.Message.Contains(viableBody.Name, StringComparison.Ordinal)), "colony founding event did not identify the exact physical world");
            Require(colonization.ResolveCompatibilityColonyWorld(loaded.Galaxy, founded)?.Id == viableBody.Id, "founded colony did not resolve to its persisted physical world");

            var postFoundPath = Path.Combine(directory, "founded-body-v8.json");
            save.Save(postFoundPath, loaded.Galaxy, 501.0);
            var reloaded = save.Load(postFoundPath);
            var reloadedColony = reloaded.Galaxy.Colonies.First(colony => colony.Id == founded.Id);
            Require(reloadedColony.PlanetaryBodyId == viableBody.Id, "v8 save/load lost founded colony body identity");
            Require(reloadedColony.PopulationSpeciesId == player.SpeciesId, "v8 save/load lost founded colony population species identity");
        });
    }

    private static void WithTemporaryDirectory(Action<string> action)
    {
        var directory = Path.Combine(Path.GetTempPath(), "stellar-continuum-planetary-validation", Guid.NewGuid().ToString("N"));
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
