using System;
using System.Linq;
using Godot;
using Game.Simulation.Models;

namespace Game.Presentation.Spatial;

/// <summary>A single organized, observer-filtered home for orbital world information.</summary>
public partial class PlanetInspectorPanel : PanelContainer
{
    private VBoxContainer _body = null!;
    private SystemSpatialSnapshot? _snapshot;
    private SystemSpatialBodyMarker? _selected;
    private bool _canLand, _showWorlds = true;
    public event Action<int>? SelectRequested;
    public event Action? FocusRequested;
    public event Action<int>? SurfaceRequested;
    public int? DisplayedBodyId => _showWorlds ? null : _selected?.BodyId;

    public PlanetInspectorPanel()
    {
        Name = "PlanetInspector";
        VisualUi.ContainPointerInput(this);
        AddThemeStyleboxOverride("panel", CinematicArt.Frame(margin: 12));
        var scroll = new ScrollContainer { HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled,
            VerticalScrollMode = ScrollContainer.ScrollMode.Auto, FollowFocus = true };
        AddChild(scroll);
        _body = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill };
        _body.AddThemeConstantOverride("separation", 4); scroll.AddChild(_body);
    }

    public void Present(SystemSpatialSnapshot snapshot, int? selectedId, bool canLand)
    {
        var selected = selectedId.HasValue ? snapshot.Bodies.FirstOrDefault(b => b.BodyId == selectedId) : null;
        if (_snapshot?.SystemId == snapshot.SystemId && _snapshot.SurveyLevel == snapshot.SurveyLevel &&
            _snapshot.Bodies.Count == snapshot.Bodies.Count && _selected == selected && _canLand == canLand) return;
        if (_selected?.BodyId != selected?.BodyId || _snapshot?.SystemId != snapshot.SystemId)
            _showWorlds = selected is null;
        _snapshot = snapshot; _selected = selected; _canLand = canLand;
        Rebuild();
    }

    private void Rebuild()
    {
        foreach (var node in _body.GetChildren()) { _body.RemoveChild(node); node.QueueFree(); }
        if (_snapshot is null) return;
        var heading = new HBoxContainer();
        var title = VisualUi.Text(_showWorlds ? _snapshot.CatalogName.ToUpperInvariant() : _selected!.Label.ToUpperInvariant(), 18);
        title.SizeFlagsHorizontal = SizeFlags.ExpandFill; title.TextOverrunBehavior = TextServer.OverrunBehavior.TrimEllipsis;
        heading.AddChild(title); _body.AddChild(heading);
        var tabs = new HBoxContainer(); _body.AddChild(tabs);
        var worlds = VisualUi.Button("Worlds", "Browse this system's known planets and moons.", () => { _showWorlds = true; Rebuild(); });
        worlds.Name = "InspectorWorlds"; tabs.AddChild(worlds);
        var details = VisualUi.Button("Details", "Inspect the selected world's surveyed information.", () => { _showWorlds = false; Rebuild(); });
        details.Name = "InspectorDetails"; details.Disabled = _selected is null; tabs.AddChild(details);
        if (_showWorlds) { BuildWorldList(); return; }
        var body = _selected!;
        var badge = VisualUi.Text(body.HasDetailedEnvironment ? "SURVEY COMPLETE" : "DETAILED SURVEY NEEDED", 10,
            body.HasDetailedEnvironment ? VisualUi.Accent : VisualUi.Gold); _body.AddChild(badge);
        Section("PHYSICAL");
        Row("Type", body.Kind switch { PlanetaryBodyKind.Moon => "Moon", PlanetaryBodyKind.DwarfPlanet => "Dwarf planet", _ => "Planet" });
        Row("Radius", MetricFormat.Radius(body.RadiusEarth, body.HasDetailedEnvironment));
        if (body.OrbitalEccentricity > 0)
        {
            Row("Eccentricity", body.OrbitalEccentricity.ToString("0.0000"));
            Row("Inclination", body.OrbitalInclinationDegrees.ToString("0.00") + "°");
        }
        Row("Mass", MetricFormat.Mass(body.MassEarth, body.HasDetailedEnvironment));
        Row("Gravity", MetricFormat.Gravity(body.GravityG, body.HasDetailedEnvironment));
        Section("ENVIRONMENT");
        Row("Temperature", MetricFormat.Temperature(body.TemperatureKelvin, body.HasDetailedEnvironment));
        Row("Pressure", MetricFormat.Pressure(body.PressureKPa, body.HasDetailedEnvironment));
        Row("Atmosphere", body.HasDetailedEnvironment ? Atmosphere(body.Atmosphere) : "Unconfirmed");
        Section("SATELLITES & SIGNALS");
        if (body.ParentBodyId is int parentId)
            Row("Orbits", _snapshot.Bodies.FirstOrDefault(b => b.BodyId == parentId)?.Label ?? "Unknown");
        else Row("Known moons", _snapshot.Bodies.Count(b => b.ParentBodyId == body.BodyId).ToString());
        Row("Resources", body.PositiveResourceSignature ? "Signal detected" : "Unconfirmed");
        Row("Activity", body.PositiveActivitySignature ? "Signal detected" : "Unconfirmed");
        Row("Anomaly", body.PositiveAnomalySignature ? "Signal detected" : "Unconfirmed");
        Section("ACTIONS");
        var focus = VisualUi.Button("Focus planet", "Move the camera toward this world.", () => FocusRequested?.Invoke(), VisualIconLibrary.NavZoomIn);
        focus.Name = "InspectorFocus"; _body.AddChild(focus);
        var surface = VisualUi.Button("Colony surface", _canLand ? "Visit your colony on this world." : "A surface view requires an owned colony on a solid world.",
            () => SurfaceRequested?.Invoke(body.BodyId), VisualIconLibrary.Colony);
        surface.Name = "InspectorSurface"; surface.Disabled = !_canLand; _body.AddChild(surface);
    }

    private void BuildWorldList()
    {
        _body.AddChild(VisualUi.Text("PLANETS & MOONS", 10, VisualUi.Accent));
        foreach (var planet in _snapshot!.Bodies.Where(b => b.Kind != PlanetaryBodyKind.Moon))
        {
            WorldButton(planet, false);
            foreach (var moon in _snapshot.Bodies.Where(b => b.ParentBodyId == planet.BodyId)) WorldButton(moon, true);
        }
        if (_snapshot.Bodies.Count == 0) _body.AddChild(VisualUi.Text("No planetary bodies in the current survey.", 12, VisualUi.Muted, true));
    }

    private void WorldButton(SystemSpatialBodyMarker body, bool moon)
    {
        var button = VisualUi.Button((moon ? "    " : "") + body.Label,
            "Select " + body.Label + " and show its surveyed statistics.", () =>
            { _showWorlds = false; SelectRequested?.Invoke(body.BodyId); }, moon ? VisualIconLibrary.NavSystem : VisualIconLibrary.Colony);
        button.Name = "InspectorWorld" + body.BodyId;
        button.Alignment = HorizontalAlignment.Left; button.CustomMinimumSize = new(0, 32);
        button.AddThemeFontSizeOverride("font_size", 12); _body.AddChild(button);
    }

    private void Section(string name)
    {
        _body.AddChild(new HSeparator()); _body.AddChild(VisualUi.Text(name, 10, VisualUi.Accent));
    }

    private void Row(string label, string value)
    {
        var row = new HBoxContainer(); row.AddThemeConstantOverride("separation", 8);
        row.AddChild(VisualUi.Text(label, 11, VisualUi.Muted));
        var amount = VisualUi.Text(value, 12, wrap: true);
        amount.Name = "Stat" + label.Replace(" ", "");
        amount.SizeFlagsHorizontal = SizeFlags.ExpandFill; amount.HorizontalAlignment = HorizontalAlignment.Right;
        row.AddChild(amount); _body.AddChild(row);
    }

    private static string Atmosphere(PlanetaryAtmosphereRegime? atmosphere) => atmosphere switch
    {
        PlanetaryAtmosphereRegime.OxygenNitrogen => "Oxygen / nitrogen",
        PlanetaryAtmosphereRegime.OxygenRich => "Oxygen rich",
        PlanetaryAtmosphereRegime.CarbonDioxideRich => "Carbon dioxide",
        PlanetaryAtmosphereRegime.Reducing => "Reducing",
        PlanetaryAtmosphereRegime.Inert => "Inert",
        PlanetaryAtmosphereRegime.Vacuum => "Vacuum",
        PlanetaryAtmosphereRegime.Other => "Other", _ => "Unconfirmed"
    };
}
