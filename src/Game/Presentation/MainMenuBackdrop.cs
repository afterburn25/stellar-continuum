using System;
using Godot;

namespace Game.Presentation;

/// <summary>
/// Restrained cinematic background reserved for the main menu; loading contexts use their
/// own project-owned artwork without changing this established menu scene.
/// </summary>
public partial class MainMenuBackdrop : Control
{
    public const string ArtworkPath = "res://assets/visual/loading/stellar-continuum-splash.png";
    public string UiArtworkResourcePath => _artwork?.ResourcePath ?? string.Empty;
    private Texture2D _artwork = null!;
    private GradientTexture2D _readabilityVignette = null!;
    private float _time;

    public override void _Ready()
    {
        MouseFilter = MouseFilterEnum.Ignore;
        _artwork = GD.Load<Texture2D>(ArtworkPath);
        _readabilityVignette = new GradientTexture2D
        {
            Width = 512,
            Height = 2,
            Fill = GradientTexture2D.FillEnum.Linear,
            FillFrom = new Vector2(0, .5f),
            FillTo = new Vector2(1, .5f),
            Gradient = new Gradient
            {
                Offsets = new[] { 0f, .28f, .58f, 1f },
                Colors = new[]
                {
                    new Color(.002f, .008f, .018f, .88f),
                    new Color(.002f, .008f, .018f, .68f),
                    new Color(.002f, .008f, .018f, .27f),
                    new Color(.002f, .008f, .018f, .08f),
                },
            },
        };
        SetProcess(true);
        QueueRedraw();
    }

    public override void _Process(double delta)
    {
        _time += (float)Math.Min(delta, .1);
        QueueRedraw();
    }

    public override void _Draw()
    {
        var size = Size;
        if (size.X <= 1.0f || size.Y <= 1.0f)
            return;

        DrawRect(new Rect2(Vector2.Zero, size), Colors.Black);
        var breathe = 1.025f + MathF.Sin(_time * .11f) * .004f;
        var extent = size * breathe;
        var drift = new Vector2(MathF.Sin(_time * .07f) * 7f, MathF.Cos(_time * .05f) * 4f);
        DrawTextureRect(_artwork, new Rect2((size - extent) * .5f + drift, extent), false,
            new Color(.88f, .93f, 1f, 1f));

        // One cached, continuous texture replaces the old overlapping rect bands. It keeps
        // live menu copy readable at every aspect ratio without introducing vertical seams.
        DrawTextureRect(_readabilityVignette, new Rect2(Vector2.Zero, size), false);
        DrawRect(new Rect2(0, 0, size.X, 2), VisualPalette.WithAlpha(VisualPalette.Selected, .34f));

    }
}
