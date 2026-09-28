using System;
using System.Linq;
using System.Threading.Tasks;
using Godot;
using Game.Presentation;
using Game.Presentation.Audio.Voice;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyResponsiveResolutionsAsync()
    {
        foreach (var size in new[] { new Vector2I(1920, 1080), new Vector2I(2560, 1440), new Vector2I(3840, 2160), new Vector2I(1280, 720) })
        {
            await ResizeResponsiveWindowAsync(size); await WaitForCameraAsync();
            var logical = GetViewport().GetVisibleRect().Size;
            var expected = size.Y > 1080 ? new Vector2(1920, 1080) : (Vector2)size;
            Require(logical == expected, $"Responsive viewport is {logical}, expected {expected} at {size}; {ResponsiveDiagnostics(size)}");
            if (_sidebar.IsDrawerOpen) await CloseDrawerAsync();
            await OpenSectionAsync("research"); await WaitForRefreshAsync();
            VoiceSettings? previousVoiceSettings = null;
            if (size.Y is 720 or 1080)
            {
                previousVoiceSettings = await BeginCaptionLayoutProbeAsync($"responsive-caption-layout-{size.Y}p");
                AssertCaptionDoesNotCover(_drawer, $"caption-safe-area-preserves-drawer-{size.Y}p");
            }
            var workspace = ActivePanel() as ResearchWorkspaceView
                ?? throw new InvalidOperationException("Research did not open its fullscreen workspace.");
            var graph = Descendants(workspace).OfType<Control>().Single(control => control.Name == "ResearchGraph");
            var inspector = Descendants(workspace).OfType<Control>().Single(control => control.Name == "ResearchInspector");
            var tabs = Descendants(workspace).OfType<FlowContainer>().Single(control => control.Name == "ResearchCategoryTabs");
            var search = Descendants(workspace).OfType<LineEdit>().Single(control => control.Name == "ResearchSearch");
            Require(workspace.GraphControlCount > 0 && graph.IsVisibleInTree() && inspector.IsVisibleInTree() &&
                    tabs.IsVisibleInTree() && search.IsVisibleInTree(),
                "Research workspace lost its graph, category tabs, search, or inspector at a responsive size.");
            Require(Encloses(ScreenRect(workspace), ScreenRect(graph)) &&
                    Encloses(ScreenRect(workspace), ScreenRect(inspector)) &&
                    Descendants(tabs).OfType<Button>().Where(button => button.IsVisibleInTree())
                        .All(button => Encloses(ScreenRect(workspace), ScreenRect(button))),
                $"Research graph, inspector, or category tabs overflowed the workspace at {size}.");
            Require(Descendants(workspace).OfType<Label>().Where(l => l.IsVisibleInTree())
                .All(l => l.GetThemeFontSize("font_size") >= 10), "Compact layout reduced text below its readable minimum.");
            AssertInsideViewport(_drawer, "responsive operations page");
            foreach (var button in Descendants(_main.GetNode("CampaignSidebar/NavigationRail")).OfType<Button>())
                AssertInsideViewport(button, "responsive icon rail");
            var filename = size.Y switch { 1080 => "30-responsive-1080p.png", 1440 => "31-responsive-1440p.png",
                2160 => "32-responsive-4k.png", _ => "33-responsive-720p.png" };
            await SaveViewportAsync(filename, size.X, size.Y);
            if (previousVoiceSettings is not null)
            {
                _main.UiVoice?.Stop(); _main.UiVoice?.ApplySettings(previousVoiceSettings);
            }
            await CloseDrawerAsync();
            await ClickButtonAsync(_dock, "Home"); await WaitForCameraAsync();
            var home = _main.UiSelectedSystemId;
            var other = _main.UiSpatialCatalog.Where(s => s.SystemId != home)
                .First(s => new Rect2(150, 190, logical.X - 500, logical.Y - 340).HasPoint(StarPoint(s.SystemId)) &&
                    StarPoint(s.SystemId).DistanceTo(StarPoint(home)) > 35);
            await ClickPositionAsync(StarPoint(other.SystemId), MouseButton.Left);
            Require(_main.UiSelectedSystemId == other.SystemId, "Scaled mouse hit testing missed the alternate star.");
            await ClickPositionAsync(StarPoint(home), MouseButton.Left);
            Require(_main.UiSelectedSystemId == home, "Scaled mouse hit testing missed the home star.");
        }
        Check(true, "responsive-720p-1080p-1440p-4k-reflow-and-input");
    }

    private async Task ResizeResponsiveWindowAsync(Vector2I requested)
    {
        var window = GetWindow();
        // The capture intentionally measures exact client pixels. A prior fullscreen video
        // preference must not turn a Size assignment into a monitor-sized no-op.
        window.Mode = Window.ModeEnum.Windowed;
        window.Borderless = false;
        await WaitFramesAsync(2);
        var visibleCapture = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_VISIBLE") == "1";
        var usable = DisplayServer.ScreenGetUsableRect(window.CurrentScreen);
        // Visible evidence remains a real, restorable desktop window. The 1440p and
        // 4K checks also require real render textures at those exact dimensions, so
        // allow the client area to extend past a smaller monitor temporarily rather
        // than substituting a lower-resolution image with a 4K label.
        if (visibleCapture) window.Position = usable.Position;
        window.Size = requested;
        var expected = requested.Y > 1080 ? new Vector2(1920, 1080) : (Vector2)requested;
        for (var frame = 0; frame < 90; frame++)
        {
            await WaitFramesAsync(1);
            var logical = GetViewport().GetVisibleRect().Size;
            if (frame is 0 or 14 or 44 or 89) GD.Print($"STELLAR_RESPONSIVE_RESIZE {ResponsiveDiagnostics(requested)} frame={frame + 1}");
            if (window.Size == requested && window.ContentScaleSize == (Vector2I)expected && logical == expected)
                return;
        }
        throw new InvalidOperationException($"Responsive resize did not settle after 90 frames; {ResponsiveDiagnostics(requested)}");
    }

    private string ResponsiveDiagnostics(Vector2I requested)
    {
        var window = GetWindow();
        var viewport = GetViewport();
        var texture = viewport.GetTexture();
        return $"requested={requested}, window={window.Size}, position={window.Position}, content-scale-size={window.ContentScaleSize}, " +
            $"content-scale-mode={window.ContentScaleMode}, content-scale-factor={window.ContentScaleFactor:0.###}, " +
            $"window-mode={window.Mode}, visible={viewport.GetVisibleRect().Size}, render-texture={texture.GetSize()}";
    }
}
