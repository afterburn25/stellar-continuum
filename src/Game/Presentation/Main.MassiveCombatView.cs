using Godot;

namespace Game.Presentation;

public partial class Main
{
    private CanvasLayer? _massiveCombatPresentationLayer;
    private MassiveCombatView? _massiveCombatView;
    private double _massiveCombatPresentationRefresh;
    private bool _massiveCombatWasActive;

    public bool UiIsMassiveCombatPresentationOpen => _massiveCombatView?.Visible == true;

    protected void InitializeMassiveCombatPresentation()
    {
        if (_massiveCombatView is not null) return;
        _massiveCombatPresentationLayer = new CanvasLayer { Name = "MassiveCombatPresentation", Layer = 70 };
        _massiveCombatView = new MassiveCombatView
        {
            Name = "MassiveCombatView",
            Visible = false,
            OrderRequested = UiIssueMassiveCombatOrder,
            TacticalSpeedRequested = speed => UiSetTacticalSpeed(speed),
            TacticalResumeSpeedRequested = speed => UiSetTacticalResumeSpeed(speed),
            MenuRequested = UiOpenMenu,
        };
        _massiveCombatPresentationLayer.AddChild(_massiveCombatView);
        AddChild(_massiveCombatPresentationLayer);
    }

    protected void RefreshMassiveCombatPresentation(double delta)
    {
        if (_massiveCombatView is null) InitializeMassiveCombatPresentation();
        var active = UiIsMassiveCombatActive;
        if (_massiveCombatPresentationLayer is not null)
            _massiveCombatPresentationLayer.Visible = active && !UiIsMenuOpen;
        _massiveCombatPresentationRefresh += delta;
        if (!active)
        {
            if (_massiveCombatWasActive) _massiveCombatView!.UpdateSnapshot(null, -1);
            _massiveCombatWasActive = false;
            _massiveCombatPresentationRefresh = 0;
            return;
        }

        // Snapshot projection can inspect thousands of formations. Ten presentation updates
        // per second keep motion legible while the view's pooled effects still animate at frame rate.
        if (!_massiveCombatWasActive || _massiveCombatPresentationRefresh >= .1)
        {
            _massiveCombatPresentationRefresh = 0;
            var observer = UiMassiveCombatObserverCivilizationId;
            _massiveCombatView!.UpdateSnapshot(observer.HasValue ? UiMassiveCombatSnapshot : null, observer ?? -1,
                observer.HasValue ? BuildMassiveCombatEnvironment() : null);
            _massiveCombatView.SetTacticalSpeedState(UiTacticalSpeed);
            _massiveCombatView.SetTacticalResumeSpeedState(_tacticalResumeSpeed);
        }
        _massiveCombatWasActive = true;
    }
}
