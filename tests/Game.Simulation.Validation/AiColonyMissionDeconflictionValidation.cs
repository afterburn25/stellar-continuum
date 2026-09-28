using Game.Simulation.Colonization;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class AiColonyMissionDeconflictionValidation
{
    public static void ValidateInterruptedSettlementRetarget()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(c => !c.IsPlayer);
        DisableAllColonyFleets(galaxy);
        var target = FindViableUnoccupiedTargetSystems(galaxy, civilization.SpeciesId, 1)[0];
        galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, target.Id);
        var fleet = AddColonyFleet(galaxy, civilization, "Interrupted settlement");
        // A settlement already occupied by another colony is no longer a valid work site.
        var occupied = galaxy.Colonies.First(c => c.SystemId == civilization.HomeSystemId);
        fleet.DestinationPlanetaryBodyId = occupied.PlanetaryBodyId;
        fleet.SettlementBodyId = occupied.PlanetaryBodyId;
        fleet.SettlementDaysCompleted = 5;
        new ColonizationSimulation().Advance(galaxy, 1);
        Require(fleet.DestinationSystemId == target.Id && fleet.SettlementBodyId is null && fleet.SettlementDaysCompleted == 0,
            "AI retarget kept the abandoned world's work state attached to a new interstellar route");
        var path = Path.Combine(Path.GetTempPath(), "stellar-retarget-" + Guid.NewGuid().ToString("N") + ".json");
        try
        {
            var saves = new Game.Persistence.CampaignSaveService();
            saves.Save(path, galaxy, 1);
            var restored = saves.Load(path).Galaxy.Fleets.Single(f => f.Id == fleet.Id);
            Require(restored.DestinationSystemId == target.Id && restored.SettlementBodyId is null,
                "AI replacement route did not survive campaign save/load");
        }
        finally { if (File.Exists(path)) File.Delete(path); }
    }

    public static void ValidateFriendlyColonyShipsSplitAcrossViableSystems()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => !candidate.IsPlayer);
        DisableAllColonyFleets(galaxy);

        var targets = FindViableUnoccupiedTargetSystems(galaxy, civilization.SpeciesId, count: 2);
        foreach (var target in targets)
            galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, target.Id);

        var first = AddColonyFleet(galaxy, civilization, "Deconfliction Colony One");
        var second = AddColonyFleet(galaxy, civilization, "Deconfliction Colony Two");

        new ColonizationSimulation().Advance(galaxy);

        if (first.DestinationSystemId is not int firstTarget)
            throw new InvalidOperationException("first AI colony ship received no destination");
        if (second.DestinationSystemId is not int secondTarget)
            throw new InvalidOperationException("second AI colony ship received no destination");

        Require(firstTarget != secondTarget,
            "two friendly AI colony ships selected the same destination while another viable system was available");
        Require(targets.Any(target => target.Id == firstTarget) && targets.Any(target => target.Id == secondTarget),
            "AI colony deconfliction selected a system outside the controlled viable target set");
    }

    public static void ValidateOnlyRemainingFriendlyTargetIsNotDuplicated()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => !candidate.IsPlayer);
        DisableAllColonyFleets(galaxy);

        var onlyTarget = FindViableUnoccupiedTargetSystems(galaxy, civilization.SpeciesId, count: 1)[0];
        galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, onlyTarget.Id);

        var first = AddColonyFleet(galaxy, civilization, "Single Target Colony One");
        var second = AddColonyFleet(galaxy, civilization, "Single Target Colony Two");

        new ColonizationSimulation().Advance(galaxy);

        var assigned = new[] { first, second }
            .Where(fleet => fleet.DestinationSystemId == onlyTarget.Id)
            .ToArray();
        var idle = new[] { first, second }
            .Where(fleet => fleet.DestinationSystemId is null)
            .ToArray();

        Require(assigned.Length == 1,
            "AI duplicated the only remaining single-colony destination across multiple friendly colony ships");
        Require(idle.Length == 1,
            "AI did not leave the extra colony ship available when every viable destination was already reserved");
    }

    public static void ValidateForeignMissionDoesNotReserveHiddenIntent()
    {
        var galaxy = CreateValidationGalaxy();
        var civilization = galaxy.Civilizations.First(candidate => !candidate.IsPlayer);
        var foreign = galaxy.Civilizations.First(candidate =>
            candidate.Id != civilization.Id && candidate.Id != galaxy.PlayerCivilizationId);
        DisableAllColonyFleets(galaxy);

        var target = FindViableUnoccupiedTargetSystems(galaxy, civilization.SpeciesId, count: 1)[0];
        galaxy.Knowledge.MarkSystemFullySurveyed(civilization.Id, target.Id);

        var requester = AddColonyFleet(galaxy, civilization, "Foreign Intent Safe Colony Ship");
        var foreignMission = AddColonyFleet(galaxy, foreign, "Hidden Foreign Colony Mission");
        foreignMission.DestinationSystemId = target.Id;
        foreignMission.DestinationPlanetaryBodyId = galaxy.PlanetaryBodies
            .Where(body => body.SystemId == target.Id)
            .OrderBy(body => body.Id)
            .First().Id;

        new ColonizationSimulation().Advance(galaxy);

        Require(requester.DestinationSystemId == target.Id,
            "AI treated a foreign in-transit colony mission as a friendly reservation and leaked hidden opponent intent");
    }

    private static StarSystemState[] FindViableUnoccupiedTargetSystems(
        GalaxyState galaxy,
        string speciesId,
        int count)
    {
        var occupiedSystems = galaxy.Colonies.Select(colony => colony.SystemId).ToHashSet();
        var evaluator = new SpeciesPlanetaryHabitabilityEvaluator();
        var targets = galaxy.PlanetaryBodies
            .Where(body => !occupiedSystems.Contains(body.SystemId))
            .Where(body => evaluator.Evaluate(body, speciesId).CanFoundCurrentColony)
            .Select(body => body.SystemId)
            .Distinct()
            .OrderBy(systemId => systemId)
            .Take(count)
            .Select(systemId => galaxy.Systems.First(system => system.Id == systemId))
            .ToArray();

        Require(targets.Length == count,
            $"validation galaxy did not provide {count} viable unoccupied colony target system{(count == 1 ? string.Empty : "s")}");
        return targets;
    }

    private static void DisableAllColonyFleets(GalaxyState galaxy)
    {
        foreach (var fleet in galaxy.Fleets.Where(fleet => fleet.Role == FleetRole.Colony))
            fleet.IsActive = false;
    }

    private static FleetState AddColonyFleet(
        GalaxyState galaxy,
        CivilizationState civilization,
        string name)
    {
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 120000 : galaxy.Fleets.Max(existing => existing.Id) + 120000,
            CivilizationId = civilization.Id,
            Name = name,
            Role = FleetRole.Colony,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = 12.0,
            SensorRange = 95.0f,
            IsActive = true,
            EmbarkedPopulationMillions = 120.0,
            EmbarkedPopulationSpeciesId = civilization.SpeciesId,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static GalaxyState CreateValidationGalaxy() =>
        new GalaxyGenerator().Generate(
            0x434F_4C4F_4E59_4443L,
            new GalaxyGenerationSettings
            {
                SystemCount = 72,
                PreWarpCivilizationCount = 6,
                AncientCivilizationCount = 1,
                Radius = 620.0f,
            });

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
