using System;
using Godot;

namespace Game.Presentation;

/// <summary>Short visual acknowledgement driven by observer-visible player notifications.</summary>
public partial class ActionFeedbackEffects : Control
{
    private string _category = string.Empty;
    private float _remaining;
    public int TriggerCount { get; private set; }
    public string LastCategory => _category;
    public string ActiveCategory => _remaining > 0 ? _category : string.Empty;

    public override void _Ready()
    {
        MouseFilter = MouseFilterEnum.Ignore;
        SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
        SetProcess(false);
    }

    public void Trigger(string category)
    {
        _category = category;
        _remaining = 1.35f;
        TriggerCount++;
        SetProcess(true);
        QueueRedraw();
    }

    public override void _Process(double delta)
    {
        _remaining = Math.Max(0, _remaining - (float)delta);
        QueueRedraw();
        if (_remaining <= 0) SetProcess(false);
    }

    public override void _Draw()
    {
        if (_remaining <= 0) return;
        var progress = 1 - _remaining / 1.35f;
        var alpha = MathF.Sin(progress * MathF.PI) * .65f;
        var color = CategoryColor(_category);
        // Notification feedback belongs near the HUD, not as giant unexplained
        // orbital rings covering stars, planets and the player's current target.
        var end = new Vector2(Size.X - 24, 72);
        var start = end - new Vector2(160, 0);
        DrawLine(start, end, new Color(color, alpha * .2f), 4, true);
        DrawLine(start, start.Lerp(end, progress), new Color(color, alpha), 1.5f, true);
        var marker = start.Lerp(end, progress);
        if (_category.Equals("exploration", StringComparison.OrdinalIgnoreCase))
        {
            // A directional survey trace makes an actual exploration notification readable at
            // a glance without suggesting a route or destination the player cannot know.
            DrawArc(marker, 6.0f, -1.05f, 1.05f, 18, new Color(color, alpha), 1.25f, true);
            DrawLine(marker + new Vector2(2.5f, -5), marker + new Vector2(6, 0), new Color(color, alpha), 1.25f, true);
            DrawLine(marker + new Vector2(6, 0), marker + new Vector2(2.5f, 5), new Color(color, alpha), 1.25f, true);
        }
        else if (_category.Equals("ships", StringComparison.OrdinalIgnoreCase))
        {
            DrawLine(marker + new Vector2(-5, -3), marker + new Vector2(5, 0), new Color(color, alpha), 1.4f, true);
            DrawLine(marker + new Vector2(5, 0), marker + new Vector2(-5, 3), new Color(color, alpha), 1.4f, true);
        }
        else CinematicArt.DrawStarlight(this, marker, 1.4f, color, alpha);
    }

    private static Color CategoryColor(string category) => category.ToLowerInvariant() switch
    {
        "research" => new Color("a78bfa"),
        "industry" => new Color("f1b85b"),
        "ships" => new Color("62d5ff"),
        "exploration" => new Color("6ee7d1"),
        "colony" => new Color("83e39e"),
        "combat" => new Color("ff766d"),
        _ => new Color("8bdcf5"),
    };
}
