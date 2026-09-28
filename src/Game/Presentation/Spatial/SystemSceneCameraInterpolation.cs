using System;
using Godot;

namespace Game.Presentation.Spatial;

/// <summary>
/// Float-facing camera interpolation that reaches the exact target even when a high-refresh
/// frame's remaining motion is smaller than one representable float step.
/// </summary>
public static class SystemSceneCameraInterpolation
{
    public static Vector3 Advance(Vector3 current, Vector3 target, double delta, double responsiveness) => new(
        Advance(current.X, target.X, delta, responsiveness),
        Advance(current.Y, target.Y, delta, responsiveness),
        Advance(current.Z, target.Z, delta, responsiveness));

    public static float Advance(float current, float target, double delta, double responsiveness)
    {
        var weight = Weight(delta, responsiveness);
        if (weight <= 0) return current;
        var next = (float)(current + (target - current) * weight);
        return next == current ? target : next;
    }

    public static float AdvanceAngle(float current, float target, double delta, double responsiveness)
    {
        var weight = (float)Weight(delta, responsiveness);
        if (weight <= 0) return current;
        var next = Mathf.LerpAngle(current, target, weight);
        return next == current ? target : next;
    }

    private static double Weight(double delta, double responsiveness) =>
        1.0 - Math.Exp(-responsiveness * Math.Clamp(delta, 0, .12));
}
