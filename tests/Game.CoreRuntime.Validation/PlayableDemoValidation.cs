using Game.Campaign;
using Game.Simulation;
using Game.Simulation.Construction;
using Game.Simulation.Research;
using Game.Simulation.Research.Adaptive;

namespace Game.CoreRuntime.Validation;

internal static class PlayableDemoValidation
{
    public static void Run()
    {
        var sessions = new CampaignSessionService();
        var demo = PlayableDemoScenario.Create(sessions);
        var normal = sessions.CreateNew(PlayableDemoScenario.Seed);
        Require(normal.Galaxy.Systems.Count == 100, "new campaigns must use the compact 100-system playable map");
        Require(demo.Galaxy.PlanetaryBodies.SequenceEqual(normal.Galaxy.PlanetaryBodies), "demo departed from canonical fresh-campaign worlds");
        var player = demo.Galaxy.PlayerCivilizationId;
        var economy = demo.Galaxy.Economies.Single(e => e.CivilizationId == player);
        Require(economy.Industry == 200 && economy.Science == 0 &&
            !demo.Galaxy.Fleets.Any(f => f.CivilizationId == player) &&
            demo.Galaxy.Technologies.Single(t => t.CivilizationId == player).CompletedTechnologyIds.Count == 0,
            "demo granted free production, technology or vessels");
        var adaptiveGuidance = DemoObjectiveView.Build(demo.Galaxy, 24, demo.AdaptiveResearch);
        Require(adaptiveGuidance.Research.Contains("In-Space Assembly") && adaptiveGuidance.Construction.Contains("Research Network"),
            "Adaptive demo guide omitted the actual available opening actions");
        var pausedStart = AdaptiveResearchCampaignCommands.StartDirectedResearch(
            demo.Galaxy, demo.AdaptiveResearch, player, "in_space_assembly", 8);
        Require(pausedStart.Accepted && AdaptiveResearchCampaignCommands.PauseDirectedResearch(
                demo.AdaptiveResearch, player, "in_space_assembly").Accepted,
            "could not establish a paused Adaptive Research guide fixture");
        var pausedGuidance = DemoObjectiveView.Build(demo.Galaxy, 24, demo.AdaptiveResearch);
        Require(pausedGuidance.Research.Contains("In-Space Assembly is paused", StringComparison.Ordinal) &&
            pausedGuidance.Research.Contains("Resume it in Research", StringComparison.Ordinal),
            "paused Adaptive Research guide did not direct the player to resume its program");
        var guidance = DemoObjectiveView.Build(demo.Galaxy, 24);
        Require(guidance.Research.Contains("Fusion Propulsion") && guidance.Construction.Contains("Research Network"), "legacy guide compatibility omitted available opening actions");
        new ResearchSimulation().StartResearch(demo.Galaxy, player, "fusion_propulsion");
        new ConstructionSimulation().StartProject(demo.Galaxy, player, "research_network");
        var clock = new SimulationClock();
        clock.SetSpeed(SimulationClock.SpeedLevel.Demo);
        var total = 0.0;
        var core = new GalaxySimulationStepCoordinator();
        for (var frame = 0; frame < 120; frame++)
        {
            var steps = PlayableDemoScenario.AdvanceFrame(clock, 1.0 / 30.0);
            Require(steps.Length <= 4 && steps.All(s => s > 0 && s <= 0.25), "demo exceeded bounded substeps");
            foreach (var days in steps) { core.Advance(demo.Galaxy, days); total += days; }
        }
        Require(Math.Abs(total - 96) < 0.000001 && Math.Abs(clock.SimulationDays - total) < 0.000001, "24x frame stepping lost accepted simulation days");
        var activeGuidance = DemoObjectiveView.Build(demo.Galaxy, 24);
        Require(activeGuidance.Research.Contains("progress depends on supplied science", StringComparison.Ordinal) &&
            !activeGuidance.Research.Contains("seconds", StringComparison.Ordinal),
            "active demo research promised a stockpile-rate ETA instead of its live supply qualifier");
        var stalled = PlayableDemoScenario.AdvanceFrame(clock, 3600);
        Require(stalled.Sum() <= 1 && stalled.Length <= 4 && clock.BacklogDays <= 2, "long frame created runaway catch-up work");
        clock.SetSpeed(SimulationClock.SpeedLevel.Paused);
        var beforePause = clock.SimulationDays;
        Require(PlayableDemoScenario.AdvanceFrame(clock, 60).Length == 0 && clock.SimulationDays == beforePause, "demo advanced while paused");
        clock.SetSpeed(SimulationClock.SpeedLevel.Normal);
        Require(clock.RequestedMultiplier == 1, "demo changed normal speed");
        clock.SetSpeed(SimulationClock.SpeedLevel.Maximum);
        Require(clock.RequestedMultiplier == 8, "ordinary maximum speed is not the playable 8x rate");
        var scheduler = PlayableDemoScenario.CreateAutosaveScheduler();
        scheduler.Reset(0);
        Require(!scheduler.IsDue(30) && scheduler.IsDue(720), "demo autosaves at the normal high-frequency cadence");

        var directory = Path.Combine(Path.GetTempPath(), $"stellar-demo-continuity-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            var normalPath = Path.Combine(directory, "autosave.json");
            var demoPath = PlayableDemoScenario.SavePathBeside(normalPath);
            Require(normalPath != demoPath, "demo reused normal autosave path");
            sessions.Save(normalPath, normal.Galaxy, normal.Diplomacy, 0);
            var normalBytes = File.ReadAllBytes(normalPath);
            sessions.Save(demoPath, demo.Galaxy, demo.Diplomacy, total);
            var loaded = sessions.LoadOrCreate(demoPath, PlayableDemoScenario.Seed);
            Require(loaded.WasLoaded && loaded.Seed == PlayableDemoScenario.Seed && Math.Abs(loaded.SimulationDays - total) < 0.000001, "Continue Demo lost campaign identity or time");
            Require(Math.Abs(loaded.Galaxy.Technologies.Single(t => t.CivilizationId == player).ActiveResearchProgress - demo.Galaxy.Technologies.Single(t => t.CivilizationId == player).ActiveResearchProgress) < 0.000001, "Continue Demo lost research progress");
            var restarted = PlayableDemoScenario.Create(sessions);
            sessions.Save(demoPath, restarted.Galaxy, restarted.Diplomacy, 0);
            Require(File.Exists(demoPath + ".bak"), "restarting demo did not preserve its previous checkpoint");
            File.Delete(demoPath);
            var recovered = sessions.LoadOrCreate(demoPath, PlayableDemoScenario.Seed);
            Require(recovered.RecoveredFromBackup && Math.Abs(recovered.SimulationDays - total) < 0.000001, "demo backup continuity failed");
            Require(normalBytes.SequenceEqual(File.ReadAllBytes(normalPath)), "demo save/restart/recovery changed the normal campaign");
        }
        finally
        {
            var expectedParent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            Require(string.Equals(Path.GetDirectoryName(Path.GetFullPath(directory)), expectedParent, StringComparison.OrdinalIgnoreCase), "refusing cleanup outside the test temporary directory");
            Directory.Delete(directory, recursive: true);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
