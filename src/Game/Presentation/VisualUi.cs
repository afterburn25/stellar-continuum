using System;
using Godot;

namespace Game.Presentation;

/// <summary>Shared presentation styling. No simulation state or command rules live here.</summary>
public static class VisualUi
{
    private static FontFile? _headingFont;
    // Shared presentation colors mirror the production visual tokens. Keep semantic accents
    // for data and state; surfaces themselves stay quiet enough for a dense strategy view.
    public static readonly Color Accent = VisualPalette.Selected;
    public static readonly Color Muted = VisualPalette.TextSecondary;
    public static readonly Color Gold = VisualPalette.Economy;
    public static readonly Color PrimaryText = VisualPalette.TextPrimary;
    public static readonly Color Keyline = VisualPalette.Keyline;
    public static readonly Color RaisedSurface = VisualPalette.SurfaceRaised;

    /// <summary>Contain pointer input at an outer UI surface. Godot otherwise forwards wheel
    /// events even through MouseFilter.Stop. Apply at the boundary, not each descendant:
    /// buttons and content must still bubble wheel events to their own ScrollContainer.</summary>
    public static void ContainPointerInput(Control boundary)
    {
        boundary.MouseFilter = Control.MouseFilterEnum.Stop;
        boundary.MouseForcePassScrollEvents = false;
    }

    public static StyleBoxFlat Surface(bool highlighted = false, int margin = 14) => new()
    {
        BgColor = VisualPalette.SurfacePrimary,
        BorderColor = highlighted ? Accent : Keyline,
        BorderWidthLeft = 1, BorderWidthTop = 1, BorderWidthRight = 1, BorderWidthBottom = 1,
        CornerRadiusTopLeft = 6, CornerRadiusTopRight = 6,
        CornerRadiusBottomLeft = 6, CornerRadiusBottomRight = 6,
        ShadowColor = new Color(0,0,0,.24f), ShadowSize = 4, ShadowOffset = new Vector2(0,2),
        ContentMarginLeft = margin, ContentMarginRight = margin,
        ContentMarginTop = margin, ContentMarginBottom = margin,
    };

    /// <summary>A quiet, inset field for dense operational detail over artwork or the map.</summary>
    public static StyleBoxFlat OperationSurface(int margin = 8) => new()
    {
        BgColor = VisualPalette.SurfacePrimary.Lerp(VisualPalette.Canvas, .16f),
        BorderColor = VisualPalette.Keyline.Lerp(VisualPalette.Canvas, .28f),
        BorderWidthLeft = 2, BorderWidthTop = 1, BorderWidthRight = 1, BorderWidthBottom = 1,
        CornerRadiusTopLeft = 4, CornerRadiusTopRight = 4, CornerRadiusBottomLeft = 4, CornerRadiusBottomRight = 4,
        ShadowColor = new Color(0, 0, 0, .18f), ShadowSize = 2, ShadowOffset = new Vector2(0, 1),
        ContentMarginLeft = margin, ContentMarginRight = margin,
        ContentMarginTop = margin, ContentMarginBottom = margin,
    };

    /// <summary>An opaque, compact strip that keeps cost and command copy readable.</summary>
    public static StyleBoxFlat CommandSurface(Color accent, int margin = 4) => new()
    {
        BgColor = VisualPalette.SurfacePrimary.Lerp(VisualPalette.Canvas, .08f),
        BorderColor = accent,
        BorderWidthLeft = 2, BorderWidthTop = 1, BorderWidthRight = 1, BorderWidthBottom = 1,
        CornerRadiusTopLeft = 4, CornerRadiusTopRight = 4, CornerRadiusBottomLeft = 4, CornerRadiusBottomRight = 4,
        ContentMarginLeft = margin, ContentMarginRight = margin,
        ContentMarginTop = margin, ContentMarginBottom = margin,
    };

    public static Label Text(string text, int size = 14, Color? color = null, bool wrap = false)
    {
        var label = new Label
        {
            Text = text, MouseFilter = Control.MouseFilterEnum.Ignore,
            AutowrapMode = wrap ? TextServer.AutowrapMode.WordSmart : TextServer.AutowrapMode.Off,
        };
        label.AddThemeFontSizeOverride("font_size", size);
        if (color.HasValue) label.AddThemeColorOverride("font_color", color.Value);
        return label;
    }

    /// <summary>Reserved for short operational titles; body copy keeps the readable UI face.</summary>
    public static Label Heading(string text, int size = 20, Color? color = null, bool wrap = false)
    {
        var label = Text(text, size, color, wrap);
        label.AddThemeFontOverride("font", _headingFont ??= GD.Load<FontFile>("res://assets/visual/fonts/Rajdhani-SemiBold.ttf"));
        return label;
    }

    public static TextureRect Icon(Texture2D texture, float size = 24) => new()
    {
        Texture = texture, CustomMinimumSize = new Vector2(size, size),
        ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
        StretchMode = TextureRect.StretchModeEnum.KeepAspectCentered,
        MouseFilter = Control.MouseFilterEnum.Ignore,
    };

    public static Button Button(string text, string tooltip, Action action, Texture2D? icon = null)
    {
        var button = new Button
        {
            Text = text, TooltipText = tooltip, Icon = icon,
            ExpandIcon = false, CustomMinimumSize = new Vector2(string.IsNullOrEmpty(text) ? 38 : 0, 38),
            FocusMode = Control.FocusModeEnum.All,
        };
        ApplyInteractiveStates(button);
        button.AddThemeConstantOverride("icon_max_width", 22);
        button.MouseEntered += AudioDirector.PlayHover;
        button.Pressed += () =>
        {
            AudioDirector.PlayConfirm();
            action();
        };
        return button;
    }

    /// <summary>Gives all actionable tiles the same visible hover, press, focus and disabled language.</summary>
    public static void ApplyInteractiveStates(Button button, Color? emphasis = null)
    {
        var accent = emphasis ?? Accent;
        var normal = OperationSurface(margin: 9);
        normal.BgColor = VisualPalette.SurfaceSecondary.Lerp(VisualPalette.Canvas, .12f);
        normal.BorderWidthLeft = normal.BorderWidthTop = normal.BorderWidthRight = normal.BorderWidthBottom = 1;
        var hover = (StyleBoxFlat)normal.Duplicate();
        hover.BgColor = RaisedSurface;
        hover.BorderColor = VisualPalette.Focus;
        hover.BorderWidthLeft = 3;
        hover.BorderWidthTop = hover.BorderWidthRight = hover.BorderWidthBottom = 1;
        var pressed = (StyleBoxFlat)normal.Duplicate();
        pressed.BgColor = VisualPalette.SurfacePrimary;
        pressed.BorderColor = accent;
        pressed.BorderWidthLeft = 3;
        var focus = (StyleBoxFlat)hover.Duplicate();
        focus.BorderColor = VisualPalette.Focus;
        var disabled = (StyleBoxFlat)normal.Duplicate();
        disabled.BgColor = VisualPalette.Canvas.Lerp(VisualPalette.SurfacePrimary, .62f);
        disabled.BorderColor = VisualPalette.Disabled;
        button.AddThemeStyleboxOverride("normal", normal);
        button.AddThemeStyleboxOverride("hover", hover);
        button.AddThemeStyleboxOverride("pressed", pressed);
        button.AddThemeStyleboxOverride("hover_pressed", pressed);
        button.AddThemeStyleboxOverride("focus", focus);
        button.AddThemeStyleboxOverride("disabled", disabled);
        button.AddThemeColorOverride("font_color", PrimaryText);
        button.AddThemeColorOverride("font_hover_color", PrimaryText);
        button.AddThemeColorOverride("font_pressed_color", PrimaryText);
        button.AddThemeColorOverride("font_disabled_color", VisualPalette.TextSecondary);
    }

    public static HFlowContainer Actions(Container parent)
    {
        var row = new HFlowContainer();
        row.AddThemeConstantOverride("h_separation", 6);
        row.AddThemeConstantOverride("v_separation", 6);
        parent.AddChild(row);
        return row;
    }
}
