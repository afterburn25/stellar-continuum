using System;
using System.IO;
using System.Threading.Tasks;
using Game.Presentation;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    // Exercise actual GPU filtering, including thin diffraction rays. Sampling only the
    // source image misses the opaque low mip levels selected by minification.
    private async Task VerifyStarLightEdgesAsync()
    {
        var viewport = new SubViewport
        {
            Size = new Vector2I(768, 192), TransparentBg = true, Disable3D = true,
            RenderTargetUpdateMode = SubViewport.UpdateMode.Always,
            CanvasItemDefaultTextureFilter = Viewport.DefaultCanvasItemTextureFilter.LinearWithMipmaps,
        };
        AddChild(viewport);
        var sizes = new[] { new Vector2(128, 128), new Vector2(24, 24), new Vector2(60, 2), new Vector2(34, 1.24f) };
        foreach (var profile in Enum.GetValues<RadialLightProfile>())
        {
            using var texture = RadialLightTexture.Create(256, profile);
            var rects = new TextureRect[sizes.Length];
            for (var index = 0; index < sizes.Length; index++)
            {
                rects[index] = new TextureRect
                {
                    ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
                    StretchMode = TextureRect.StretchModeEnum.Scale,
                    Texture = texture,
                };
                viewport.AddChild(rects[index]);
                rects[index].Position = new Vector2(32 + index * 192, 32);
                rects[index].Size = sizes[index];
            }
            await WaitFramesAsync(4);
            using var rendered = viewport.GetTexture().GetImage();
            rendered.SavePng(Path.Combine(_outputDirectory, $"star-edge-{profile}.png"));
            for (var index = 0; index < sizes.Length; index++)
            {
                var left = 32 + index * 192;
                var width = (int)sizes[index].X;
                var middleY = 32 + (int)(sizes[index].Y * .5f);
                var edge = Math.Max(rendered.GetPixel(left, middleY).A, rendered.GetPixel(left + width - 1, middleY).A);
                var centerAlpha = rendered.GetPixel(left + width / 2, middleY).A;
                GD.Print($"STAR_FILTER_EDGE profile={profile} size={sizes[index]} edgeAlpha={edge:0.0000} centerAlpha={centerAlpha:0.0000}");
                Require(edge <= 1f / 255f && centerAlpha > .01f,
                    $"{profile} {sizes[index]} exposes a light-texture frame after GPU filtering (alpha {edge}).");
            }
            foreach (var rect in rects) rect.QueueFree();
            await WaitFramesAsync(2);
        }
        viewport.QueueFree();
    }
}
