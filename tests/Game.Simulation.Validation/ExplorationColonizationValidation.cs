using System.Numerics;
using Game.Persistence;
using Game.Simulation.AI;
using Game.Simulation.Colonization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class ExplorationColonizationValidation
{
    public static void ValidateScoutAndScienceSurveyRoles()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var observer = galaxy.Civilizations.First(civilization => civilization.Id != player.Id);
        var target = galaxy.Systems.FirstOrDefault(system =>
            system.Id != player.HomeSystemId &&
            !galaxy.Knowledge.IsSystemFullySurveyed(player.Id, system.Id) &&
            !galaxy.Knowledge.IsSystemKnown(observer.Id, system.Id))
            ?? throw new InvalidOperationException("validation galaxy did not contain a suitable survey target");

        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        Require(
            galaxy.Knowledge.GetSystemSurveyLevel(player.Id, target.Id) == SystemSurveyLevel.Detected,
            "sensor detection was incorrectly treated as a completed survey");
        Require(
            galaxy.Knowledge.GetSystemSurveyProgress(player.Id, target.Id) == 0.0,
            "newly detected system started with invented survey progress");

        var nextFleetId = galaxy.Fleets.Count == 0 ? 1000 : galaxy.Fleets.Max(fleet => fleet.Id) + 1000;
        var scout = new FleetState
        {
            Id = nextFleetId,
            CivilizationId = player.Id,
            Name = "Validation Scout",
            Role = FleetRole.Scout,
            Position = target.Position,
            CurrentSystemId = target.Id,
            StrategicSpeed = 24.0,
            SensorRange = 140.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(scout);

        var exploration = new ExplorationSimulation();
        exploration.Advance(galaxy, 1.0);
        Require(galaxy.Knowledge.GetSystemSurveyLevel(player.Id, target.Id) == SystemSurveyLevel.Detected,
            "scouting completed before its required on-site time");
        var scoutEvents = exploration.Advance(galaxy, 1.0);

        Require(
            galaxy.Knowledge.GetSystemSurveyLevel(player.Id, target.Id) == SystemSurveyLevel.PartiallySurveyed,
            "scout did not create first-pass reconnaissance knowledge");
        var scoutProgress = galaxy.Knowledge.GetSystemSurveyProgress(player.Id, target.Id);
        Require(
            scoutProgress >= ExplorationSimulation.ScoutReconnaissanceProgress && scoutProgress < 1.0,
            "scout reconnaissance incorrectly completed the detailed survey");
        Require(
            scoutEvents.Any(evt => evt.Type == ExplorationEventType.SystemReconnoitered && evt.SystemId == target.Id),
            "scout reconnaissance did not emit a reconnaissance event");

        scout.IsActive = false;
        var science = new FleetState
        {
            Id = nextFleetId + 1,
            CivilizationId = player.Id,
            Name = "Validation Science Vessel",
            Role = FleetRole.Science,
            Position = target.Position,
            CurrentSystemId = target.Id,
            StrategicSpeed = 18.0,
            SensorRange = 185.0f,
            IsActive = true,
        };
        galaxy.Fleets.Add(science);

        var scienceEvents = exploration.Advance(galaxy, 20.0);
        Require(
            galaxy.Knowledge.IsSystemFullySurveyed(player.Id, target.Id),
            "science vessel did not complete a detailed survey after sufficient deterministic survey time");
        Require(
            Math.Abs(galaxy.Knowledge.GetSystemSurveyProgress(player.Id, target.Id) - 1.0) < 0.0000001,
            "completed science survey did not clamp progress to 100 percent");
        Require(
            scienceEvents.Any(evt => evt.Type == ExplorationEventType.SystemSurveyed && evt.SystemId == target.Id),
            "science survey completion event was not emitted");

        Require(
            galaxy.Knowledge.GetSystemSurveyLevel(observer.Id, target.Id) == SystemSurveyLevel.Unknown,
            "one civilization's survey leaked into another civilization's knowledge");
    }

    public static void ValidateColonizationRequiresFullSurvey()
    {
        var galaxy = CreateValidationGalaxy();
        var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        var target = FindColonizationTarget(galaxy, player.Id);

        // Give the strategic-input adapter interstellar-transit capability so this scenario
        // can specifically prove that colonization opportunity depends on survey depth.
        galaxy.Technologies.First(state => state.CivilizationId == player.Id)
            .CompletedTechnologyIds.Add("prototype_warp_drive");
        var strategicInputs = new CivilizationStrategicInputBuilder();

        galaxy.Knowledge.RevealSystem(player.Id, target.Id);
        Require(
            !strategicInputs.Build(galaxy, player.Id).HasKnownColonizationOpportunity,
            "AI strategic input used authoritative habitability from a merely detected system");

        var colonyFleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 2000 : galaxy.Fleets.Max(fleet => fleet.Id) + 2000,
            CivilizationId = player.Id,
            Name = "Validation Colony Ship",
            Role = FleetRole.Colony,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = 13.5,
            SensorRange = 80.0f,
            IsActive = true,
            EmbarkedPopulationMillions = 250.0,
            EmbarkedPopulationSpeciesId = player.SpeciesId,
        };
        galaxy.Fleets.Add(colonyFleet);

        var colonization = new ColonizationSimulation();
        var rejected = colonization.IssuePlayerColonyOrder(galaxy, player.Id, target.Id);
        Require(!rejected.Accepted, "colonization accepted a merely detected target");
        Require(colonyFleet.DestinationSystemId is null, "rejected colonization order still changed fleet destination");

        galaxy.Knowledge.RecordReconnaissance(player.Id, target.Id);
        Require(
            !strategicInputs.Build(galaxy, player.Id).HasKnownColonizationOpportunity,
            "AI strategic input used authoritative habitability from scout reconnaissance");
        var stillRejected = colonization.IssuePlayerColonyOrder(galaxy, player.Id, target.Id);
        Require(!stillRejected.Accepted, "colonization accepted scout reconnaissance as a full science survey");

        galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.Id);
        Require(
            strategicInputs.Build(galaxy, player.Id).HasKnownColonizationOpportunity,
            "AI strategic input did not expose a legitimate fully surveyed colonization opportunity");
        var accepted = colonization.IssuePlayerColonyOrder(galaxy, player.Id, target.Id);
        Require(accepted.Accepted, "colonization rejected a valid fully surveyed target with an available populated colony ship");
        Require(colonyFleet.DestinationSystemId == target.Id, "accepted colony order did not assign the surveyed target");
    }

    public static void ValidateColonyPopulationConservationAndPersistence()
    {
        WithTemporaryDirectory(directory =>
        {
            var galaxy = CreateValidationGalaxy();
            var player = galaxy.Civilizations.First(civilization => civilization.Id == galaxy.PlayerCivilizationId);
            var source = galaxy.Colonies
                .Where(colony => colony.CivilizationId == player.Id)
                .OrderByDescending(colony => colony.PopulationMillions)
                .First();
            var sourceSpeciesId = source.PopulationSpeciesId;
            Require(SpeciesCatalog.TryGet(sourceSpeciesId, out _), "source colony did not carry a known species identity");
            var target = FindColonizationTarget(galaxy, player.Id);
            var colonyDesign = ShipDesignRegistry.All.First(design => design.Role == FleetRole.Colony);
            var initialSourcePopulation = source.PopulationMillions;
            var initialPopulation = galaxy.Colonies
                .Where(colony => colony.CivilizationId == player.Id)
                .Sum(colony => colony.PopulationMillions);

            var technology = galaxy.Technologies.First(state => state.CivilizationId == player.Id);
            technology.CompletedTechnologyIds.Add("orbital_industry");
            technology.CompletedTechnologyIds.Add("prototype_warp_drive");
            var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == player.Id);
            construction.CompletedProjectIds.Add("orbital_shipyard");
            var economy = galaxy.Economies.First(state => state.CivilizationId == player.Id);
            economy.Industry = colonyDesign.IndustryCost + 50.0;

            var shipbuilding = new ShipbuildingSimulation();
            var buildOrder = shipbuilding.StartBuild(galaxy, player.Id, colonyDesign.Id);
            Require(buildOrder.Accepted, "validation colony ship could not be ordered");
            Require(
                Math.Abs(source.PopulationMillions - (initialSourcePopulation - colonyDesign.PopulationCostMillions)) < 0.0000001 &&
                Math.Abs(galaxy.Colonies.Where(colony => colony.CivilizationId == player.Id).Sum(colony => colony.PopulationMillions) -
                    (initialPopulation - colonyDesign.PopulationCostMillions)) < 0.0000001,
                "colony ship order did not reserve real population from the source colony");

            var shipyard = galaxy.ShipyardStates.First(state => state.CivilizationId == player.Id);
            Require(
                Math.Abs(shipyard.ReservedPopulationMillions - colonyDesign.PopulationCostMillions) < 0.0000001,
                "shipyard did not retain the reserved colonist population while the ship was under construction");
            Require(
                shipyard.ReservedPopulationSpeciesId == sourceSpeciesId,
                "shipyard did not retain the source-colony species identity with reserved colonists");

            shipbuilding.Advance(galaxy, simulationDays: colonyDesign.IndustryCost / ShipbuildingSimulation.IndustryPerDay);
            var fleet = galaxy.Fleets.FirstOrDefault(candidate =>
                candidate.IsActive &&
                candidate.CivilizationId == player.Id &&
                candidate.Role == FleetRole.Colony &&
                candidate.EmbarkedPopulationMillions > 0.0)
                ?? throw new InvalidOperationException("completed colony ship did not become an active populated fleet");

            Require(
                Math.Abs(fleet.EmbarkedPopulationMillions - colonyDesign.PopulationCostMillions) < 0.0000001,
                "completed colony ship lost or changed its reserved population");
            Require(
                fleet.EmbarkedPopulationSpeciesId == sourceSpeciesId,
                "completed colony ship changed the species identity of its reserved colonists");
            Require(
                Math.Abs(shipyard.ReservedPopulationMillions) < 0.0000001 && shipyard.ReservedPopulationSpeciesId is null,
                "shipyard retained population or species identity after transferring colonists to the completed ship");
            Require(
                Math.Abs(PopulationInColoniesAndActiveFleets(galaxy, player.Id) - initialPopulation) < 0.0000001,
                "population was not conserved after colony-ship completion");

            galaxy.Knowledge.RevealSystem(player.Id, target.Id);
            galaxy.Knowledge.AdvanceSystemSurvey(player.Id, target.Id, 0.42);
            fleet.Position = Vector2.Lerp(
                galaxy.Systems.First(system => system.Id == player.HomeSystemId).Position,
                target.Position,
                0.5f);
            fleet.CurrentSystemId = null;
            fleet.DestinationSystemId = target.Id;

            var savePath = Path.Combine(directory, "exploration-colonization-roundtrip.json");
            var saveService = new CampaignSaveService();
            saveService.Save(savePath, galaxy, 412.5);
            var loaded = saveService.Load(savePath);

            Require(
                loaded.Galaxy.Knowledge.GetSystemSurveyLevel(player.Id, target.Id) == SystemSurveyLevel.PartiallySurveyed,
                "save/load lost the partial survey state");
            Require(
                Math.Abs(loaded.Galaxy.Knowledge.GetSystemSurveyProgress(player.Id, target.Id) - 0.42) < 0.0000001,
                "save/load changed mid-survey progress");

            var loadedFleet = loaded.Galaxy.Fleets.First(candidate => candidate.Id == fleet.Id);
            Require(loadedFleet.IsActive, "save/load deactivated an in-transit colony ship");
            Require(loadedFleet.DestinationSystemId == target.Id && loadedFleet.CurrentSystemId is null, "save/load lost colony mission transit state");
            Require(
                Math.Abs(loadedFleet.EmbarkedPopulationMillions - colonyDesign.PopulationCostMillions) < 0.0000001,
                "save/load lost embarked colonists");
            Require(
                loadedFleet.EmbarkedPopulationSpeciesId == sourceSpeciesId,
                "save/load changed the species identity of embarked colonists");
            Require(
                loaded.Galaxy.Colonies.Where(colony => colony.CivilizationId == player.Id)
                    .All(colony => SpeciesCatalog.TryGet(colony.PopulationSpeciesId, out _)),
                "save/load produced a colony with an unknown population species");
            Require(
                Math.Abs(PopulationInColoniesAndActiveFleets(loaded.Galaxy, player.Id) - initialPopulation) < 0.0000001,
                "save/load changed conserved population totals");

            loaded.Galaxy.Knowledge.MarkSystemFullySurveyed(player.Id, target.Id);
            loadedFleet.Position = target.Position;
            loadedFleet.CurrentSystemId = target.Id;
            loadedFleet.DestinationSystemId = null;
            loadedFleet.TransitPhase = FleetTransitPhase.None;

            var colonization = new ColonizationSimulation();
            Require(colonization.Advance(loaded.Galaxy).Count == 0 && loadedFleet.IsActive,
                "colony founded instantly on arrival");
            var events = colonization.Advance(loaded.Galaxy, ColonizationSimulation.ColonyEstablishmentDays);
            var founded = loaded.Galaxy.Colonies.FirstOrDefault(colony =>
                colony.CivilizationId == player.Id && colony.SystemId == target.Id)
                ?? throw new InvalidOperationException("colony ship arrival did not establish the settlement");

            Require(events.Any(evt => evt.FleetId == loadedFleet.Id && evt.ColonyId == founded.Id), "colony founding did not emit a completion event");
            Require(
                Math.Abs(founded.PopulationMillions - colonyDesign.PopulationCostMillions) < 0.0000001,
                "founded colony population did not equal the population physically carried by the colony ship");
            Require(
                founded.PopulationSpeciesId == sourceSpeciesId,
                "founded colony did not retain the species identity of the physically transported colonists");
            Require(
                !loadedFleet.IsActive &&
                Math.Abs(loadedFleet.EmbarkedPopulationMillions) < 0.0000001 &&
                loadedFleet.EmbarkedPopulationSpeciesId is null,
                "founded colony did not consume/deactivate the colony fleet population payload and identity");
            Require(
                Math.Abs(PopulationInColoniesAndActiveFleets(loaded.Galaxy, player.Id) - initialPopulation) < 0.0000001,
                "colony founding created or destroyed population");
        });
    }

    private static StarSystemState FindColonizationTarget(GalaxyState galaxy, int civilizationId) =>
        galaxy.Systems.FirstOrDefault(system =>
            system.Id != galaxy.Civilizations.First(civilization => civilization.Id == civilizationId).HomeSystemId &&
            system.HasHabitableWorld &&
            !system.HasPreWarpCivilization &&
            !galaxy.Colonies.Any(colony => colony.SystemId == system.Id))
        ?? throw new InvalidOperationException("validation galaxy did not contain an unoccupied habitable target");

    private static double PopulationInColoniesAndActiveFleets(GalaxyState galaxy, int civilizationId) =>
        galaxy.Colonies.Where(colony => colony.CivilizationId == civilizationId).Sum(colony => colony.PopulationMillions) +
        galaxy.Fleets.Where(fleet => fleet.IsActive && fleet.CivilizationId == civilizationId).Sum(fleet => fleet.EmbarkedPopulationMillions);

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x4558_504C_4F52_45L,
            new GalaxyGenerationSettings
            {
                SystemCount = 48,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 520.0f,
            });

    private static void WithTemporaryDirectory(Action<string> action)
    {
        var directory = Path.Combine(Path.GetTempPath(), "stellar-continuum-exploration-validation", Guid.NewGuid().ToString("N"));
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
