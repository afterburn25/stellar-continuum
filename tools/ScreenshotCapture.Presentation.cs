using System;
using System.Linq;
using System.Threading.Tasks;
using Godot;
using Game.Presentation;
using Game.Presentation.Spatial;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyPlanetInspectorAsync()
    {
        var panel = Descendants(_main).OfType<PlanetInspectorPanel>().Single();
        Require(panel.IsVisibleInTree() && panel.DisplayedBodyId == 3, "Selecting Earth did not populate its right-hand inspector.");
        AssertInsideViewport(panel, "planet inspector");
        Require(Descendants(panel).OfType<Label>().Single(x => x.Name == "StatGravity").Text == "9.81 m/s²",
            "Earth's gravity did not appear in its labeled stat row.");
        var camera = ObserveCamera();
        await ClickPositionAsync(ScreenRect(panel).Position + new Vector2(5, 120), MouseButton.WheelDown);
        Require(SameCamera(camera, ObserveCamera()), "Scrolling the planet inspector changed the system camera.");
        await ClickNamedButtonAsync(panel, "InspectorWorlds");
        var moon = _main.UiSystemBodies.First(b => b.Label == "Moon" && b.ParentBodyId == 3);
        await ClickNamedButtonAsync(panel, "InspectorWorld" + moon.BodyId);
        Require(panel.DisplayedBodyId == moon.BodyId && _main.UiSelectedBodyId == moon.BodyId,
            "Selecting a moon in the right-hand list did not update the map and details together.");
        await ClickNamedButtonAsync(panel, "InspectorWorlds");
        await ClickNamedButtonAsync(panel, "InspectorWorld3");
        Check(panel.DisplayedBodyId == 3 && _main.UiSelectedBodyId == 3,
            "planet-inspector-organized-stats-and-mouse-selection");
    }

    private async Task VerifyMetricPlanetInspectorAsync(MainMenuLayer menu)
    {
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        if (!_main.UiIsPaused) await PressKeyAsync(Key.Space);
        await ClickButtonAsync(_dock, "Home");
        await WaitForRefreshAsync();
        await ClickButtonAsync(_dock, "Open System");
        await WaitForCameraAsync();
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left);
        await WaitForRefreshAsync();
        var panel = Descendants(_main).OfType<PlanetInspectorPanel>().Single();
        Require(panel.DisplayedBodyId == 3 && panel.IsVisibleInTree(),
            "metric inspector did not open Earth details");
        Require(Descendants(panel).OfType<Label>().Single(x => x.Name == "StatRadius").Text == "6,371 km" &&
                Descendants(panel).OfType<Label>().Single(x => x.Name == "StatMass").Text.Contains("× 10²⁴ kg", StringComparison.Ordinal) &&
                Descendants(panel).OfType<Label>().Single(x => x.Name == "StatGravity").Text == "9.81 m/s²",
            "metric inspector did not show complete SI physical rows");
        foreach (var size in new[] { new Vector2I(1280, 720), new Vector2I(1920, 1080) })
        {
            GetWindow().Size = size;
            await WaitFramesAsync(15);
            Require(GetViewport().GetVisibleRect().Size == size, $"metric inspector viewport did not resize to {size}");
            AssertInsideViewport(panel, $"metric planet inspector {size.Y}p");
            await SaveViewportAsync($"metric-planet-inspector-{size.Y}p.png", size.X, size.Y);
        }
        GetWindow().Size = new Vector2I(1280, 720);
        Check(true, "metric-planet-inspector-720p-1080p");
    }

    private async Task VerifyLocalSkySceneryAsync()
    {
        // Render the real local-sky component independently so different system
        // environments can be reviewed without granting survey knowledge to a player.
        var layer = new CanvasLayer { Layer = 1000 };
        AddChild(layer);
        var sky = new SystemSkyBackdrop { Size = new Vector2(1280, 720) };
        layer.AddChild(sky);
        sky.SetSystem(0);
        await WaitFramesAsync(4);
        var first = GetViewport().GetTexture().GetImage().GetData();
        await SaveViewportAsync("26-system-sky-sol.png");
        sky.SetSystem(37);
        await WaitFramesAsync(4);
        var different = GetViewport().GetTexture().GetImage().GetData();
        Require(!first.SequenceEqual(different) && sky.StarCount >= 760,
            "Different systems received the same sky or lost their starfield.");
        await SaveViewportAsync("27-system-sky-variant.png");
        sky.SetSystem(0);
        await WaitFramesAsync(4);
        Require(first.SequenceEqual(GetViewport().GetTexture().GetImage().GetData()),
            "Returning to a system changed its stable local sky.");
        Check(true, "system-skies-distinct-and-stable-on-return");
        layer.QueueFree(); await WaitFramesAsync(2);
    }
}
