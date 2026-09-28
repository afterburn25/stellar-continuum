using System;
using System.Linq;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Presentation.Spatial;
using Game.Simulation.Generation;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    /// <summary>Real-input receipt for the default full galaxy and the retained nearby profile.
    /// It deliberately inspects public map output only, so unknown systems never reveal hidden world facts.</summary>
    private async Task VerifyNearbyCatalogAsync(MainMenuLayer menu)
    {
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        if (!_main.UiIsPaused) await PressKeyAsync(Key.Space);

        var fullGalaxy = _main.UndisclosedCoreScreenPosition.HasValue &&
            Math.Abs(_main.UiCatalogVisualCoordinateScale - 14.0f) < .001f;
        var nearbyCompatibility = _main.UiOverviewName == "Galaxy" &&
            !_main.UndisclosedCoreScreenPosition.HasValue && Math.Abs(_main.UiCatalogVisualCoordinateScale - 14.0f) < .001f;
        var expectNearby = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_EXPECT_NEARBY") == "1";
        Require(_main.UiSpatialCatalog.Count == 500 && (expectNearby ? nearbyCompatibility : fullGalaxy),
            "Fresh Player campaign did not use the 500-system full-galaxy presentation or explicit nearby compatibility profile.");
        var wolf359 = fullGalaxy ? FullGalaxyMeasuredSystemId("Wolf 359") : -1;
        if (fullGalaxy)
        {
            Require(FullGalaxyStellarPopulation.MeasuredSystemCount == 96 &&
                    FullGalaxyStellarPopulation.MeasuredStars.Count == 96 &&
                    wolf359 >= 0 && _main.UiSpatialCatalog.Any(system => system.SystemId == wolf359),
                "Full 500-system galaxy did not retain its 96 measured nearby stars and Wolf 359 identity.");
        }
        Require(_main.UiSpatialCatalog.Any(system => system.SurveyLevel == SystemSurveyLevel.Unknown),
            "Galaxy catalogue validation requires unknown stars to retain survey privacy.");

        var window = GetWindow();
        foreach (var size in new[] { new Vector2I(1280, 720), new Vector2I(1920, 1080) })
        {
            window.ContentScaleSize = size;
            window.Size = size;
            await WaitFramesAsync(12);
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();
            var home = _main.UiSelectedSystemId;
            var homeFact = _main.UiSelectedSystemIntelligence.Facts.Single(fact => fact.Label == "DISTANCE FROM HOMEWORLD");
            Require(homeFact.Value.Contains("0 ly", StringComparison.Ordinal), "Home system does not display its zero-distance reference.");

            await ClickControlAsync(Descendants(_main).OfType<Button>().Single(button => button.Name == "SpatialOverview"));
            await WaitForCameraAsync();
            var artwork = _main.UiGalaxyArtworkScreenRect;
            Require(_main.UiSpatialScale == SpatialPresentationScale.GalaxyOverview && PublicCatalogFits() &&
                    _main.UiGalaxyDeepFieldOpacity >= .44f && _main.UiHasVisibleGalaxyArtwork &&
                    artwork.Size.X > 100 && Math.Abs(artwork.Size.X - artwork.Size.Y) < 1 &&
                    CatalogFitsVisibleGalaxyDisc(artwork),
                $"The complete {(fullGalaxy ? "full-galaxy" : "nearby")} catalogue did not fit the usable {size.Y}p overview ellipse.");
            Require(FullGalaxyArtworkFits(),
                "Galaxy artwork left a visible frame around the playable overview.");
            if (fullGalaxy)
            {
                Require(_main.UiCatalogStarRadius(wolf359) is >= 3 and <= 3.3f &&
                        _main.UiCatalogStarCoreRadius(wolf359) <= 4.4f && StarPoint(wolf359).DistanceTo(artwork.GetCenter()) > 0,
                    "Measured Wolf 359 did not use the bounded close-star point treatment in the full galaxy.");
            }
            await SaveViewportAsync($"nearby-{size.Y}-01-overview.png", 0, 0);

            await ClickControlAsync(Descendants(_main).OfType<Button>().Single(button => button.Name == "SpatialRegion"));
            await WaitForCameraAsync();
            Require(_main.UiGalaxyDeepFieldOpacity == 0 && _main.UiRegionalBackdropOpacity > .99f &&
                    _main.UiVisibleRegionalPointCount > 0 && _main.UiCatalogStarCoreRadius(home) <= 4.4f,
                "Regional nearby-star view did not preserve point-star rendering and the local sky.");

            var anchor = StarPoint(home);
            var before = ObserveCamera();
            var displayWorld = (anchor - _main.UiMapOriginScreen) /
                (before.Zoom * _main.UiCatalogVisualCoordinateScale);
            await WheelAsync(true, anchor);
            var after = ObserveCamera();
            var reconstructedAnchor = _main.UiMapOriginScreen + displayWorld *
                (after.Zoom * _main.UiCatalogVisualCoordinateScale);
            Require(reconstructedAnchor.DistanceTo(anchor) < 1 && StarPoint(home).DistanceTo(anchor) < 1,
                "Cursor-anchored nearby-catalog zoom lost alignment with its visual coordinate scale.");

            var dragEnd = StarPoint(home) + new Vector2(38, -24);
            await DragAsync(StarPoint(home), dragEnd, MouseButton.Left);
            await WaitForCameraAsync();
            Require(StarPoint(home).DistanceTo(dragEnd) < 1,
                "Nearby-catalog pan did not preserve the transformed star position.");
            await ClickPositionAsync(StarPoint(home), MouseButton.Left);
            Require(_main.UiSelectedSystemId == home,
                "Nearest known nearby-catalog star was not selected by its rendered point.");

            await ClickButtonAsync(_dock, "Open System");
            await WaitForCameraAsync();
            var canvas = _main.GetNode<SystemSpatialCanvas>("SystemSpatialCanvas");
            Require(_main.UiIsSystemSpatialView && !canvas.IsDetailedFocus && _main.UiVisibleRegionalPointCount == 0,
                "Opening a known nearby-catalog star changed the ordinary system-sun presentation.");
            await ClickButtonAsync(_dock, "Back to Region");
            await WaitForCameraAsync();
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();
            var wolfId = fullGalaxy ? wolf359 : NearbyStarCatalog.Stars.Select((star, index) => (star, index))
                .Single(item => item.star.Name == "Wolf 359").index;
            Require(wolfId >= 0 && StarMapDiscGeometry.For(StellarPrimaryClass.MRedDwarf, 192f).CoreRadius >= 27f,
                "Wolf 359 fixture or M-dwarf close-disc geometry changed unexpectedly.");
            await ClickPositionAsync(StarPoint(wolfId), MouseButton.Left);
            Require(_main.UiSelectedSystemId == wolfId, "Wolf 359 close-frame fixture could not select its catalogue point.");
            var distanceFact = _main.UiSelectedSystemIntelligence.Facts.Single(fact => fact.Label == "DISTANCE FROM HOMEWORLD");
            Require(distanceFact.Value.Contains("7.8 ly", StringComparison.Ordinal) && distanceFact.Value.Contains("pc", StringComparison.Ordinal),
                "Selected Wolf 359 does not display its measured distance from Earth/Sol.");
            await WaitFramesAsync(40);
            var visibleDistance = Descendants(_main).OfType<Label>().Single(label => label.Name == "OverviewHomeDistance");
            Require(visibleDistance.IsVisibleInTree() && visibleDistance.Text == distanceFact.Value &&
                    Encloses(GetViewport().GetVisibleRect(), ScreenRect(visibleDistance)),
                "The selected-star home distance is not visible immediately in the right-hand overview.");
            await SaveViewportAsync($"nearby-{size.Y}-wolf-region.png", 0, 0);
            var nextCaptureZoom = 2f;
            for (var step = 0; _main.UiMapZoom < _main.UiRegionalMaximumZoom - .01f; step++)
            {
                Require(step < 40, "Wolf 359 close-frame fixture did not reach maximum regional zoom.");
                await WheelAsync(true, StarPoint(wolfId));
                if (_main.UiMapZoom >= nextCaptureZoom)
                {
                    await SaveViewportAsync($"nearby-{size.Y}-wolf-zoom-{nextCaptureZoom:0}.png", 0, 0);
                    nextCaptureZoom *= 4;
                }
                Require(!_main.UiIsSystemSpatialView, "Wolf 359 close-frame fixture unexpectedly entered hidden orbital detail.");
            }
            Require(_main.UiCatalogStarCoreRadius(wolfId) >= 27f &&
                    _main.UiCatalogStarRadius(wolfId) >= 64f,
                "Wolf 359 did not render as a materially enlarged red-dwarf disc at close regional zoom.");
            await SaveViewportAsync($"nearby-{size.Y}-02-wolf-359-close.png", 0, 0);
            GD.Print($"STELLAR_CLASS_SIZE_EVIDENCE system=Wolf 359 class=MRedDwarf core={_main.UiCatalogStarCoreRadius(wolfId):0.0} halo={_main.UiCatalogStarRadius(wolfId):0.0} zoom={_main.UiMapZoom:0.0}");
            await ClickButtonAsync(_dock, "Home");
            await WaitForCameraAsync();
            await VerifyUnknownEntryPrivacyAsync(home, $"-nearby-{size.Y}");
            await SaveViewportAsync($"nearby-{size.Y}-03-region.png", 0, 0);
        }

        GD.Print(fullGalaxy
            ? "FULL_GALAXY_CATALOG_EVIDENCE systems=500 measured=96 profile=full-galaxy visualCoordinateScale=14"
            : "NEARBY_CATALOG_EVIDENCE systems=500 profile=solar-neighborhood visualCoordinateScale=14");
    }

    private static int FullGalaxyMeasuredSystemId(string name)
    {
        var expected = FullGalaxyStellarPopulation.MeasuredStars.Single(star =>
            string.Equals(star.Name, name, StringComparison.OrdinalIgnoreCase));
        var systemId = FullGalaxyStellarPopulation.MeasuredStars
            .Select((star, index) => (star, index))
            .Single(candidate => candidate.star.HygId == expected.HygId &&
                                 string.Equals(candidate.star.Name, expected.Name, StringComparison.OrdinalIgnoreCase)).index;
        return systemId;
    }

    private bool CatalogFitsVisibleGalaxyDisc(Rect2 artwork)
    {
        var center = artwork.GetCenter();
        var horizontalRadius = artwork.Size.X * .5f * .81818182f;
        var verticalRadius = horizontalRadius * .72f;
        return PublicCatalogIds().All(id =>
        {
            var offset = StarPoint(id) - center;
            return offset.X * offset.X / (horizontalRadius * horizontalRadius) +
                offset.Y * offset.Y / (verticalRadius * verticalRadius) <= .96f;
        });
    }
}
