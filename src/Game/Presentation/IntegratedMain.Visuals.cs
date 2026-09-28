namespace Game.Presentation;

public partial class IntegratedMain
{
    public override void _Draw()
    {
        // Visual overlays read the campaign directly and cannot render a failed startup.
        if (!_runtimeReady)
            return;
        if (UiIsSystemSpatialView && UiSystemViewBlend >= 1)
            return;
        DrawVisualMapOverlay();
    }
}
