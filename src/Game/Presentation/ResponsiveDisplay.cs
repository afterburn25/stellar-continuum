using System;
using Godot;

namespace Game.Presentation;

/// <summary>1080p reference UI, readable 720p layout, and native-pixel high-DPI rendering.</summary>
public partial class ResponsiveDisplay : Node
{
    private bool _refreshQueued;
    public override void _Ready()
    {
        GetWindow().MinSize = new(1280, 720);
        GetWindow().SizeChanged += QueueScaleRefresh;
        QueueScaleRefresh();
    }
    public override void _ExitTree() => GetWindow().SizeChanged -= QueueScaleRefresh;

    private void QueueScaleRefresh()
    {
        if (_refreshQueued) return;
        _refreshQueued = true;
        // SizeChanged runs inside Godot's native window/render-attachment update.
        // Never change the logical viewport recursively while that update is active.
        Callable.From(RefreshScale).CallDeferred();
    }

    private void RefreshScale()
    {
        _refreshQueued = false;
        if (!IsInsideTree()) return;
        var window = GetWindow();
        var physical = window.Size;
        if (physical.X <= 0 || physical.Y <= 0) return;
        // At 720p use the full 1280x720 layout rather than shrinking 1080p text.
        // Above 1080p the canvas remains the reference size and is rasterized at
        // the native window resolution (4K = 2x), including text and GPU planets.
        var scale = Math.Max(1f, Math.Min(physical.X / 1920f, physical.Y / 1080f));
        var logical = new Vector2I((int)Math.Round(physical.X / scale), (int)Math.Round(physical.Y / scale));
        // The computed canvas already follows the actual client aspect ratio,
        // including maximized title bars. Do not add a second letterbox transform.
        window.ContentScaleAspect = Window.ContentScaleAspectEnum.Ignore;
        // SizeChanged can arrive before the native client area has finished its transition.
        // Compare the desired canvas itself rather than suppressing a repeated physical size:
        // the deferred callback then repairs a stale scale after a mode or DPI transition.
        if (window.ContentScaleSize != logical) window.ContentScaleSize = logical;
    }
}
