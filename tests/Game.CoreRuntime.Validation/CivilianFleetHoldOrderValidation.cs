using System;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.Colonization;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.CoreRuntime.Validation;

internal static class CivilianFleetHoldOrderValidation
{
    internal static void Run()
    {
        HoldCompletesOneLaneThenRetainsRoute();
        HeldPartialScoutAndScienceWorkResume();
        PaidColonyHoldPreservesStartedSettlementAndAuthorization();
        RejectsForeignInactiveAndUnsupportedShips();
        ReturnToBaseUsesPhysicalRouteAndFailsSafe();
        ReturnChoosesShortestSupportedLaneRouteAndOutpostsRefuelPartially();
        ExplicitCourseOverridesPendingReturn();
        PaidColonyQueuedReturnLosesBaseSafely();
    }

    private static void HoldCompletesOneLaneThenRetainsRoute()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var homeId = galaxy.Colonies.First(colony => colony.CivilizationId == player).SystemId;
        var systems = galaxy.Systems.Where(system => system.Id != homeId).Take(3).ToArray();
        var scout = Fleet(player, 99001, FleetRole.Scout, systems[0], strategicSpeed: 10);
        scout.DestinationSystemId = systems[2].Id;
        scout.PlannedRouteSystemIds.Add(systems[1].Id);
        scout.PlannedRouteSystemIds.Add(systems[2].Id);
        var initialPosition = scout.Position;
        var initialFuel = scout.FuelRemainingLightYears;
        var firstLeg = Vector2.Distance(initialPosition, systems[1].Position);
        var secondLeg = Vector2.Distance(systems[1].Position, systems[2].Position);
        var departureDays = FleetLocalTransit.GateRadius / FleetLocalTransit.Rate(scout);
        galaxy.Fleets.Add(scout);
        var exploration = new ExplorationSimulation();
        var coordinator = new GalaxySimulationStepCoordinator();

        exploration.Advance(galaxy, departureDays + firstLeg / scout.StrategicSpeed / 4);
        RequirePosition(scout.Position, Vector2.Lerp(initialPosition, systems[1].Position, .25f),
            "partial transit did not move incrementally along the current lane");
        RequireNear(scout.FuelRemainingLightYears, initialFuel - firstLeg / 4,
            "partial transit did not debit exact fuel");
        Require(scout.CurrentSystemId is null && scout.DestinationSystemId == systems[2].Id &&
                scout.PlannedRouteSystemIds.SequenceEqual(new[] { systems[1].Id, systems[2].Id }),
            "partial transit changed the represented route before hold issuance");

        Require(coordinator.IssueCivilianHoldOrder(galaxy, player, scout.Id).Accepted, "transit hold rejected");
        var heldStatus = new ExplorationMissionStatusEvaluator().Build(galaxy, scout);
        Require(heldStatus.Phase == ExplorationMissionPhase.Traveling &&
                heldStatus.Summary.Contains("holding after reaching", StringComparison.Ordinal),
            "mid-lane hold status did not explain the one-lane stopping rule");
        AssertSaveState(galaxy, scout.Id, "mid-lane hold did not persist");
        AssertLegacyMissingHoldDefaultsFalse(galaxy, scout.Id);

        exploration.Advance(galaxy, firstLeg / scout.StrategicSpeed / 4);
        RequirePosition(scout.Position, Vector2.Lerp(initialPosition, systems[1].Position, .5f),
            "held in-transit scout stopped before reaching its lane endpoint");
        RequireNear(scout.FuelRemainingLightYears, initialFuel - firstLeg / 2,
            "held incremental movement did not debit exact fuel");
        Require(scout.CurrentSystemId is null && scout.HoldRequested &&
                scout.PlannedRouteSystemIds.SequenceEqual(new[] { systems[1].Id, systems[2].Id }),
            "held incremental movement changed the remaining route");

        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        economy.LastBaseOperationsFundingFraction = 0;
        var unfundedStatus = new ExplorationMissionStatusEvaluator().Build(galaxy, scout);
        var unfundedSnapshot = Snapshot(galaxy, scout.Id);
        Require(unfundedStatus.Phase == ExplorationMissionPhase.AwaitingOrder &&
                unfundedStatus.Summary.Contains("unfunded", StringComparison.OrdinalIgnoreCase),
            "held mission status did not disclose the no-funding suspension");
        exploration.Advance(galaxy, 1000);
        AssertSnapshot(Snapshot(galaxy, scout.Id), unfundedSnapshot,
            "unfunded held transit mutated before operating funding was restored");
        economy.LastBaseOperationsFundingFraction = 1;
        Require(scout.HoldRequested, "restoring operating funding cancelled the civilian hold");

        exploration.Advance(galaxy, 10_000);
        RequirePosition(scout.Position, systems[1].Position,
            "held transit did not stop at the current lane endpoint");
        Require(scout.CurrentSystemId == systems[1].Id && scout.DestinationSystemId == systems[2].Id &&
                scout.TransitPhase == FleetTransitPhase.LocalArrival &&
                scout.PlannedRouteSystemIds.SequenceEqual(new[] { systems[1].Id, systems[2].Id }),
            "hold did not remain at the inbound gate of its current lane");
        RequireNear(scout.FuelRemainingLightYears, initialFuel - firstLeg,
            "held lane completion did not debit exact physical fuel");
        AssertSaveState(galaxy, scout.Id, "intermediate held stop did not persist");

        Require(coordinator.IssueCivilianResumeOrder(galaxy, player, scout.Id).Accepted, "transit resume rejected");
        exploration.Advance(galaxy, 10_000);
        RequirePosition(scout.Position, systems[2].Position,
            "resumed scout did not reach its retained final destination");
        Require(scout.CurrentSystemId == systems[2].Id && scout.DestinationSystemId is null &&
                scout.PlannedRouteSystemIds.Count == 0 && !scout.HoldRequested,
            "resumed scout did not complete and clear its retained route");
        RequireNear(scout.FuelRemainingLightYears, initialFuel - firstLeg - secondLeg,
            "resumed route did not debit exact remaining physical fuel");
    }

    private static void HeldPartialScoutAndScienceWorkResume()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var homeId = galaxy.Colonies.First(colony => colony.CivilizationId == player).SystemId;
        var systems = galaxy.Systems.Where(system => system.Id != homeId).Take(2).ToArray();
        var scout = Fleet(player, 99002, FleetRole.Scout, systems[0]);
        var science = Fleet(player, 99003, FleetRole.Science, systems[1]);
        galaxy.Fleets.Add(scout);
        galaxy.Fleets.Add(science);
        var exploration = new ExplorationSimulation();
        var coordinator = new GalaxySimulationStepCoordinator();

        exploration.Advance(galaxy, .5);
        var scoutProgress = scout.ReconnaissanceDaysCompleted;
        var scienceProgress = galaxy.Knowledge.GetSystemSurveyProgress(player, systems[1].Id);
        Require(scout.ReconnaissanceSystemId == systems[0].Id && scoutProgress > 0 &&
                scoutProgress < ExplorationSimulation.ScoutReconnaissanceDays,
            "scout fixture did not establish nonzero partial reconnaissance");
        Require(scienceProgress > 0 && scienceProgress < 1,
            "science fixture did not establish nonzero partial survey progress");
        Require(coordinator.IssueCivilianHoldOrder(galaxy, player, scout.Id).Accepted &&
                coordinator.IssueCivilianHoldOrder(galaxy, player, science.Id).Accepted,
            "partial local-work hold was rejected");
        AssertSaveState(galaxy, scout.Id, "held partial scout work did not survive two save generations");
        AssertSaveState(galaxy, science.Id, "held partial science work did not survive two save generations");

        exploration.Advance(galaxy, 5);
        RequireNear(scout.ReconnaissanceDaysCompleted, scoutProgress, "held scout performed local work");
        RequireNear(galaxy.Knowledge.GetSystemSurveyProgress(player, systems[1].Id), scienceProgress,
            "held science vessel performed local work");

        Require(coordinator.IssueCivilianResumeOrder(galaxy, player, scout.Id).Accepted, "scout resume rejected");
        exploration.Advance(galaxy, .25);
        Require(scout.ReconnaissanceDaysCompleted > scoutProgress &&
                Math.Abs(galaxy.Knowledge.GetSystemSurveyProgress(player, systems[1].Id) - scienceProgress) < .0001,
            "resuming scout did not advance only its retained partial local work");
        Require(coordinator.IssueCivilianResumeOrder(galaxy, player, science.Id).Accepted, "science resume rejected");
        exploration.Advance(galaxy, .25);
        Require(galaxy.Knowledge.GetSystemSurveyProgress(player, systems[1].Id) > scienceProgress,
            "resumed science vessel did not continue its partial survey");
    }

    private static void PaidColonyHoldPreservesStartedSettlementAndAuthorization()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        foreach (var system in galaxy.Systems) galaxy.Knowledge.MarkSystemFullySurveyed(player, system.Id);
        var home = galaxy.Systems.First(system => system.Id ==
            galaxy.Colonies.First(colony => colony.CivilizationId == player).SystemId);
        var colony = Fleet(player, 99004, FleetRole.Colony, home);
        colony.EmbarkedPopulationMillions = 2.5;
        colony.EmbarkedPopulationSpeciesId = galaxy.Civilizations.Single(c => c.Id == player).SpeciesId;
        galaxy.Fleets.Add(colony);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        economy.Credits = 10000;
        var colonization = new ColonizationSimulation();
        var target = colonization.GetOpportunityPlan(galaxy, colony.Id, 64).Candidates.First(candidate => candidate.CanOrder);
        var beforeCredits = economy.Credits;
        var coordinator = new GalaxySimulationStepCoordinator();
        Require(coordinator.IssueColonyFleetOrder(galaxy, player, colony.Id, target.SystemId, target.PlanetaryBodyId).Accepted,
            "paid colony authorization rejected");
        RequireNear(economy.Credits, beforeCredits - ColonizationSimulation.ColonyExpeditionCreditCost,
            "colony authorization did not debit its quoted fee");

        var targetSystem = galaxy.Systems.Single(system => system.Id == target.SystemId);
        colony.CurrentSystemId = null;
        colony.Position = targetSystem.Position;
        colony.PlannedRouteSystemIds.Clear();
        colony.PlannedRouteSystemIds.Add(target.SystemId);
        Require(coordinator.IssueCivilianHoldOrder(galaxy, player, colony.Id).Accepted, "colony hold rejected");
        var exploration = new ExplorationSimulation();
        exploration.Advance(galaxy, 1);
        Require(colony.CurrentSystemId == target.SystemId && colony.DestinationSystemId == target.SystemId &&
                colony.DestinationPlanetaryBodyId == target.PlanetaryBodyId && colony.EmbarkedPopulationMillions == 2.5,
            "held final arrival lost its pending colony authorization or passengers");
        AssertSaveState(galaxy, colony.Id, "held final colony arrival did not persist");

        Require(coordinator.IssueCivilianResumeOrder(galaxy, player, colony.Id).Accepted, "colony resume rejected");
        exploration.Advance(galaxy, 1);
        colonization.Advance(galaxy, 1);
        colonization.Advance(galaxy, 4);
        Require(colony.SettlementBodyId == target.PlanetaryBodyId && colony.SettlementDaysCompleted > 0,
            "resumed colony did not establish nonzero paid settlement progress");
        var startedProgress = colony.SettlementDaysCompleted;
        Require(coordinator.IssueCivilianHoldOrder(galaxy, player, colony.Id).Accepted, "started settlement hold rejected");
        AssertSaveState(galaxy, colony.Id, "held nonzero settlement progress or paid mission state did not persist");
        colonization.Advance(galaxy, 3);
        RequireNear(colony.SettlementDaysCompleted, startedProgress, "held settlement progressed");
        RequireNear(economy.Credits, beforeCredits - ColonizationSimulation.ColonyExpeditionCreditCost,
            "hold changed paid authorization treasury");
        Require(colony.EmbarkedPopulationMillions == 2.5 &&
                colony.EmbarkedPopulationSpeciesId == galaxy.Civilizations.Single(c => c.Id == player).SpeciesId,
            "hold changed embarked colonists or their species");
        Require(coordinator.IssueCivilianResumeOrder(galaxy, player, colony.Id).Accepted, "started settlement resume rejected");
        colonization.Advance(galaxy, 3);
        RequireNear(colony.SettlementDaysCompleted, startedProgress + 3,
            "resumed settlement did not continue from its exact paid progress");
        var beforeReturnProgress = colony.SettlementDaysCompleted;
        var beforeReturnTreasury = economy.Credits;
        var missionRevision = colony.MissionOrderRevision;
        var previewBeforeProgress = coordinator.PreviewCivilianReturnToBase(galaxy, player, colony.Id);
        colonization.Advance(galaxy, 1);
        Require(colony.SettlementDaysCompleted > beforeReturnProgress && colony.MissionOrderRevision == missionRevision,
            "settlement time changed the stable colony mission identity used by return confirmation");
        var preview = coordinator.PreviewCivilianReturnToBase(galaxy, player, colony.Id);
        Require(preview.RequiresConfirmation && previewBeforeProgress.RequiresConfirmation &&
                preview.Message != previewBeforeProgress.Message &&
                preview.Message.Contains($"{colony.SettlementDaysCompleted:0.#} days", StringComparison.Ordinal),
            "return preview did not refresh and display current paid settlement progress");
        Require(coordinator.IssueCivilianReturnToBaseOrder(galaxy, player, colony.Id).RequiresConfirmation &&
            colony.SettlementDaysCompleted > beforeReturnProgress && colony.EmbarkedPopulationMillions == 2.5 &&
            economy.Credits == beforeReturnTreasury, "unconfirmed paid colony return mutated authorization state");
        Require(coordinator.IssueCivilianReturnToBaseOrder(galaxy, player, colony.Id, confirmAbandonColonyWork: true).Accepted &&
            colony.SettlementBodyId is null && colony.DestinationPlanetaryBodyId is null && colony.SettlementDaysCompleted == 0 &&
            colony.EmbarkedPopulationMillions == 2.5 && colony.EmbarkedPopulationSpeciesId == galaxy.Civilizations.Single(c => c.Id == player).SpeciesId &&
            economy.Credits == beforeReturnTreasury, "confirmed paid colony return changed passengers, species, or treasury beyond the no-refund abandonment");
    }

    private static void RejectsForeignInactiveAndUnsupportedShips()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var system = galaxy.Systems[0];
        var foreign = Fleet(player + 999, 99005, FleetRole.Scout, system);
        var military = Fleet(player, 99006, FleetRole.Military, system);
        var inactive = Fleet(player, 99007, FleetRole.Science, system);
        inactive.IsActive = false;
        galaxy.Fleets.Add(foreign);
        galaxy.Fleets.Add(military);
        galaxy.Fleets.Add(inactive);
        var coordinator = new GalaxySimulationStepCoordinator();
        Require(!coordinator.IssueCivilianHoldOrder(galaxy, player, foreign.Id).Accepted && !foreign.HoldRequested,
            "foreign hold mutated state");
        Require(!coordinator.IssueCivilianHoldOrder(galaxy, player, military.Id).Accepted && !military.HoldRequested,
            "military hold mutated state");
        Require(!coordinator.IssueCivilianHoldOrder(galaxy, player, inactive.Id).Accepted && !inactive.HoldRequested,
            "inactive hold mutated state");

        var persistenceGalaxy = CreateGalaxy();
        var persistencePlayer = persistenceGalaxy.PlayerCivilizationId;
        var persistedInactive = Fleet(persistencePlayer, 99007, FleetRole.Science, persistenceGalaxy.Systems[0]);
        persistedInactive.IsActive = false;
        persistedInactive.HoldRequested = true;
        persistenceGalaxy.Fleets.Add(persistedInactive);
        AssertSaveState(persistenceGalaxy, persistedInactive.Id,
            "deactivated held civilian ship made the campaign unsaveable");
    }

    private static void ReturnToBaseUsesPhysicalRouteAndFailsSafe()
    {
        var galaxy = CreateGalaxy(); var player = galaxy.PlayerCivilizationId;
        var home = galaxy.Colonies.First(colony => colony.CivilizationId == player);
        var systems = galaxy.Systems.Where(system => system.Id != home.SystemId).Take(2).ToArray();
        var alreadyHome = Fleet(player, 99009, FleetRole.Scout, galaxy.Systems.Single(system => system.Id == home.SystemId));
        alreadyHome.DestinationSystemId = systems[0].Id; alreadyHome.PlannedRouteSystemIds.Add(systems[0].Id);
        galaxy.Fleets.Add(alreadyHome);
        var coordinator = new GalaxySimulationStepCoordinator();
        Require(coordinator.IssueCivilianReturnToBaseOrder(galaxy, player, alreadyHome.Id).Accepted &&
            alreadyHome.DestinationSystemId is null && alreadyHome.PlannedRouteSystemIds.Count == 0,
            "return from an owned base did not end its prior course");
        var scout = Fleet(player, 99008, FleetRole.Scout, systems[0], strategicSpeed: 10);
        scout.Position = (systems[0].Position + systems[1].Position) / 2;
        scout.CurrentSystemId = null; scout.DestinationSystemId = systems[1].Id;
        scout.PlannedRouteSystemIds.Add(systems[1].Id);
        galaxy.Fleets.Add(scout);
        Require(coordinator.IssueCivilianReturnToBaseOrder(galaxy, player, scout.Id).Accepted && scout.ReturnToBaseRequested,
            "mid-lane return intent was not accepted");
        var path = Path.Combine(Path.GetTempPath(), $"stellar-return-{Guid.NewGuid():N}.json");
        try
        {
            new CampaignSaveService().Save(path, galaxy, 1);
            Require(new CampaignSaveService().Load(path).Galaxy.Fleets.Single(fleet => fleet.Id == scout.Id).ReturnToBaseRequested,
                "queued return intent did not persist");
        }
        finally { DeleteSave(path); }
        var returnPosition = scout.Position;
        var returnFuel = scout.FuelRemainingLightYears;
        galaxy.Economies.Single(item => item.CivilizationId == player).LastBaseOperationsFundingFraction = 0;
        new ExplorationSimulation().Advance(galaxy, 1000);
        RequirePosition(scout.Position, returnPosition, "unfunded queued return moved without operations funding");
        RequireNear(scout.FuelRemainingLightYears, returnFuel, "unfunded queued return consumed fuel");
        galaxy.Economies.Single(item => item.CivilizationId == player).LastBaseOperationsFundingFraction = 1;
        var remainingFirstLane = Vector2.Distance(scout.Position, systems[1].Position);
        new ExplorationSimulation().Advance(galaxy, 10000);
        Require(scout.CurrentSystemId == systems[1].Id && scout.DestinationSystemId == home.SystemId && scout.ReturnToBaseRequested,
            "queued return did not wait for the current lane then assign a physical base route");
        var afterFirstLaneFuel = scout.FuelRemainingLightYears;
        RequireNear(afterFirstLaneFuel, returnFuel - remainingFirstLane,
            "queued return did not consume the exact positive remaining-lane fuel");
        new ExplorationSimulation().Advance(galaxy, 10000);
        Require(scout.CurrentSystemId == home.SystemId && scout.DestinationSystemId is null && !scout.ReturnToBaseRequested &&
            scout.FuelRemainingLightYears == scout.FuelCapacityLightYears && afterFirstLaneFuel <= scout.FuelCapacityLightYears,
            "physical return did not arrive and refuel only at the owned full-service colony");
        Require(coordinator.IssueCivilianReturnToBaseOrder(galaxy, player + 1, scout.Id).Accepted == false,
            "foreign return command was accepted");
        galaxy.Colonies.Where(colony => colony.CivilizationId == player).ToList().ForEach(colony => galaxy.Colonies.Remove(colony));
        var atomic = Fleet(player, 99010, FleetRole.Science, systems[1]);
        atomic.DestinationSystemId = systems[0].Id; atomic.PlannedRouteSystemIds.Add(systems[0].Id); galaxy.Fleets.Add(atomic);
        Require(!coordinator.IssueCivilianReturnToBaseOrder(galaxy, player, atomic.Id).Accepted && !atomic.HoldRequested &&
            !atomic.ReturnToBaseRequested && atomic.ReturnToBaseFailureReason is null && atomic.DestinationSystemId == systems[0].Id,
            "at-system unreachable return was not rejected atomically");
        scout.CurrentSystemId = systems[1].Id; scout.Position = systems[1].Position;
        scout.DestinationSystemId = systems[0].Id; scout.PlannedRouteSystemIds = new() { systems[0].Id };
        scout.ReturnToBaseRequested = true;
        new ExplorationSimulation().Advance(galaxy, 10000);
        Require(scout.HoldRequested && !scout.ReturnToBaseRequested && scout.ReturnToBaseFailureReason is not null,
            "lost base did not convert queued return into a safe held recovery state");
        AssertSaveState(galaxy, scout.Id, "recovery-held return failure did not survive save/load");
        var inactive = Fleet(player, 99011, FleetRole.Science, systems[0]); inactive.IsActive = false;
        inactive.ReturnToBaseRequested = true; galaxy.Fleets.Add(inactive);
        AssertSaveState(galaxy, inactive.Id, "inactive return intent was not inert/save-compatible");
        var legacyPath = Path.Combine(Path.GetTempPath(), $"stellar-return-legacy-{Guid.NewGuid():N}.json");
        try
        {
            var saves = new CampaignSaveService(); saves.Save(legacyPath, galaxy, 1);
            var payload = JsonNode.Parse(File.ReadAllText(legacyPath))!;
            var dto = payload["Galaxy"]!["Fleets"]!.AsArray().OfType<JsonObject>().Single(item => item["Id"]!.GetValue<int>() == inactive.Id);
            dto.Remove("ReturnToBaseRequested"); dto.Remove("ReturnToBaseFailureReason"); File.WriteAllText(legacyPath, payload.ToJsonString());
            var restored = saves.Load(legacyPath).Galaxy.Fleets.Single(fleet => fleet.Id == inactive.Id);
            Require(!restored.ReturnToBaseRequested && restored.ReturnToBaseFailureReason is null, "missing legacy return fields did not default inert");
        }
        finally { DeleteSave(legacyPath); }
    }

    private static void ReturnChoosesShortestSupportedLaneRouteAndOutpostsRefuelPartially()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        galaxy.Colonies.Where(colony => colony.CivilizationId == player).ToList().ForEach(colony => galaxy.Colonies.Remove(colony));
        var lanes = new InterstellarLaneNetwork();
        var systems = galaxy.Systems.ToArray();
        (StarSystemState Origin, StarSystemState EuclideanBase, StarSystemState RouteBase, double EuclideanRoute, double ShortRoute)? fixture = null;
        foreach (var candidateOrigin in systems)
        foreach (var near in systems.Where(system => system.Id != candidateOrigin.Id))
        foreach (var far in systems.Where(system => system.Id != candidateOrigin.Id && system.Id != near.Id))
        {
            if (Vector2.Distance(candidateOrigin.Position, near.Position) >= Vector2.Distance(candidateOrigin.Position, far.Position)) continue;
            var nearRoute = lanes.FindShortestRoute(galaxy.Systems, candidateOrigin.Id, near.Id, 10_000);
            var farRoute = lanes.FindShortestRoute(galaxy.Systems, candidateOrigin.Id, far.Id, 10_000);
            var nearDistance = RouteDistance(galaxy, nearRoute);
            var farDistance = RouteDistance(galaxy, farRoute);
            if (nearDistance > farDistance + .001)
            {
                fixture = (candidateOrigin, near, far, nearDistance, farDistance);
                break;
            }
        }
        Require(fixture is not null, "deterministic galaxy lacked the required Euclidean-versus-lane return fixture");
        var selected = fixture!.Value;
        galaxy.Colonies.Add(Settlement(player, 99100, selected.EuclideanBase, SettlementKind.Colony));
        galaxy.Colonies.Add(Settlement(player, 99101, selected.RouteBase, SettlementKind.Colony));
        var fleet = Fleet(player, 99102, FleetRole.Scout, selected.Origin);
        galaxy.Fleets.Add(fleet);
        var result = new GalaxySimulationStepCoordinator().IssueCivilianReturnToBaseOrder(galaxy, player, fleet.Id);
        Require(result.Accepted && fleet.DestinationSystemId == selected.RouteBase.Id &&
                selected.EuclideanRoute > selected.ShortRoute &&
                Vector2.Distance(selected.Origin.Position, selected.EuclideanBase.Position) <
                Vector2.Distance(selected.Origin.Position, selected.RouteBase.Position),
            "return selected Euclidean proximity instead of the shortest supported lane route");

        var outpostGalaxy = CreateGalaxy();
        var outpostPlayer = outpostGalaxy.PlayerCivilizationId;
        var outpost = outpostGalaxy.Colonies.First(colony => colony.CivilizationId == outpostPlayer);
        outpost.Kind = SettlementKind.ResourceOutpost;
        outpostGalaxy.Colonies.Where(colony => colony.CivilizationId == outpostPlayer && colony.Id != outpost.Id)
            .ToList().ForEach(colony => outpostGalaxy.Colonies.Remove(colony));
        var origin = outpostGalaxy.Systems.First(system => system.Id != outpost.SystemId);
        var outpostSystem = outpostGalaxy.Systems.Single(system => system.Id == outpost.SystemId);
        var returning = Fleet(outpostPlayer, 99103, FleetRole.Science, origin);
        var reach = new LaneInterstellarOperationalReachView().Assess(outpostGalaxy, outpostPlayer, returning,
            outpost.SystemId, InterstellarMissionKind.ScienceSurvey);
        Require(reach.IsSupported && reach.RouteDistanceLightYears > 0, "outpost return fixture had no physical route");
        returning.FuelRemainingLightYears = reach.RouteDistanceLightYears + 1;
        outpostGalaxy.Fleets.Add(returning);
        Require(new GalaxySimulationStepCoordinator().IssueCivilianReturnToBaseOrder(outpostGalaxy, outpostPlayer, returning.Id).Accepted,
            "return to owned outpost was rejected");
        var simulation = new ExplorationSimulation();
        for (var leg = 0; leg < outpostGalaxy.Systems.Count && returning.DestinationSystemId is not null; leg++)
            simulation.Advance(outpostGalaxy, 10_000);
        Require(returning.CurrentSystemId == outpostSystem.Id && returning.DestinationSystemId is null &&
                !returning.ReturnToBaseRequested,
            $"return did not physically arrive at the owned outpost (current={returning.CurrentSystemId}, destination={returning.DestinationSystemId}, pending={returning.ReturnToBaseRequested}, fuel={returning.FuelRemainingLightYears:0.###})");
        RequireNear(returning.FuelRemainingLightYears, returning.FuelCapacityLightYears * .5,
            "owned outpost did not provide exactly its partial refuel service");
    }

    private static void ExplicitCourseOverridesPendingReturn()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var homeId = galaxy.Colonies.First(colony => colony.CivilizationId == player).SystemId;
        var systems = galaxy.Systems.Where(system => system.Id != homeId).Take(2).ToArray();
        var fleet = Fleet(player, 99104, FleetRole.Scout, systems[0]);
        fleet.ReturnToBaseRequested = true;
        fleet.ReturnToBaseFailureReason = "Earlier recovery route unavailable.";
        fleet.HoldRequested = true;
        galaxy.Fleets.Add(fleet);
        var result = new ExplorationSimulation().IssueTravelOrder(galaxy, fleet.Id, systems[1].Id);
        Require(result.Accepted && fleet.DestinationSystemId == systems[1].Id && fleet.PlannedRouteSystemIds.Count > 0 &&
                !fleet.ReturnToBaseRequested && fleet.ReturnToBaseFailureReason is null && !fleet.HoldRequested,
            "an accepted explicit course did not replace the pending return and recovery hold");
    }

    private static void PaidColonyQueuedReturnLosesBaseSafely()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var home = galaxy.Colonies.First(colony => colony.CivilizationId == player);
        var origin = galaxy.Systems.First(system => system.Id != home.SystemId);
        var endpoint = galaxy.Systems.First(system => system.Id != home.SystemId && system.Id != origin.Id);
        var body = galaxy.PlanetaryBodies.First(candidate => candidate.SystemId == endpoint.Id);
        var colony = Fleet(player, 99105, FleetRole.Colony, origin, strategicSpeed: 10);
        colony.DestinationSystemId = endpoint.Id;
        colony.DestinationPlanetaryBodyId = body.Id;
        colony.PlannedRouteSystemIds.Add(endpoint.Id);
        colony.CurrentSystemId = null;
        colony.Position = Vector2.Lerp(origin.Position, endpoint.Position, .5f);
        colony.EmbarkedPopulationMillions = 3.25;
        colony.EmbarkedPopulationSpeciesId = galaxy.Civilizations.Single(c => c.Id == player).SpeciesId;
        galaxy.Fleets.Add(colony);
        var coordinator = new GalaxySimulationStepCoordinator();
        Require(coordinator.IssueCivilianReturnToBaseOrder(galaxy, player, colony.Id).RequiresConfirmation,
            "paid in-transit colony return did not require confirmation");
        Require(coordinator.IssueCivilianReturnToBaseOrder(galaxy, player, colony.Id, true).Accepted && colony.ReturnToBaseRequested,
            "confirmed paid in-transit colony return was rejected");
        AssertSaveState(galaxy, colony.Id, "pending paid colony return did not persist");
        var beforePosition = colony.Position;
        var beforeFuel = colony.FuelRemainingLightYears;
        var remainingLane = Vector2.Distance(beforePosition, endpoint.Position);
        galaxy.Colonies.Where(candidate => candidate.CivilizationId == player).ToList().ForEach(candidate => galaxy.Colonies.Remove(candidate));
        new ExplorationSimulation().Advance(galaxy, 10_000);
        Require(colony.CurrentSystemId == endpoint.Id && colony.HoldRequested && !colony.ReturnToBaseRequested &&
                colony.ReturnToBaseFailureReason is not null && colony.DestinationSystemId == endpoint.Id &&
                colony.DestinationPlanetaryBodyId == body.Id && colony.EmbarkedPopulationMillions == 3.25,
            "lost base did not safe-hold the paid colony at its original final lane endpoint with authorization intact");
        RequireNear(colony.FuelRemainingLightYears, beforeFuel - remainingLane,
            "paid colony final lane did not consume exact positive fuel before safe hold");
        AssertSaveState(galaxy, colony.Id, "failed paid colony return did not persist its authorization and safe hold");
    }

    private static double RouteDistance(GalaxyState galaxy, System.Collections.Generic.IReadOnlyList<int> route)
    {
        var byId = galaxy.Systems.ToDictionary(system => system.Id);
        return route.Zip(route.Skip(1), (first, second) =>
            (double)Vector2.Distance(byId[first].Position, byId[second].Position)).Sum();
    }

    private static ColonyState Settlement(int owner, int id, StarSystemState system, SettlementKind kind) => new()
    {
        Id = id, CivilizationId = owner, SystemId = system.Id,
        PlanetaryBodyId = null, Name = $"Return base {id}", Kind = kind,
    };

    private static FleetState Fleet(int owner, int id, FleetRole role, StarSystemState system,
        double strategicSpeed = 1000) => new()
    {
        Id = id, CivilizationId = owner, Name = $"Hold {role}", Role = role, Position = system.Position,
        CurrentSystemId = system.Id, StrategicSpeed = strategicSpeed, MaximumLegRangeLightYears = 10000,
        FuelCapacityLightYears = 10000, FuelRemainingLightYears = 10000, SensorRange = 100,
    };

    private static GalaxyState CreateGalaxy() => new GalaxyGenerator().Generate(91473,
        // Keep a real second civilization and colony so the lost-player-base recovery case
        // remains a structurally valid modern save after its player settlement is removed.
        new GalaxyGenerationSettings { SystemCount = 12, PreWarpCivilizationCount = 2, AncientCivilizationCount = 0, Radius = 200 });

    private static void AssertSaveState(GalaxyState galaxy, int fleetId, string message)
    {
        var firstPath = Path.Combine(Path.GetTempPath(), $"stellar-hold-first-{Guid.NewGuid():N}.json");
        var secondPath = Path.Combine(Path.GetTempPath(), $"stellar-hold-second-{Guid.NewGuid():N}.json");
        try
        {
            var expected = Snapshot(galaxy, fleetId);
            var saves = new CampaignSaveService();
            saves.Save(firstPath, galaxy, 1.25);
            var first = saves.Load(firstPath).Galaxy;
            AssertSnapshot(Snapshot(first, fleetId), expected, message + " (first generation)");
            saves.Save(secondPath, first, 2.5);
            var second = saves.Load(secondPath).Galaxy;
            AssertSnapshot(Snapshot(second, fleetId), expected, message + " (second generation)");
        }
        finally { DeleteSave(firstPath); DeleteSave(secondPath); }
    }

    private static void AssertLegacyMissingHoldDefaultsFalse(GalaxyState galaxy, int fleetId)
    {
        var path = Path.Combine(Path.GetTempPath(), $"stellar-hold-legacy-{Guid.NewGuid():N}.json");
        try
        {
            var expected = Snapshot(galaxy, fleetId) with { HoldRequested = false };
            var saves = new CampaignSaveService();
            saves.Save(path, galaxy, 3.75);
            var legacy = JsonNode.Parse(File.ReadAllText(path))!;
            legacy["Galaxy"]!["Fleets"]!.AsArray().OfType<JsonObject>()
                .Single(item => item["Id"]!.GetValue<int>() == fleetId).Remove("HoldRequested");
            File.WriteAllText(path, legacy.ToJsonString());
            AssertSnapshot(Snapshot(saves.Load(path).Galaxy, fleetId), expected,
                "missing legacy hold field did not default false without changing the mission");
        }
        finally { DeleteSave(path); }
    }

    private static PersistedFleetSnapshot Snapshot(GalaxyState galaxy, int fleetId)
    {
        var fleet = galaxy.Fleets.Single(candidate => candidate.Id == fleetId);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == fleet.CivilizationId);
        var surveyProgress = fleet.CurrentSystemId is int systemId
            ? galaxy.Knowledge.GetSystemSurveyProgress(fleet.CivilizationId, systemId) : (double?)null;
        return new PersistedFleetSnapshot(fleet.Id, fleet.CivilizationId, fleet.Role, fleet.Position,
            fleet.CurrentSystemId, fleet.DestinationSystemId, fleet.PlannedRouteSystemIds.ToArray(),
            fleet.FuelRemainingLightYears, fleet.HoldRequested, fleet.IsActive, fleet.DestinationPlanetaryBodyId,
            fleet.PreventAutomaticSettlement, fleet.SettlementBodyId, fleet.SettlementDaysCompleted,
            fleet.ReconnaissanceSystemId, fleet.ReconnaissanceDaysCompleted, fleet.EmbarkedPopulationMillions,
            fleet.EmbarkedPopulationSpeciesId, fleet.ReturnToBaseRequested, fleet.ReturnToBaseFailureReason,
            fleet.MissionOrderRevision, surveyProgress, economy.Credits, economy.LastBaseOperationsFundingFraction);
    }

    private static void AssertSnapshot(PersistedFleetSnapshot actual, PersistedFleetSnapshot expected, string message)
    {
        Require(actual.Id == expected.Id && actual.CivilizationId == expected.CivilizationId &&
                actual.Role == expected.Role && actual.CurrentSystemId == expected.CurrentSystemId &&
                actual.DestinationSystemId == expected.DestinationSystemId &&
                actual.RouteSystemIds.SequenceEqual(expected.RouteSystemIds) &&
                actual.HoldRequested == expected.HoldRequested && actual.IsActive == expected.IsActive &&
                actual.DestinationPlanetaryBodyId == expected.DestinationPlanetaryBodyId &&
                actual.PreventAutomaticSettlement == expected.PreventAutomaticSettlement &&
                actual.SettlementBodyId == expected.SettlementBodyId &&
                actual.ReconnaissanceSystemId == expected.ReconnaissanceSystemId &&
                actual.EmbarkedPopulationSpeciesId == expected.EmbarkedPopulationSpeciesId &&
                actual.ReturnToBaseRequested == expected.ReturnToBaseRequested &&
                actual.ReturnToBaseFailureReason == expected.ReturnToBaseFailureReason &&
                actual.MissionOrderRevision == expected.MissionOrderRevision,
            message + " (identity, route, hold, or pending mission fields differ)");
        RequirePosition(actual.Position, expected.Position, message + " (position differs)");
        RequireNear(actual.FuelRemainingLightYears, expected.FuelRemainingLightYears, message + " (fuel differs)");
        RequireNear(actual.SettlementDaysCompleted, expected.SettlementDaysCompleted, message + " (settlement progress differs)");
        RequireNear(actual.ReconnaissanceDaysCompleted, expected.ReconnaissanceDaysCompleted, message + " (scout progress differs)");
        RequireNear(actual.EmbarkedPopulationMillions, expected.EmbarkedPopulationMillions, message + " (passengers differ)");
        if (actual.LocalSurveyProgress is double actualSurvey && expected.LocalSurveyProgress is double expectedSurvey)
            RequireNear(actualSurvey, expectedSurvey, message + " (science progress differs)");
        else
            Require(actual.LocalSurveyProgress == expected.LocalSurveyProgress, message + " (science progress presence differs)");
        RequireNear(actual.TreasuryCredits, expected.TreasuryCredits, message + " (treasury differs)");
        RequireNear(actual.OperatingFundingFraction, expected.OperatingFundingFraction, message + " (operating funding differs)");
    }

    private static void DeleteSave(string path)
    {
        if (File.Exists(path)) File.Delete(path);
        if (File.Exists(path + ".bak")) File.Delete(path + ".bak");
    }

    private static void RequirePosition(Vector2 actual, Vector2 expected, string message)
    {
        if (Vector2.Distance(actual, expected) > .0001f)
            throw new InvalidOperationException($"{message}: {actual} != {expected}");
    }

    private static void RequireNear(double actual, double expected, string message)
    {
        if (Math.Abs(actual - expected) > .0001)
            throw new InvalidOperationException($"{message}: {actual} != {expected}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private sealed record PersistedFleetSnapshot(int Id, int CivilizationId, FleetRole Role, Vector2 Position,
        int? CurrentSystemId, int? DestinationSystemId, int[] RouteSystemIds, double FuelRemainingLightYears,
        bool HoldRequested, bool IsActive, int? DestinationPlanetaryBodyId, bool PreventAutomaticSettlement,
        int? SettlementBodyId, double SettlementDaysCompleted, int? ReconnaissanceSystemId,
        double ReconnaissanceDaysCompleted, double EmbarkedPopulationMillions, string? EmbarkedPopulationSpeciesId,
        bool ReturnToBaseRequested, string? ReturnToBaseFailureReason, int MissionOrderRevision,
        double? LocalSurveyProgress, double TreasuryCredits, double OperatingFundingFraction);
}
