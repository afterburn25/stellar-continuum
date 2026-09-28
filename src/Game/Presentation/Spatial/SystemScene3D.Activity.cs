using System;
using Godot;
using Game.Presentation;

namespace Game.Presentation.Spatial;

public sealed record ShipyardBuildActivity(string? DesignId, double Progress, bool Active, bool TimeRunning);

/// <summary>Presentation-only shipyard construction activity. The caller supplies authoritative order state.</summary>
public partial class SystemScene3D
{
    public void PresentShipyardActivity(ShipyardBuildActivity activity, CivilizationVisualStyle? style = null)
    {
        if (!_infrastructure.TryGetValue("orbital_shipyard", out var shipyard)) return;
        PresentShipyardActivity(shipyard, activity.DesignId, activity.Progress, activity.Active, activity.TimeRunning, style);
    }
    /// <summary>Attach or update a single capped construction vignette beneath the represented shipyard.</summary>
    public static void PresentShipyardActivity(Node3D shipyard, string? designId, double progress, bool active, bool timeRunning = true, CivilizationVisualStyle? style = null)
    {
        const string activityName = "ShipyardConstructionActivity";
        var existing = shipyard.GetNodeOrNull<ShipyardConstructionActivity>(activityName);
        if (!active || string.IsNullOrWhiteSpace(designId))
        {
            if (existing is not null) { shipyard.RemoveChild(existing); existing.QueueFree(); }
            return;
        }
        if (existing is null)
        {
            existing = new ShipyardConstructionActivity { Name = activityName };
            shipyard.AddChild(existing);
        }
        existing.Present(designId, progress, timeRunning, style);
    }

    private sealed partial class ShipyardConstructionActivity : Node3D
    {
        private Node3D? _hull;
        private MeshInstance3D? _weldLight;
        private float _time;
        private string _design = "";
        private string _species = "";
        private bool _timeRunning;
        private readonly StandardMaterial3D _frame = new() { AlbedoColor = new("3c5664"), Metallic = .68f, Roughness = .36f };
        private readonly StandardMaterial3D _weld = new() { AlbedoColor = new("fff0b0"), EmissionEnabled = true, Emission = new("ffbc62"), EmissionEnergyMultiplier = 2.2f, Roughness = .22f };

        public void Present(string design, double progress, bool timeRunning, CivilizationVisualStyle? style)
        {
            var fraction = Mathf.Clamp((float)progress, .04f, 1f);
            if (_design != design || _species != (style?.SpeciesId ?? ""))
            {
                _design = design; _species = style?.SpeciesId ?? "";
                if (_hull is not null) { RemoveChild(_hull); _hull.QueueFree(); }
                if (_weldLight is not null) { RemoveChild(_weldLight); _weldLight.QueueFree(); }
                _hull = ShipGeometry.Create(design, style, highDetail: false);
                _hull.Position = new(0, .72f, 0);
                AddChild(_hull);
                _weldLight = AddBox(new(0, 1.2f, -2.35f), new(.24f, .24f, .24f), _weld);
            }
            _timeRunning = timeRunning;
            if (_hull is not null)
            {
                _hull.Scale = new(1f, Mathf.Lerp(.18f, 1f, fraction), fraction);
                _hull.Position = new(0, .72f, Mathf.Lerp(1.6f, 0, fraction));
            }
            // Four fixed scaffold frames and at most four service craft keep activity bounded.
            for (var i = 0; i < 4; i++)
            {
                var arm = GetNodeOrNull<Node3D>($"Frame{i}") ?? Frame(i);
                arm.Visible = fraction > i * .16f;
            }
        }

        public override void _Process(double delta)
        {
            if (!_timeRunning) return;
            _time += (float)delta;
            if (_weldLight is not null)
            {
                _weldLight.Visible = _hull is not null && Mathf.Sin(_time * 8f) > -.25f;
                _weldLight.Position = new(Mathf.Sin(_time * 1.7f) * 1.6f, 1.1f + Mathf.Cos(_time * 2.3f) * .35f, -1.65f);
            }
            for (var i = 0; i < 4; i++)
            {
                var craft = GetNodeOrNull<Node3D>($"Craft{i}");
                if (craft is not null) craft.Position = new(Mathf.Cos(_time * (.55f + i * .08f) + i) * (2.8f + i * .22f), .65f + i * .24f, Mathf.Sin(_time * (.55f + i * .08f) + i) * 2.15f);
            }
        }

        private Node3D Frame(int index)
        {
            var side = index < 2 ? -1 : 1;
            var z = index % 2 == 0 ? -1.6f : 1.6f;
            var root = new Node3D { Name = $"Frame{index}", Position = new(side * 3.3f, .55f, z) }; AddChild(root);
            AddBox(root, new(0, 1.25f, 0), new(.14f, 2.5f, .14f), _frame);
            AddBox(root, new(-side * .85f, 2.2f, 0), new(1.8f, .12f, .14f), _frame);
            var craft = new Node3D { Name = $"Craft{index}" }; AddChild(craft);
            AddBox(craft, Vector3.Zero, new(.42f, .16f, .64f), _frame); AddBox(craft, new(0, 0, .34f), new(.20f, .08f, .10f), _weld);
            return root;
        }

        private MeshInstance3D AddBox(Vector3 at, Vector3 size, Material mat) => AddBox(this, at, size, mat);
        private static MeshInstance3D AddBox(Node3D parent, Vector3 at, Vector3 size, Material mat) { var mesh = new MeshInstance3D { Mesh = new BoxMesh { Size = size }, MaterialOverride = mat, Position = at }; parent.AddChild(mesh); return mesh; }
    }
}
