using System;
using Godot;

namespace Game.Presentation;

/// <summary>Renders decorative galactic dust at the current physical pixel size. The
/// strategic catalogue and all input remain in Main's authoritative map projection.</summary>
internal static class GalaxyCloudRenderer
{
    private const string ShaderPath = "res://assets/visual/shaders/galaxy_dust.gdshader";
    private const string DetailTexturePath = "res://assets/visual/space/galactic-dust-detail-v1.png";

    private static SubViewport? _viewport;
    private static ColorRect? _clouds;
    private static ShaderMaterial? _material;
    private static Texture2D? _detailTexture;
    private static long? _seed;
    private static bool _spiral;
    private static float _prominence;
    private static float _reservedCoreRadius;

    public static Texture2D? Render(CanvasItem owner, Rect2 frame, long seed, bool spiral, float prominence,
        float reservedCoreRadius = 0f)
    {
        if (_viewport is null || !GodotObject.IsInstanceValid(_viewport))
            Initialize(owner);
        var viewport = _viewport!;
        var clouds = _clouds!;
        var material = _material!;
        var scale = owner.GetViewport().GetFinalTransform().Scale;
        var pixels = new Vector2I(Math.Clamp(Mathf.CeilToInt(frame.Size.X * scale.X), 64, 4096),
            Math.Clamp(Mathf.CeilToInt(frame.Size.Y * scale.Y), 64, 4096));
        if (viewport.Size != pixels || _seed != seed || _spiral != spiral ||
            !Mathf.IsEqualApprox(_prominence, prominence) || _reservedCoreRadius != reservedCoreRadius)
        {
            viewport.Size = pixels; clouds.Size = pixels;
            material.SetShaderParameter("aspect", new Vector2(frame.Size.X, frame.Size.Y) / MathF.Min(frame.Size.X, frame.Size.Y));
            material.SetShaderParameter("seed", (float)(seed % 8192));
            material.SetShaderParameter("spiral", spiral);
            material.SetShaderParameter("prominence", prominence);
            material.SetShaderParameter("reserved_core_radius", reservedCoreRadius);
            _reservedCoreRadius = reservedCoreRadius;
            _spiral = spiral;
            _prominence = prominence;
            _seed = seed;
            viewport.RenderTargetUpdateMode = SubViewport.UpdateMode.Once;
        }
        return viewport.IsInsideTree() ? viewport.GetTexture() : null;
    }

    private static void Initialize(CanvasItem owner)
    {
        // Load every fallible dependency before allocating a render target. A missing imported
        // texture must not leave an unattached SubViewport in the static cache at shutdown.
        var shader = GD.Load<Shader>(ShaderPath) ?? throw new InvalidOperationException(
            $"Galaxy dust shader '{ShaderPath}' is unavailable. Verify the asset is included in the project/export.");
        var detailTexture = LoadDetailTexture();

        SubViewport? viewport = null;
        ShaderMaterial? material = null;
        try
        {
            material = new ShaderMaterial { Shader = shader };
            material.SetShaderParameter("detail_texture", detailTexture);
            var clouds = new ColorRect
            {
                Material = material,
                Color = Colors.White,
                MouseFilter = Control.MouseFilterEnum.Ignore,
            };
            viewport = new SubViewport
            {
                Name = "GalaxyDustRenderer",
                TransparentBg = true,
                Disable3D = true,
                GuiDisableInput = true,
                RenderTargetUpdateMode = SubViewport.UpdateMode.Once,
            };
            viewport.AddChild(clouds);
            owner.GetTree().Root.CallDeferred(Node.MethodName.AddChild, viewport);

            _viewport = viewport;
            _material = material;
            _detailTexture = detailTexture;
            _clouds = clouds;
        }
        catch
        {
            if (viewport is not null && GodotObject.IsInstanceValid(viewport))
                viewport.Free();
            material?.Dispose();
            detailTexture.Dispose();
            throw;
        }
    }

    // Import descriptors are local editor by-products and cannot be relied upon in a clean
    // checkout/export. Build the mip chain from the shipped original once, before binding it
    // to the repeat-enabled sampler; subsequent map draws reuse this Texture2D.
    private static Texture2D LoadDetailTexture()
    {
        var source = GD.Load<Texture2D>(DetailTexturePath) ?? throw new InvalidOperationException(
            $"Galaxy dust detail texture '{DetailTexturePath}' is unavailable. " +
            "Import the shipped image before running, and include its imported resource in exports.");
        using var image = source.GetImage();
        if (image.IsCompressed() && image.Decompress() != Error.Ok)
            throw new InvalidOperationException("Galaxy dust detail could not be decompressed for mip generation.");
        if (!image.HasMipmaps() && image.GenerateMipmaps() != Error.Ok)
            throw new InvalidOperationException("Galaxy dust detail could not generate its required mip chain.");
        return ImageTexture.CreateFromImage(image);
    }
}
