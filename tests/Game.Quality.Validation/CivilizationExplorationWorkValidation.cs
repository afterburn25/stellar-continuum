using System;
using System.Linq;
using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.AI;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Quality.Validation;

internal static class CivilizationExplorationWorkValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateSupportedOwnedSurveyWork();
        Console.WriteLine("PASS: Civilization AI exploration signal uses supported owned mission-planner work");
    }

    private static void ValidateSupportedOwnedSurveyWork()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4558_504C_4F52_4557L,
            new GalaxyGenerationSettings
            {
                SystemCount = 28,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 0,
                Radius = 360.0f,
            });

        var civilization = galaxy.Civilizations.First(candidate => !candidate.IsPlayer && !candidate.IsSeededAncient);
        var foreign = galaxy.Civilizations.First(candidate => candidate.Id != civilization.Id);
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);

        // Recreate the old shortcut's condition: the campaign catalog contains targets this
        // civilization has not detected. That fact alone must no longer claim reachable work.
        Require(galaxy.Knowledge.GetKnownSystems(civilization.Id).Count < galaxy.Systems.Count,
            "validation fixture unexpectedly began with the entire catalog detected");

        foreach (var fleet in galaxy.Fleets.Where(fleet =>
                     fleet.CivilizationId == civilization.Id &&
                     fleet.Role is FleetRole.Scout or FleetRole.Science))
        {
            fleet.IsActive = false;
        }

        var nextFleetId = galaxy.Fleets.Count == 0 ? 95000 : galaxy.Fleets.Max(fleet => fleet.Id) + 95000;
        var foreignScout = CreateSurveyFleet(nextFleetId++, foreign.Id, FleetRole.Scout, "Foreign Survey Scout", home.Id, home.Position);
        galaxy.Fleets.Add(foreignScout);

        var supportedPlanner = new ExplorationMissionPlanner(new ConstantReachView(supported: true));
        var supportedBuilder = new CivilizationStrategicInputBuilder(explorationMissionPlanner: supportedPlanner);
        var foreignOnly = supportedBuilder.Build(galaxy, civilization.Id);
        Require(!foreignOnly.HasUnexploredReachableSystems,
            "foreign survey vessel or undetected catalog count leaked into own reachable-exploration state");

        var ownScout = CreateSurveyFleet(nextFleetId++, civilization.Id, FleetRole.Scout, "Owned Survey Scout", home.Id, home.Position);
        galaxy.Fleets.Add(ownScout);
        var plan = supportedPlanner.BuildPlan(
            galaxy,
            ownScout.Id,
            ExplorationMissionPlanner.HardMaximumCandidates);
        Require(plan.Candidates.Count > 0,
            "validation fixture produced no legitimate survey work for the owned Scout");
        Require(plan.Candidates.Any(candidate => candidate.Reach.IsSupported),
            "supported reach fixture did not produce a supported survey candidate");

        var supportedOwn = supportedBuilder.Build(galaxy, civilization.Id);
        Require(supportedOwn.HasUnexploredReachableSystems,
            "active owned Scout with supported canonical mission-planner work was not visible to strategy");

        var blockedPlanner = new ExplorationMissionPlanner(new ConstantReachView(supported: false));
        var blockedBuilder = new CivilizationStrategicInputBuilder(explorationMissionPlanner: blockedPlanner);
        var blockedOwn = blockedBuilder.Build(galaxy, civilization.Id);
        Require(!blockedOwn.HasUnexploredReachableSystems,
            "strategy claimed reachable exploration work when Logistics-owned reach rejected every candidate");

        ownScout.IsActive = false;
        var noOwnedSurvey = supportedBuilder.Build(galaxy, civilization.Id);
        Require(!noOwnedSurvey.HasUnexploredReachableSystems,
            "inactive owned Scout still counted as available exploration work");
    }

    private static FleetState CreateSurveyFleet(
        int id,
        int civilizationId,
        FleetRole role,
        string name,
        int systemId,
        Vector2 position) => new()
    {
        Id = id,
        CivilizationId = civilizationId,
        Name = name,
        Role = role,
        Position = position,
        CurrentSystemId = systemId,
        StrategicSpeed = 24.0,
        SensorRange = 135.0f,
        IsActive = true,
    };

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private sealed class ConstantReachView : IInterstellarOperationalReachView
    {
        private readonly bool _supported;

        public ConstantReachView(bool supported)
        {
            _supported = supported;
        }

        public MissionReachAssessment Assess(
            GalaxyState galaxy,
            int civilizationId,
            FleetState fleet,
            int targetSystemId,
            InterstellarMissionKind missionKind)
        {
            _ = galaxy;
            _ = civilizationId;
            _ = fleet;
            _ = targetSystemId;
            _ = missionKind;
            return _supported
                ? MissionReachAssessment.Supported("Validation reach supports this survey mission.")
                : MissionReachAssessment.Unsupported("Validation reach blocks this survey mission.");
        }
    }
}
