using System;
using System.IO;
using System.Threading.Tasks;
using Game.Presentation.Spatial;
using Game.Simulation.Models;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyPlanetLimbDiagnosticsAsync()
    {
        var earth = Marker(9301, "earth", PlanetaryAtmosphereRegime.OxygenNitrogen, SystemSpatialBodyVisualClass.Oceanic, true);
        var venus = Marker(9306, "venus", PlanetaryAtmosphereRegime.CarbonDioxideRich, SystemSpatialBodyVisualClass.HotRocky, true);
        var mercury = Marker(9302, "mercury", PlanetaryAtmosphereRegime.Vacuum, SystemSpatialBodyVisualClass.HotRocky, true);
        var rocky = Marker(9307, null, PlanetaryAtmosphereRegime.Vacuum, SystemSpatialBodyVisualClass.Rocky, true);
        var earthImage = await RenderPlanetMaterialAsync(earth, "planet-limbs-earth");
        var earth512 = await RenderPlanetMaterialAsync(earth, "planet-limbs-earth-512", 512);
        var venusImage = await RenderPlanetMaterialAsync(venus, "planet-limbs-venus");
        var mercuryImage = await RenderPlanetMaterialAsync(mercury, "planet-limbs-mercury");
        var rockyImage = await RenderPlanetMaterialAsync(rocky, "planet-limbs-rocky");
        Require(earthImage.GetPixel(128, 128).A > .98f && mercuryImage.GetPixel(128, 128).A > .98f,
            "planet material diagnostic found a transparent planetary centre");
        Require(earth512.GetPixel(256, 256).A > .98f && earth512.GetPixel(24, 142).A < .001f,
            "512-pixel planet material diagnostic did not preserve transparent exterior coverage");
        Require(mercuryImage.GetPixel(0, 0).A < .001f,
            "airless planet material left alpha outside its limb");
        // Light points upper-left for this marker. (13,82) is beyond the antialiased
        // geometric edge yet inside the illuminated atmospheric falloff; (8,80) is outside
        // that falloff. This uses p=(UV*2-1)*1.08 rather than an unlit rim.
        RequireIlluminatedAlphaFalloff(earthImage, mercuryImage);
        Require(earthImage.GetPixel(13, 82).A > .001f && mercuryImage.GetPixel(13, 82).A < .001f &&
                earthImage.GetPixel(8, 80).A < .001f,
            "illuminated Earth limb did not fade from atmosphere to transparent space continuously");
        var unknownImage = await RenderPlanetMaterialAsync(Marker(9303, "earth", PlanetaryAtmosphereRegime.OxygenNitrogen,
            SystemSpatialBodyVisualClass.UnknownPlanet, false), "planet-limbs-unknown");
        var knownCentre = earthImage.GetPixel(128, 128);
        var unknownCentre = unknownImage.GetPixel(128, 128);
        Require(MathF.Abs(unknownCentre.R - knownCentre.R) + MathF.Abs(unknownCentre.G - knownCentre.G) + MathF.Abs(unknownCentre.B - knownCentre.B) > .03f,
            "unknown body diagnostic rendered a known surface texture");
        var unknownOther = await RenderPlanetMaterialAsync(Marker(9303, "venus", PlanetaryAtmosphereRegime.OxygenNitrogen,
            SystemSpatialBodyVisualClass.UnknownPlanet, false), "planet-limbs-unknown-other-key");
        RequireImagesEqual(unknownImage, unknownOther,
            "unknown bodies changed their rendered appearance based on hidden texture keys");
        var vacuumEarth = earth with { Atmosphere = PlanetaryAtmosphereRegime.Vacuum };
        var vacuumImage = await RenderPlanetMaterialAsync(vacuumEarth, "planet-limbs-earth-vacuum");
        Require(vacuumImage.GetPixel(13, 82).A < .001f,
            "same-body atmosphere-to-vacuum refresh retained an atmospheric alpha shell");
        var materialGrid = new[] { earthImage, venusImage, mercuryImage, rockyImage };
        SaveCompositeBoard(materialGrid, "planet-limbs-material-diagnostic-grid-dark", new Color("07111c"));
        SaveCompositeBoard(materialGrid, "planet-limbs-material-diagnostic-grid-light", new Color("d8e3ef"));
        SaveCompositeBoard(materialGrid, "planet-limbs-material-diagnostic-grid-nebula", new Color("35244c"));
        Check(true, "native-planet-limb-material-alpha-readback");
    }

    private async Task<Image> RenderPlanetMaterialAsync(SystemSpatialBodyMarker marker, string name, int size = 256)
    {
        var viewport = new SubViewport { Size = new Vector2I(size, size), TransparentBg = true, RenderTargetUpdateMode = SubViewport.UpdateMode.Always };
        viewport.AddChild(new TextureRect { Texture = CelestialBodyMaterials.WhiteTexture, Material = CelestialBodyMaterials.GetPlanetMaterial(marker), Size = new Vector2(size, size), ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize });
        AddChild(viewport);
        await WaitFramesAsync(4);
        var image = viewport.GetTexture().GetImage();
        var path = Path.Combine(_outputDirectory, name + ".png");
        Require(image.SavePng(path) == Error.Ok, $"Could not write {name} material diagnostic.");
        _captures.Add(name + ".png");
        viewport.QueueFree();
        return image;
    }

    private void SaveCompositeBoard(Image[] sourceGrid, string name, Color background)
    {
        const int columns = 2;
        var cellWidth = sourceGrid[0].GetWidth();
        var cellHeight = sourceGrid[0].GetHeight();
        var board = Image.CreateEmpty(cellWidth * columns, cellHeight * 2, false, Image.Format.Rgba8);
        for (var y = 0; y < board.GetHeight(); y++) for (var x = 0; x < board.GetWidth(); x++)
        {
            var gridIndex = (y / cellHeight) * columns + x / cellWidth;
            var pixel = sourceGrid[gridIndex].GetPixel(x % cellWidth, y % cellHeight);
            board.SetPixel(x, y, background.Lerp(pixel, pixel.A));
        }
        var file = name + ".png";
        Require(board.SavePng(Path.Combine(_outputDirectory, file)) == Error.Ok, $"Could not write {name} board.");
        _captures.Add(file);
        board.Dispose();
    }

    private static void RequireIlluminatedAlphaFalloff(Image atmosphere, Image airless)
    {
        var previous = 1.01f;
        for (var x = 30; x >= 8; x--)
        {
            var atmosphericAlpha = atmosphere.GetPixel(x, 82).A;
            Require(atmosphericAlpha <= previous + .004f,
                "illuminated atmospheric limb alpha was not monotonic from globe to space");
            Require(airless.GetPixel(x, 82).A <= atmosphericAlpha + .004f,
                "airless limb exceeded atmospheric alpha along the illuminated ray");
            previous = atmosphericAlpha;
        }
    }

    private static void RequireImagesEqual(Image expected, Image actual, string message)
    {
        Require(expected.GetSize() == actual.GetSize(), message);
        for (var y = 0; y < expected.GetHeight(); y++)
        for (var x = 0; x < expected.GetWidth(); x++)
            Require(expected.GetPixel(x, y) == actual.GetPixel(x, y), message);
    }

    private static SystemSpatialBodyMarker Marker(int id, string? key, PlanetaryAtmosphereRegime atmosphere, SystemSpatialBodyVisualClass visualClass, bool known) =>
        new(id, null, 0, key ?? "Rocky", PlanetaryBodyKind.Planet, visualClass, 20, 8, 1, 1, known, false, false, false, 1, 1, 1, 280, 100, atmosphere, key);
}
