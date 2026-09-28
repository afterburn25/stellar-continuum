using System;

namespace Game.Presentation.Spatial;

/// <summary>Presentation-only affine camera. Drawing and picking read the same interpolated
/// scale/origin. Interpolating both with one weight preserves a zoom gesture's pointer anchor.</summary>
public sealed class SmoothSpatialCamera
{
    private double _scale = 1, _originX, _originY;
    private double _targetOriginX, _targetOriginY;
    public float Scale => (float)_scale;
    public float OriginX => (float)_originX;
    public float OriginY => (float)_originY;
    public float TargetScale { get; private set; } = 1f;
    public float TargetOriginX => (float)_targetOriginX;
    public float TargetOriginY => (float)_targetOriginY;
    public bool IsMoving => Math.Abs(_scale - TargetScale) > Math.Max(1e-10, TargetScale * .000002) ||
        Math.Abs(_originX - _targetOriginX) > .05 ||
        Math.Abs(_originY - _targetOriginY) > .05;

    // Do the large world/origin cancellation in double precision. Casting the origin first
    // made distant systems jump several pixels at the closest zoom in a physical galaxy.
    public float ProjectX(double worldX) => (float)(_originX + worldX * _scale);
    public float ProjectY(double worldY) => (float)(_originY + worldY * _scale);

    public void Snap(float scale, double originX, double originY)
    {
        SetTarget(scale, originX, originY);
        _scale = TargetScale;
        _originX = _targetOriginX;
        _originY = _targetOriginY;
    }

    public void SetTarget(float scale, double originX, double originY)
    {
        if (!float.IsFinite(scale) || scale <= 0 || !double.IsFinite(originX) || !double.IsFinite(originY))
            throw new ArgumentOutOfRangeException(nameof(scale), "Camera transform must be finite with positive scale.");
        TargetScale = scale;
        _targetOriginX = originX;
        _targetOriginY = originY;
    }

    public void ZoomAt(float factor, float anchorX, float anchorY, float minimum, float maximum)
    {
        if (!float.IsFinite(factor) || factor <= 0 || minimum <= 0 || maximum < minimum)
            throw new ArgumentOutOfRangeException(nameof(factor));
        var worldX = (anchorX - _originX) / _scale;
        var worldY = (anchorY - _originY) / _scale;
        var next = Math.Clamp(TargetScale * factor, minimum, maximum);
        SetTarget(next, anchorX - worldX * next, anchorY - worldY * next);
    }

    public void Pan(float x, float y) => Snap(Scale, _originX + x, _originY + y);

    /// <summary>Resize translation preserves an in-progress gesture and its target.</summary>
    public void Translate(float x, float y)
    {
        SetTarget(TargetScale, _targetOriginX + x, _targetOriginY + y);
        _originX += x;
        _originY += y;
    }

    public bool Advance(double delta)
    {
        if (!IsMoving)
        {
            var changed = _scale != TargetScale || _originX != _targetOriginX || _originY != _targetOriginY;
            if (changed) Snap(TargetScale, _targetOriginX, _targetOriginY);
            return changed;
        }
        var weight = 1.0 - Math.Exp(-12.0 * Math.Clamp(delta, 0, 0.1));
        _scale += (TargetScale - _scale) * weight;
        _originX += (_targetOriginX - _originX) * weight;
        _originY += (_targetOriginY - _originY) * weight;
        if (!IsMoving)
            Snap(TargetScale, _targetOriginX, _targetOriginY);
        return true;
    }
}
