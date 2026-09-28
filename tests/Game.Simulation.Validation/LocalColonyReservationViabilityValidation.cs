using Game.Simulation.Colonization;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class LocalColonyReservationViabilityValidation
{
    public static void ValidateBodylessStrandedFleetDoesNotDeadlockViableFriendlySpecies()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        DisableAllColonyFleets(galaxy);

        var scenario = FindCrossSpeciesScenario(galaxy);
        galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, scenario.SystemId);
        var targetSystem = galaxy.Systems.First(system => system.Id == scenario.SystemId);

        var stranded = AddColonyFleet(
            galaxy,
            civilization,
            scenario.StrandedSpeciesId,
            "Body-less Stranded Colony Fleet");
        stranded.Position = targetSystem.Position;
        stranded.CurrentSystemId = targetSystem.Id;
        stranded.DestinationSystemId = null;
        stranded.DestinationPlanetaryBodyId = null;

        var requester = AddColonyFleet(
            galaxy,
            civilization,
            scenario.ViableSpeciesId,
            "Viable Friendly Colony Fleet");

        var planner = new ColonizationOpportunityPlanner();
        var plan = planner.BuildPlan(
            galaxy,
            requester.Id,
            ColonizationOpportunityPlanner.HardMaximumCandidates);
        var candidate = plan.Candidates.FirstOrDefault(option =>
            option.PlanetaryBodyId == scenario.ViableBodyId)
            ?? throw new InvalidOperationException("viable cross-species target was missing from colony opportunity plan");

        Require(candidate.CanOrder,
            "body-less stranded friendly colony fleet deadlocked a system another friendly passenger species can actually settle");
        Require(!candidate.SystemReservedByFriendlyColonyMission && candidate.ReservedByFleetId is null,
            "stranded body-less fleet leaked into friendly reservation metadata despite having no viable settlement in the system");

        var order = new ColonizationSimulation().IssueColonyFleetOrder(
            galaxy,
            requester.Id,
            scenario.SystemId,
            scenario.ViableBodyId);
        Require(order.Accepted,
            "explicit friendly colony order was blocked by a stranded body-less fleet with no viable settlement");
        Require(requester.DestinationSystemId == scenario.SystemId &&
                requester.DestinationPlanetaryBodyId == scenario.ViableBodyId,
            "accepted cross-species rescue order did not set the viable fleet's exact destination");
    }

    public static void ValidateInvalidRetainedBodyDoesNotReserveAnotherViableBody()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        DisableAllColonyFleets(galaxy);

        var scenario = FindMixedBodyScenario(galaxy);
        galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, scenario.SystemId);
        var targetSystem = galaxy.Systems.First(system => system.Id == scenario.SystemId);

        var stranded = AddColonyFleet(
            galaxy,
            civilization,
            scenario.SpeciesId,
            "Invalid Retained-Body Colony Fleet");
        stranded.Position = targetSystem.Position;
        stranded.CurrentSystemId = targetSystem.Id;
        stranded.DestinationSystemId = null;
        stranded.DestinationPlanetaryBodyId = scenario.InvalidBodyId;

        var requester = AddColonyFleet(
            galaxy,
            civilization,
            scenario.SpeciesId,
            "Same-Species Viable Colony Fleet");

        var plan = new ColonizationOpportunityPlanner().BuildPlan(
            galaxy,
            requester.Id,
            ColonizationOpportunityPlanner.HardMaximumCandidates);
        var candidate = plan.Candidates.FirstOrDefault(option =>
            option.PlanetaryBodyId == scenario.ViableBodyId)
            ?? throw new InvalidOperationException("same-species viable body was missing from colony opportunity plan");

        Require(candidate.CanOrder,
            "fleet retaining an invalid exact body target reserved the entire system and blocked another viable body");
        Require(!candidate.SystemReservedByFriendlyColonyMission && candidate.ReservedByFleetId is null,
            "invalid retained body target incorrectly produced system-level friendly reservation metadata");
    }

    private static CrossSpeciesScenario FindCrossSpeciesScenario(GalaxyState galaxy)
    {
        var occupiedSystems = galaxy.Colonies.Select(colony => colony.SystemId).ToHashSet();
        var evaluator = new SpeciesPlanetaryHabitabilityEvaluator();

        foreach (var body in galaxy.PlanetaryBodies
                     .Where(body => !occupiedSystems.Contains(body.SystemId))
                     .OrderBy(body => body.SystemId)
                     .ThenBy(body => body.Id))
        {
            var viableSpecies = SpeciesCatalog.All
                .Where(species => evaluator.Evaluate(body, species.Id).CanFoundCurrentColony)
                .OrderBy(species => species.Id, StringComparer.Ordinal)
                .FirstOrDefault();
            if (viableSpecies is null)
                continue;

            var strandedSpecies = SpeciesCatalog.All
                .Where(species => species.Id != viableSpecies.Id)
                .Where(species => !galaxy.PlanetaryBodies
                    .Where(candidate => candidate.SystemId == body.SystemId)
                    .Any(candidate => evaluator.Evaluate(candidate, species.Id).CanFoundCurrentColony))
                .OrderBy(species => species.Id, StringComparer.Ordinal)
                .FirstOrDefault();
            if (strandedSpecies is null)
                continue;

            return new CrossSpeciesScenario(
                body.SystemId,
                body.Id,
                viableSpecies.Id,
                strandedSpecies.Id);
        }

        throw new InvalidOperationException(
            "validation galaxy did not provide an unoccupied system viable for one species and wholly nonviable for another");
    }

    private static MixedBodyScenario FindMixedBodyScenario(GalaxyState galaxy)
    {
        var occupiedSystems = galaxy.Colonies.Select(colony => colony.SystemId).ToHashSet();
        var evaluator = new SpeciesPlanetaryHabitabilityEvaluator();

        foreach (var systemId in galaxy.PlanetaryBodies
                     .Where(body => !occupiedSystems.Contains(body.SystemId))
                     .Select(body => body.SystemId)
                     .Distinct()
                     .OrderBy(id => id))
        {
            var bodies = galaxy.PlanetaryBodies
                .Where(body => body.SystemId == systemId)
                .OrderBy(body => body.Id)
                .ToArray();

            foreach (var species in SpeciesCatalog.All.OrderBy(species => species.Id, StringComparer.Ordinal))
            {
                var viable = bodies.FirstOrDefault(body => evaluator.Evaluate(body, species.Id).CanFoundCurrentColony);
                var invalid = bodies.FirstOrDefault(body => !evaluator.Evaluate(body, species.Id).CanFoundCurrentColony);
                if (viable is not null && invalid is not null)
                {
                    return new MixedBodyScenario(
                        systemId,
                        viable.Id,
                        invalid.Id,
                        species.Id);
                }
            }
        }

        throw new InvalidOperationException(
            "validation galaxy did not provide an unoccupied system with mixed viable/nonviable bodies for one species");
    }

    private static void DisableAllColonyFleets(GalaxyState galaxy)
    {
        foreach (var fleet in galaxy.Fleets.Where(fleet => fleet.Role == FleetRole.Colony))
            fleet.IsActive = false;
    }

    private static FleetState AddColonyFleet(
        GalaxyState galaxy,
        CivilizationState civilization,
        string passengerSpeciesId,
        string name)
    {
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 180000 : galaxy.Fleets.Max(existing => existing.Id) + 180000,
            CivilizationId = civilization.Id,
            Name = name,
            Role = FleetRole.Colony,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = 12.0,
            SensorRange = 95.0f,
            IsActive = true,
            EmbarkedPopulationMillions = 120.0,
            EmbarkedPopulationSpeciesId = passengerSpeciesId,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x5354_5241_4E44_4544L,
            new GalaxyGenerationSettings
            {
                SystemCount = 96,
                PreWarpCivilizationCount = 8,
                AncientCivilizationCount = 1,
                Radius = 720.0f,
            });

    private sealed record CrossSpeciesScenario(
        int SystemId,
        int ViableBodyId,
        string ViableSpeciesId,
        string StrandedSpeciesId);

    private sealed record MixedBodyScenario(
        int SystemId,
        int ViableBodyId,
        int InvalidBodyId,
        string SpeciesId);

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
