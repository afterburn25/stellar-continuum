using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Simulation;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyLoadingContextsAsync(MainMenuLayer menu, ConfirmationDialog dialog)
    {
        var backdrop = Descendants(menu).OfType<MainMenuBackdrop>().Single();
        Require(backdrop.UiArtworkResourcePath == MainMenuBackdrop.ArtworkPath,
            "campaign menu did not retain its original artwork");
        var tipControl = Descendants(menu).OfType<Control>().Single(control => control.Name == "LoadingTip");
        AssertInsideViewport(tipControl, "startup loading tip");

        await ClickNamedButtonAsync(menu, "NewPlayerCampaign");
        await ClickNamedButtonAsync(menu, "SandboxCampaignOption");
        var seed = Descendants(menu).OfType<LineEdit>().Single(input => input.Name == "SandboxSeed");
        await ReplaceSeedThroughKeyboardAsync(seed, "20260911");
        await ClickNamedButtonAsync(menu, "StartConfiguredSandbox");
        Require(dialog.Visible, "New Game did not show its protected confirmation.");
        await ClickControlAsync(dialog.GetOkButton());
        await WaitForLoadingToBeginAsync(menu);
        await ToSignal(GetTree().CreateTimer(.55), SceneTreeTimer.SignalName.Timeout);
        var generationTip = menu.UiLoadingTip;
        Require(menu.UiLoadingTitle == "GENERATING NEW GALAXY..." &&
                menu.UiLoadingArtworkPath.EndsWith("stellar-galaxy-generation.png", StringComparison.Ordinal) &&
                generationTip.StartsWith("Tip:", StringComparison.Ordinal) &&
                generationTip != _startupLoadingTipEvidence && menu.UiLoadingProgress is > 0 and < 100,
            "New Game did not show distinct galaxy art, a stable next tip, and truthful incomplete progress.");
        AssertInsideViewport(tipControl, "galaxy generation loading tip");
        await SaveViewportAsync("loading-context-02-new-galaxy.png", 0, 0);
        await ToSignal(GetTree().CreateTimer(.4), SceneTreeTimer.SignalName.Timeout);
        Require(menu.UiLoadingTip == generationTip, "generation changed its tip during one loading transition");
        await WaitForCampaignLoadingAsync();

        if (!_main.UiIsPaused)
            await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        Require(_main.UiIsPaused, "real playback control did not pause the generated campaign before saving");
        var generatedSeed = _main.UiCampaignSeed;
        var generatedHomePlanet = _main.UiHomePlanetSampleIdentity;
        var savePath = ProjectSettings.GlobalizePath("user://saves/autosave.json");
        Require(generatedSeed == 20260911 && !string.IsNullOrWhiteSpace(generatedHomePlanet),
            "New Game did not commit the requested generated campaign.");
        var loadingCountBeforeSave = menu.LoadingPresentationShownCount;
        await OpenSectionAsync("menu");
        var saveButton = Descendants(ActivePanel()).OfType<Button>().Single(button => button.Text == "Save");
        await ClickControlAsync(saveButton);
        await WaitFramesAsync(2);
        Require(File.Exists(savePath) && menu.LoadingPresentationShownCount == loadingCountBeforeSave,
            "the real Save control failed or incorrectly opened a loading splash");
        var generatedDay = _main.UiSimulationDays;
        var saveHash = HashFile(savePath);

        await ClickNamedButtonAsync(ActivePanel(), "CampaignMenu");
        Require(_main.UiIsMenuOpen, "the visible Campaign control did not open the menu after saving");
        await ClickNamedButtonAsync(menu, "LoadCampaign");
        Require(dialog.Visible, "Load saved campaign did not show its protected confirmation.");
        await ClickControlAsync(dialog.GetOkButton());
        await WaitForLoadingToBeginAsync(menu);
        await ToSignal(GetTree().CreateTimer(.55), SceneTreeTimer.SignalName.Timeout);
        var saveTip = menu.UiLoadingTip;
        Require(menu.UiLoadingTitle == "LOADING SAVED GAME..." &&
                menu.UiLoadingArtworkPath.EndsWith("stellar-save-loading.png", StringComparison.Ordinal) &&
                saveTip.StartsWith("Tip:", StringComparison.Ordinal) && saveTip != generationTip &&
                menu.UiLoadingProgress is > 0 and < 100,
            "saved restoration did not show distinct save art, a stable next tip, and truthful incomplete progress.");
        AssertInsideViewport(tipControl, "saved-game loading tip");
        await SaveViewportAsync("loading-context-03-saved-game.png", 0, 0);
        await ToSignal(GetTree().CreateTimer(.4), SceneTreeTimer.SignalName.Timeout);
        Require(menu.UiLoadingTip == saveTip, "saved restoration changed its tip during one loading transition");
        await WaitForCampaignLoadingAsync();
        Require(_main.UiIsPaused && _main.UiCampaignSeed == generatedSeed &&
                Math.Abs(_main.UiSimulationDays - generatedDay) < .000001 &&
                _main.UiHomePlanetSampleIdentity == generatedHomePlanet && HashFile(savePath) == saveHash,
            "manual saved restoration changed identity, time, save bytes, or returned unpaused");
        Check(true, "startup-new-game-and-saved-loading-contexts");
    }

    private async Task WaitForLoadingToBeginAsync(MainMenuLayer menu)
    {
        var elapsed = Stopwatch.StartNew();
        while (elapsed.Elapsed < TimeSpan.FromSeconds(3) && !menu.IsLoadingCampaign)
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
        Require(menu.IsLoadingCampaign, "campaign action did not enter its loading presentation");
    }
}
