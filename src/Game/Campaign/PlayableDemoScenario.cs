using System;
using System.IO;
using Game.Simulation;

namespace Game.Campaign;

/// <summary>A repeatable human Earth/Sol campaign using canonical fresh-start generation; the demo adds only a separate save slot and clock speed.</summary>
public static class PlayableDemoScenario
{
    public const long Seed = 20260908;
    public const string SaveFileName = "demo-autosave.json";
    public const double MaximumStepDays = 0.25;
    public const int MaximumStepsPerFrame = 4;
    public static CampaignAutosaveScheduler CreateAutosaveScheduler() => new(new CampaignAutosavePolicy(720, 48));
    public static CampaignBootstrapResult Create(CampaignSessionService sessions) => sessions.CreateNew(Seed);
    public static string SavePathBeside(string normalSavePath) => Path.Combine(
        Path.GetDirectoryName(normalSavePath) ?? throw new ArgumentException("Save path needs a parent directory."), SaveFileName);

    public static double[] AdvanceFrame(SimulationClock clock, double realDeltaSeconds)
    {
        var accepted = clock.AdvanceBoundedFrame(realDeltaSeconds);
        if (accepted <= 0) return Array.Empty<double>();
        var count = Math.Min(MaximumStepsPerFrame, (int)Math.Ceiling(accepted / MaximumStepDays));
        var steps = new double[count];
        for (var i = 0; i < count; i++)
        {
            steps[i] = Math.Min(MaximumStepDays, accepted);
            accepted -= steps[i];
        }
        return steps;
    }
}
