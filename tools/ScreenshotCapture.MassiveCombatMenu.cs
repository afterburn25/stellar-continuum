using System;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Presentation.Spatial;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyMassiveCombatMenuLifecycleAsync(MainMenuLayer menu)
    {
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        Require(_main.UiPrepareMassiveCombatMenuCapture(), "capture-guard-creates-real-campaign-encounter");
        await WaitFramesAsync(8);
        var combatLayer = _main.GetNode<CanvasLayer>("MassiveCombatPresentation");
        var view = combatLayer.GetNode<MassiveCombatView>("MassiveCombatView");
        Require(view.IsVisibleInTree() && _main.UiTacticalSpeed == 1, "tactical-view-opens-at-real-time-speed");
        var systemScene = view.GetNode<SystemScene3D>("TacticalSystemScene");
        Require(systemScene.PrimaryStarCount > 0 && systemScene.BodyCount > 0 &&
                view.RenderedDetailedVessels is > 0 and <= 32,
            "small-live-battle-renders-bounded-detailed-ships-in-its-real-system-scene");
        var initialTick = _main.UiMassiveCombatSnapshot?.Tick ?? -1;
        await WaitFramesAsync(12);
        Require((_main.UiMassiveCombatSnapshot?.Tick ?? -1) > initialTick, "tactical-clock-advances-before-menu");

        _main.UiSetTacticalSpeed(2, announce: false);
        await PressKeyAsync(Key.Escape);
        var pausedTick = _main.UiMassiveCombatSnapshot?.Tick ?? -1;
        Require(menu.IsBlockingGameplay && _main.UiTacticalSpeed == 0 && !combatLayer.Visible,
            "escape-opens-menu-above-combat-and-pauses-tactical-clock");
        await SaveViewportAsync("massive-combat-menu-paused.png");
        await WaitFramesAsync(20);
        Require((_main.UiMassiveCombatSnapshot?.Tick ?? -1) == pausedTick, "campaign-menu-blocks-hidden-tactical-progression");
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        await WaitFramesAsync(10);
        Require(_main.UiTacticalSpeed == 2 && (_main.UiMassiveCombatSnapshot?.Tick ?? -1) > pausedTick,
            "closing-campaign-menu-restores-prior-nondefault-tactical-rate");

        await ClickNamedButtonAsync(view, "TacticalCampaignMenu");
        Require(menu.IsBlockingGameplay && _main.UiTacticalSpeed == 0 && !combatLayer.Visible,
            "mouse-menu-button-opens-menu-and-pauses-tactical-clock");
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        await WaitFramesAsync(4);
        Require(_main.UiTacticalSpeed == 2, "mouse-menu-close-restores-prior-tactical-rate");

        _main.UiSave();
        await WaitFramesAsync(4);
        _main.UiOpenMenu();
        await WaitFramesAsync(3);
        Require(_main.UiLoadCurrentCampaign(), "active-tactical-save-reloads-through-campaign-service");
        var loadedTick = _main.UiMassiveCombatSnapshot?.Tick ?? -1;
        await WaitFramesAsync(16);
        Require(_main.UiIsMassiveCombatActive && _main.UiTacticalSpeed == 0 &&
                (_main.UiMassiveCombatSnapshot?.Tick ?? -1) == loadedTick,
            "loaded-active-encounter-resets-host-and-remains-paused-behind-menu");
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        await WaitFramesAsync(12);
        Require(_main.UiTacticalSpeed == 0 && (_main.UiMassiveCombatSnapshot?.Tick ?? -1) == loadedTick,
            "loaded-encounter-does-not-resume-with-stale-preload-rate");
        _main.UiSetTacticalSpeed(1, announce: false);
        await WaitFramesAsync(10);
        Require((_main.UiMassiveCombatSnapshot?.Tick ?? -1) > loadedTick,
            "explicit-tactical-resume-advances-reloaded-encounter");
        await SaveViewportAsync("massive-combat-reloaded-resumed.png");
        await ResizeResponsiveWindowAsync(new Vector2I(1920, 1080));
        await WaitFramesAsync(8);
        Require(view.IsVisibleInTree() && view.RenderedDetailedVessels is > 0 and <= 32 &&
                systemScene.PrimaryStarCount > 0,
            "small-system-battle-remains-detailed-and-readable-at-native-1080p");
        await SaveViewportAsync("massive-combat-system-1080p.png", 1920, 1080);
        await ResizeResponsiveWindowAsync(new Vector2I(1280, 720));
    }
}
