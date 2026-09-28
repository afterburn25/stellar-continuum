using System;
using System.Linq;
using System.Threading.Tasks;
using Game.Presentation.Spatial;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyFreeSystemZoomAsync(SystemSpatialCanvas canvas)
    {
        foreach (var size in new[] { new Vector2I(1280, 720), new Vector2I(1920, 1080) })
        {
            GetWindow().ContentScaleSize = size;
            GetWindow().Size = size;
            await WaitFramesAsync(12);
            await ClickButtonAsync(_dock, "Home");
            await ClickButtonAsync(_dock, "Open System");
            await WaitForCameraAsync();
            Require(!canvas.IsDetailedFocus, "system opened in forced focus");
            var pluto = canvas.VisibleBodies.Single(body => body.Label == "Pluto");
            Require(pluto.OrbitalEccentricity > .24f && pluto.OrbitalInclinationDegrees > 17f,
                "Pluto lost its distinctive observer-safe orbit");
            var earth = canvas.VisibleBodies.Single(body => body.Label == "Earth");
            var jupiter = canvas.VisibleBodies.Single(body => body.Label == "Jupiter");
            Require(jupiter.DisplayRadius > earth.DisplayRadius * 7,
                "Jupiter was flattened to a terrestrial display size");
            var lanes = canvas.GetLocalLanes!.Invoke();
            Require(lanes.All(lane => canvas.GetLaneMarkerBoundaryClearance(lane.DestinationSystemId) > 1),
                "gate or label intruded into the outer boundary");
            VerifyRenderedBodiesOnOrbits(canvas);
            await SaveViewportAsync($"system-scale-{size.Y}-overview.png", 0, 0);

            var zoomAnchor = new Vector2(canvas.Camera.OriginX, canvas.Camera.OriginY);
            await WheelAsync(false, zoomAnchor);
            await WheelAsync(false, zoomAnchor);
            VerifyRenderedBodiesOnOrbits(canvas);
            await SaveViewportAsync($"system-scale-{size.Y}-zoomed-out-orbits.png", 0, 0);
            await WheelAsync(true, zoomAnchor);
            await WheelAsync(true, zoomAnchor);
            VerifyRenderedBodiesOnOrbits(canvas);

            var empty = new Vector2(size.X * .36f, size.Y * .68f);
            await ClickPositionAsync(empty, MouseButton.Left);
            Require(canvas.SelectedBodyId is null, "free-zoom fixture did not clear its selection");
            var gateSize = canvas.GetLaneMarkerBodySize(lanes[0].DestinationSystemId);
            var before = canvas.Camera.Scale;
            var world = new Vector2((empty.X - canvas.Camera.OriginX) / before,
                (empty.Y - canvas.Camera.OriginY) / before);
            for (var step = 0; step < 12; step++) await WheelAsync(true, empty);
            Require(!canvas.IsDetailedFocus && canvas.SelectedBodyId is null && canvas.Camera.Scale > before * 8,
                "unselected wheel zoom stopped early or forced a focus");
            Require(canvas.GetLaneMarkerBodySize(lanes[0].DestinationSystemId) is { } zoomedSize &&
                gateSize is { } fixedSize && zoomedSize.DistanceTo(fixedSize) < .02f && Math.Abs(fixedSize.X - 40f) < .1f,
                "travel arrows changed screen size when zooming");
            var anchored = new Vector2(canvas.Camera.OriginX, canvas.Camera.OriginY) + world * canvas.Camera.Scale;
            Require(anchored.DistanceTo(empty) < 1, "free zoom drifted away from the pointer");
            var origin = new Vector2(canvas.Camera.OriginX, canvas.Camera.OriginY);
            await DragAsync(empty, empty + new Vector2(60, -30), MouseButton.Left);
            Require(!canvas.IsDetailedFocus && new Vector2(canvas.Camera.OriginX, canvas.Camera.OriginY).DistanceTo(origin) > 40,
                "free zoom prevented left-drag panning");

            await ClickButtonAsync(_dock, "Home");
            await ClickButtonAsync(_dock, "Open System");
            await WaitForCameraAsync();
            var anchor = BodyPoint(3);
            for (var step = 0; step < 37; step++)
            {
                var scaleBefore = canvas.Camera.Scale;
                await WheelAsync(true, anchor);
                Require(BodyPoint(3).DistanceTo(anchor) < 1,
                    $"Earth drifted on free wheel {step}: before={scaleBefore} after={canvas.Camera.Scale} anchor={anchor} body={BodyPoint(3)} hovered={GetViewport().GuiGetHoveredControl()?.GetPath()}");
            }
            Require(!canvas.IsDetailedFocus && canvas.SelectedBodyId is null,
                "zooming over Earth entered focused mode without a click");
            Require(BodyPoint(3).DistanceTo(anchor) < 1 && earth.DisplayRadius * canvas.Camera.Scale > 200,
                "unfocused zoom could not show a large, pointer-anchored Earth");
            await SaveViewportAsync($"system-scale-{size.Y}-free-earth-close.png", 0, 0);
        }
        GD.Print("STELLAR_SYSTEM_SCALE_ACCEPTANCE_COMPLETE");
    }

    private static void VerifyRenderedBodiesOnOrbits(SystemSpatialCanvas canvas)
    {
        var layout = new SystemSpatialViewport(canvas.Camera.OriginX, canvas.Camera.OriginY, canvas.Camera.Scale);
        var primaryBodies = canvas.VisibleBodies.Where(body => body.ParentBodyId is null).ToArray();
        foreach (var body in primaryBodies)
        {
            var sprite = canvas.GetNode<FocusedPlanetView>($"OrbitalBody{body.BodyId}");
            var renderedCenter = sprite.Position + sprite.Size * .5f;
            var expected = new Vector2(layout.CenterX, layout.CenterY) +
                new Vector2(body.OffsetX, body.OffsetY) * layout.Scale;
            Require(sprite.Visible && renderedCenter.DistanceTo(expected) < .5f,
                $"{body.Label} left its orbital position while zooming");
            Require(Math.Abs(sprite.Size.X * .5f - layout.BodyRadius(body)) < .1f,
                $"{body.Label} kept a separate enlarged sprite size");
            foreach (var other in primaryBodies.Where(other => other.BodyId > body.BodyId))
            {
                var distance = new Vector2(body.OffsetX - other.OffsetX, body.OffsetY - other.OffsetY).Length() * layout.Scale;
                var extent = layout.BodyRadius(body) * (body.SurfaceKey == "saturn" ? 2.8f : 1f) +
                    layout.BodyRadius(other) * (other.SurfaceKey == "saturn" ? 2.8f : 1f);
                Require(distance > extent, $"{body.Label} and {other.Label} bunched together when zoomed out");
            }
        }
        GD.Print("STELLAR_UI_CHECK_PASS rendered-planets-remain-separated-on-orbits");
    }
}
