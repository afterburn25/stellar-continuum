using System;
using System.Linq;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Presentation.Spatial;
using Game.Simulation.Knowledge;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyRegionalMapVisualsAsync(MainMenuLayer menu)
    {
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        if (!_main.UiIsPaused) await PressKeyAsync(Key.Space);
        var window = GetWindow();
        foreach (var size in new[] { new Vector2I(1280, 720), new Vector2I(1920, 1080) })
        {
            window.ContentScaleSize = size;
            window.Size = size;
            await WaitFramesAsync(12);
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();
            var home = _main.UiSelectedSystemId;
            await ClickControlAsync(Descendants(_main).OfType<Button>().Single(b => b.Name == "SpatialOverview"));
            await WaitForCameraAsync();
            var artwork = _main.UiGalaxyArtworkScreenRect;
            Require(_main.UiOverviewBlend > .99f && _main.UiGalaxyDeepFieldOpacity >= .44f &&
                    _main.UiHasVisibleGalaxyArtwork && artwork.Size.X > 100 &&
                    Math.Abs(artwork.Size.X - artwork.Size.Y) < 1 && CatalogFitsVisibleGalaxyDisc(artwork),
                "Whole-galaxy view lost its fitted galaxy backdrop or distant galaxies.");
            await SaveViewportAsync($"regional-{size.Y}-01-overview.png", 0, 0);
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();
            Require(_main.UiGalaxyDeepFieldOpacity == 0 && _main.UiRegionalBackdropOpacity > .99f &&
                _main.UiRegionalBackdropStarCount is >= 300 and <= 500,
                "Regional sky must replace distant galaxies with bounded stars, clusters and nebula.");
            Require(_main.UiCatalogStarRadius(home) >= 12,
                "Default regional stars still use the old tiny symbol size.");
            Require(_main.UiVisibleRegionalPointCount > 0 &&
                _main.UiVisibleRegionalPointCount <= _main.UiSpatialCatalog.Count &&
                !Descendants(_main).OfType<TextureRect>().Any(sprite => sprite.Name.ToString().StartsWith("RegionalPhotosphere")) &&
                _main.UiCatalogStarCoreRadius(home) <= 4,
                "Regional stars must use a bounded point-flare renderer with a small hot core.");
            RequireRegionalPointLight(home);
            await SaveViewportAsync($"regional-{size.Y}-02-region.png", 0, 0);

            // A lingering home selection must never force entry while zooming empty sky.
            var safe = new Rect2(150, 240, size.X - 530, size.Y - 380);
            var controls = Descendants(_main).OfType<Button>().Where(button => button.IsVisibleInTree())
                .Select(button => ScreenRect(button).Grow(12)).ToArray();
            var anchor = Enumerable.Range(0, 30).Select(i => safe.Position + new Vector2(
                    30 + i % 6 * (safe.Size.X - 60) / 5, 30 + i / 6 * (safe.Size.Y - 60) / 4))
                .Where(point => !controls.Any(bounds => bounds.HasPoint(point)))
                .OrderByDescending(point => PublicCatalogIds().Min(id => point.DistanceTo(StarPoint(id)))).First();
            await ClickPositionAsync(anchor, MouseButton.Left);
            Require(_main.UiSelectedSystemId < 0, "Empty-sky click did not clear the regional selection.");
            var camera = ObserveCamera();
            var worldAnchor = (anchor - _main.UiMapOriginScreen) / camera.Zoom;
            for (var step = 0; _main.UiMapZoom < _main.UiRegionalMaximumZoom; step++)
            {
                Require(step < 36, "Free regional zoom failed to reach its extended limit.");
                await WheelAsync(true, anchor);
                Require(!_main.UiIsSystemSpatialView && _main.UiSelectedSystemId < 0,
                    "Unselected empty-sky zoom entered a system or fabricated a selection.");
                Require((worldAnchor * _main.UiMapZoom + _main.UiMapOriginScreen).DistanceTo(anchor) < 1,
                    "Free regional zoom stopped following the cursor.");
            }
            Require(_main.UiMapZoom > 96 && _main.UiGalaxyDeepFieldOpacity == 0,
                "Free regional zoom retained the old shallow cap or distant galaxies.");
            await SaveViewportAsync($"regional-{size.Y}-03-free-zoom.png", 0, 0);
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();

            // Unknown systems may be approached but cannot disclose orbital data.
            await VerifyUnknownEntryPrivacyAsync(home, $"-{size.Y}");
            Require(_main.UiMapZoom >= _main.UiRegionalMaximumZoom - .01f &&
                _main.UiCatalogStarRadius(_main.UiSelectedSystemId) >= 40 &&
                _main.UiCatalogStarCoreRadius(_main.UiSelectedSystemId) >= 18,
                "Unknown star close approach did not reach a useful safe inspection scale.");
            await SaveViewportAsync($"regional-{size.Y}-04-unknown-close.png", 0, 0);
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();

            // Approach Sol with another star selected; no click/focus prerequisite.
            await SelectDifferentStarAsync(home);
            for (var step = 0; !_main.UiIsSystemSpatialView; step++)
            {
                Require(step < 24, "Known-star wheel approach failed without prior selection.");
                await WheelAsync(true, StarPoint(home));
                if (!_main.UiIsSystemSpatialView && _main.UiMapZoom is > 7 and < 10)
                    await SaveViewportAsync($"regional-{size.Y}-05-approach.png", 0, 0);
            }
            var canvas = _main.GetNode<SystemSpatialCanvas>("SystemSpatialCanvas");
            Require(_main.UiSelectedSystemId == home && canvas.IsStarFocused &&
                _main.UiGalaxyDeepFieldOpacity == 0, "Regional approach did not arrive at the correct detailed star.");
            Require(_main.UiVisibleRegionalPointCount == 0,
                "Regional star sprites remained visible over the system close-up.");
            var distance = canvas.Scene.TargetDistance;
            await WheelAsync(true, GetViewport().GetVisibleRect().Size * .5f);
            Require(canvas.IsStarFocused && canvas.Scene.TargetDistance < distance,
                "Stellar arrival could not zoom further into the photosphere.");
            await SaveViewportAsync($"regional-{size.Y}-06-sun-close.png", 0, 0);
            await ClickButtonAsync(_dock, "Back to Region");
            await WaitForCameraAsync();
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();
            await ClickButtonAsync(_dock, "Open System");
            await WaitForCameraAsync();
            Require(_main.UiIsSystemSpatialView && !canvas.IsDetailedFocus,
                "Explicit system navigation no longer opens the 2D orbital view.");
            Require(_main.UiVisibleRegionalPointCount == 0,
                "Regional star sprites remained visible over the orbital map.");
            await ClickButtonAsync(_dock, "Back to Region");
            await WaitForCameraAsync();
            GD.Print($"STELLAR_REGIONAL_MAP_EVIDENCE resolution={size} freeZoom={_main.UiRegionalMaximumZoom} entryZoom={_main.UiRegionalSystemEntryZoom} stars={_main.UiRegionalBackdropStarCount}");
        }
    }

    private void RequireRegionalPointLight(int systemId)
    {
        var center = StarPoint(systemId);
        using var image = GetViewport().GetTexture().GetImage();
        Color Sample(Vector2 offset)
        {
            var point = center + offset;
            return image.GetPixel((int)point.X, (int)point.Y);
        }
        float Brightness(Color color) => (color.R + color.G + color.B) / 3;
        var hotCore = Sample(Vector2.Zero);
        var halo = Sample(new Vector2(7, 4));
        var sky = Sample(new Vector2(45, 29));
        Require(Math.Min(hotCore.R, Math.Min(hotCore.G, hotCore.B)) > .60f &&
            Brightness(hotCore) > Brightness(halo) + .15f &&
            Brightness(halo) > Brightness(sky),
            $"Regional Sol must have a small white-hot core tapering into a halo, not a solar disc: core={hotCore}, halo={halo}, sky={sky}");
        Require(halo.R > halo.B,
            $"Regional Sol's halo lost its warm physical hue: {halo}");
    }
}
