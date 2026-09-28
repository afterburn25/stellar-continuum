using System;
using System.Diagnostics;
using System.Threading.Tasks;
using Game.Presentation;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private string _startupLoadingTipEvidence = string.Empty;

    private async Task WaitForStartupLoadingAsync(MainMenuLayer menu, bool captureEvidence)
    {
        Require(menu.StartupLoadingPresentationShownCount == 1 && menu.IsLoadingCampaign,
            "startup did not enter the dedicated game-loading presentation");
        Require(menu.HasLoadingPresentation && menu.UiLoadingProgress < 100,
            "startup loading art or truthful incomplete progress was missing");
        Require(AudioDirector.Instance is { IsMusicPlaying: false },
            "background music started before the main menu finished loading");
        if (captureEvidence)
        {
            Require(menu.UiLoadingTitle.Replace(" ", string.Empty, StringComparison.Ordinal)
                        .Contains("LOADINGGAME", StringComparison.Ordinal) &&
                    menu.UiLoadingArtworkPath.EndsWith("stellar-loading-splash.png", StringComparison.Ordinal) &&
                    menu.UiLoadingTip.StartsWith("Tip:", StringComparison.Ordinal),
                "startup loading context did not show its Earth-station art, title and beginner tip");
            _startupLoadingTipEvidence = menu.UiLoadingTip;
            await ToSignal(GetTree().CreateTimer(.75), SceneTreeTimer.SignalName.Timeout);
            Require(menu.IsLoadingCampaign && menu.UiLoadingProgress < 100 &&
                    menu.UiLoadingTip == _startupLoadingTipEvidence,
                "fast startup dismissed or reported completion before the minimum display interval");
            await SaveViewportAsync("loading-splash-startup.png", 0, 0);
        }

        var wait = Stopwatch.StartNew();
        var priorProgress = menu.UiLoadingProgress;
        while (wait.Elapsed < TimeSpan.FromSeconds(20) && menu.IsLoadingCampaign)
        {
            Require(menu.StartupLoadingPresentationShownCount == 1 && menu.HasLoadingPresentation &&
                    AudioDirector.Instance is { IsMusicPlaying: false } &&
                    menu.UiLoadingProgress >= priorProgress &&
                    menu.UiLoadingArtworkPath.EndsWith("stellar-loading-splash.png", StringComparison.Ordinal),
                "startup loading restarted, changed artwork, disappeared, or moved progress backwards");
            priorProgress = menu.UiLoadingProgress;
            await ToSignal(GetTree().CreateTimer(.05), SceneTreeTimer.SignalName.Timeout);
        }
        Require(!menu.IsLoadingCampaign && menu.LastLoadingDurationSeconds >=
                CampaignLoadingTimeline.MinimumDisplaySeconds && menu.UiLoadingProgress == 100,
            "startup splash did not remain until real readiness and its minimum display interval");
        Require(menu.RetainedCampaignLoadAssetCount == 6,
            "startup did not retain every resource reported by its real asset-loading phase");
        Require(AudioDirector.Instance is { IsMusicPlaying: true },
            "music did not start when the main menu became ready");
        await WaitFramesAsync(2);
    }
}
