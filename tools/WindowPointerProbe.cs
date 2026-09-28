using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Linq;
using Godot;
using Game.Presentation;

namespace Game.Tools;

/// <summary>Windows client-message acceptance test, independent of Input.ParseInputEvent.
/// It does not move the user's cursor or send input to any other window.</summary>
public partial class WindowPointerProbe : Node
{
    [DllImport("user32.dll")] private static extern bool PostMessageW(nint window, uint message, nuint wParam, nint lParam);
    private Button _target = null!;
    private bool _activated;
    private Main _main = null!;
    public override async void _Ready()
    {
        try
        {
            if (OS.GetName() != "Windows") throw new InvalidOperationException("This probe requires Windows.");
            GetWindow().Title = "Stellar Continuum — pointer acceptance";
            _main = GD.Load<PackedScene>("res://scenes/Main.tscn").Instantiate<Main>();
            AddChild(_main);
            await Settle();
            await Click(_main.FindChild("ResumeCampaign", true, false) as Button ?? throw new InvalidOperationException("Resume button missing."));
            if (!_main.UiIsPaused) await RightClick(_main.FindChild("SimulationPlaybackButton", true, false) as Button ?? throw new InvalidOperationException("Playback button missing."));
            foreach (var size in new[] { new Vector2I(1280, 720), new Vector2I(1920, 1080), new Vector2I(1280, 720) })
            {
                GetWindow().Size = size;
                await Settle();
                await Probe();
            }
            GD.Print("STELLAR_NATIVE_POINTER_PROBE_COMPLETE");
            _main.UiVoice?.Stop();
            await AudioDirector.ShutdownAndQuitAsync(GetTree(), 0);
        }
        catch (Exception error) { GD.PushError(error.ToString()); _main?.UiVoice?.Stop(); await AudioDirector.ShutdownAndQuitAsync(GetTree(), 1); }
    }
    private async Task Settle()
    {
        await ToSignal(GetTree().CreateTimer(.3), SceneTreeTimer.SignalName.Timeout);
        await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
    }
    private async Task Probe()
    {
        var viewport = GetViewport();
        var logical = viewport.GetVisibleRect().Size;
        var physical = (Vector2)GetWindow().Size;
        GD.Print($"POINTER_LAYOUT native={physical} logical={logical} transform={viewport.GetFinalTransform()}");
        var playback = _main.FindChild("SimulationPlaybackButton", true, false) as Button
            ?? throw new InvalidOperationException("Compact playback button missing.");
        await Click(playback);
        if (_main.UiIsPaused) throw new InvalidOperationException("Native left-click did not start compact playback.");
        await RightClick(playback);
        foreach (var section in new[] { "Research", "Construction", "Ships" })
        {
            await Click(_main.GetNode<Button>("CampaignSidebar/NavigationRail/NavigationScroll/Items/Nav" + section));
            await Settle();
            if (!_main.GetNode<CampaignSidebar>("CampaignSidebar").IsDrawerOpen)
                throw new InvalidOperationException("Native rail click did not open operations page.");
            await Click(_main.GetNode<Button>("CampaignSidebar/DetailDrawer/Body/Header/DrawerClose"));
        }
    }
    private async Task Click(Button target)
    {
            _target = target;
            await Settle();
            var viewport = GetViewport();
            var logical = viewport.GetVisibleRect().Size;
            var physical = (Vector2)GetWindow().Size;
            // Derive the rendered client location from actual native/logical extents,
            // not the input transform under test. No test-only input rescaling.
            var client = _target.GetGlobalRect().GetCenter() * physical / logical;
            _activated = false;
            void Activate() => _activated = true;
            target.Pressed += Activate;
            var handle = (nint)DisplayServer.WindowGetNativeHandle(DisplayServer.HandleType.WindowHandle);
            var packed = (nint)(((int)client.Y << 16) | ((int)client.X & 0xffff));
            PostMessageW(handle, 0x200, 0, packed);
            PostMessageW(handle, 0x201, 1, packed);
            PostMessageW(handle, 0x202, 0, packed);
            await Settle();
            target.Pressed -= Activate;
            if (!_activated) throw new InvalidOperationException($"Native click missed visible button: client={client}, logical={_target.Position}, native={physical}, final={viewport.GetFinalTransform()}, mouse={viewport.GetMousePosition()}");
            GD.Print($"STELLAR_NATIVE_POINTER_PASS {physical} at {client}");
    }
    private async Task RightClick(Button target)
    {
        await Settle();
        var logical = GetViewport().GetVisibleRect().Size;
        var physical = (Vector2)GetWindow().Size;
        var client = target.GetGlobalRect().GetCenter() * physical / logical;
        var handle = (nint)DisplayServer.WindowGetNativeHandle(DisplayServer.HandleType.WindowHandle);
        var packed = (nint)(((int)client.Y << 16) | ((int)client.X & 0xffff));
        PostMessageW(handle, 0x204, 2, packed);
        PostMessageW(handle, 0x205, 0, packed);
        await Settle();
        if (!_main.UiIsPaused) throw new InvalidOperationException("Native right-click did not immediately pause compact playback.");
    }
}
