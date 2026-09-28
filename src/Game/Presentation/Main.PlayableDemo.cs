using System.IO;
using Game.Campaign;
using Game.Simulation;

namespace Game.Presentation;

public partial class Main
{
    private string CurrentCampaignSavePath => UiIsDeveloperMode ? DeveloperSavePath : AutosavePath;
    public bool UiIsPlayableDemo => UiIsDeveloperMode;
    public bool UiHasDemoSave => File.Exists(PlayableDemoScenario.SavePathBeside(AutosavePath)) || File.Exists(PlayableDemoScenario.SavePathBeside(AutosavePath) + ".bak");
    public SimulationClock.SpeedLevel UiCurrentSpeed => _clock.Speed;
    public SimulationClock.SpeedLevel UiResumeSpeed => _clock.ResumeSpeed;
    public double UiRequestedSpeedMultiplier => UiIsMassiveCombatActive ? UiTacticalSpeed : _clock.RequestedMultiplier;
    public double UiSimulationDays => _clock.SimulationDays;
    public DemoObjectiveSnapshot? UiDemoObjective => _galaxy is not null
        ? DemoObjectiveView.Build(_galaxy, _clock.RequestedMultiplier, _adaptiveResearch)
        : null;
    public void UiResumeAtSpeed(SimulationClock.SpeedLevel speed)
    {
        if ((int)speed < 0 || (int)speed > (int)SimulationClock.SpeedLevel.Demo) return;
        _clock.SetSpeed(speed == SimulationClock.SpeedLevel.Demo && !UiIsDeveloperMode
            ? SimulationClock.SpeedLevel.Normal : speed);
    }
    public void UiResumeDemoSpeed()
    {
        if (UiIsMassiveCombatActive)
        {
            UiSetTacticalSpeed(4);
            return;
        }
        if (UiIsDeveloperMode && !(GetNodeOrNull<MainMenuLayer>("MainMenuLayer")?.IsBlockingGameplay ?? false))
            _clock.SetSpeed(SimulationClock.SpeedLevel.Demo);
    }

    public bool UiCheckpointBeforeCampaignSwitch() => _galaxy is null || TryPersistIntegratedCampaign(
        "save-before-switch", false, "Campaign switch cancelled because saving failed. Your current campaign is still open.");

    public void UiCreateNewCampaignConfirmed() => CreateIntegratedNewCampaign();
    public void UiCreateNewCampaignConfirmed(string enteredSeed) => CreateIntegratedNewCampaign(enteredSeed);
    public void UiCreateNewCampaignConfirmed(string enteredSeed, string playerSpeciesId) =>
        CreateIntegratedNewCampaign(enteredSeed, playerSpeciesId);

    // Compatibility entrypoints route through the maintained Developer campaign boundary.
    public void UiPlayDemoConfirmed() => UiCreateDeveloperCampaignConfirmed(PlayableDemoScenario.Seed);
    public void UiContinueDemo() => UiSwitchToDeveloperMode();
}
