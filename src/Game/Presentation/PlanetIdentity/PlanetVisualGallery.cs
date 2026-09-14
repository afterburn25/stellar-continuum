using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Game.Presentation.Spatial;
using Game.Simulation.Models;
using Game.Simulation.Knowledge;
using Godot;

namespace Game.Presentation.PlanetIdentity;

/// <summary>Developer gallery and GPU capture driver built from the live game renderers.</summary>
public partial class PlanetVisualGallery : Control
{
    private SystemScene3D _orbit = null!;
    private PlanetSurfaceView _surface = null!;
    private Control _stage = null!;
    private Label _title = null!, _subtitle = null!, _facts = null!, _footer = null!;
    private OptionButton _classes = null!, _variants = null!, _skies = null!;
    private Button _mode = null!;
    private TextureRect _thumbnail = null!;
    private PlanetClass _class = PlanetClass.Terran;
    private bool _ground;
    private bool _capturing;
    private readonly List<object> _evidence = new();
    public event Action? Closed;
    public override async void _Ready()
    {
        try
        {
            SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
            var backdrop = new ColorRect { Color = new Color("071119") }; AddChild(backdrop); backdrop.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
            var margin = new MarginContainer(); AddChild(margin); margin.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
            foreach (var side in new[] { "left", "right", "top", "bottom" }) margin.AddThemeConstantOverride("margin_" + side, 18);
            var layout = new VBoxContainer(); layout.AddThemeConstantOverride("separation", 12); margin.AddChild(layout);
            var top = new HBoxContainer(); layout.AddChild(top);
            top.AddChild(VisualUi.Text("STELLAR CONTINUUM  /  PLANET ATLAS", 22, VisualUi.Accent));
            var spacer = new Control { SizeFlagsHorizontal = SizeFlags.ExpandFill }; top.AddChild(spacer);
            top.AddChild(VisualUi.Button("Close", "Return to developer tools.", () => { if (Closed is not null) Closed(); else GetTree().Quit(); }));
            var toolbar = new HBoxContainer(); toolbar.AddThemeConstantOverride("separation", 12); layout.AddChild(toolbar);
            _classes = new OptionButton { CustomMinimumSize = new(260, 36) };
            foreach (var c in Enum.GetValues<PlanetClass>()) _classes.AddItem(PlanetClassifier.Name(c));
            toolbar.AddChild(_classes); _classes.ItemSelected += index => { _class = (PlanetClass)index; FillVariants(); Present(); };
            _variants = new OptionButton { CustomMinimumSize = new(225, 36) }; toolbar.AddChild(_variants); _variants.ItemSelected += _ => Present();
            _skies = new OptionButton { CustomMinimumSize = new(200, 36) };
            foreach (var name in new[] { "Yellow dwarf", "Red dwarf", "Blue-white star", "Binary stars", "Nebula", "Sparse outer region", "Inner star band", "Triple stars" }) _skies.AddItem(name);
            toolbar.AddChild(_skies); _skies.ItemSelected += _ => Present();
            _mode = VisualUi.Button("Surface view", "Switch between the actual orbital and surface renderers.", () => { _ground = !_ground; Present(); }); toolbar.AddChild(_mode);
            var main = new HBoxContainer { SizeFlagsVertical = SizeFlags.ExpandFill }; main.AddThemeConstantOverride("separation", 16); layout.AddChild(main);
            _stage = new Control { SizeFlagsHorizontal = SizeFlags.ExpandFill, SizeFlagsVertical = SizeFlags.ExpandFill, ClipContents = true }; main.AddChild(_stage);
            _orbit = new SystemScene3D(); _stage.AddChild(_orbit); _orbit.SetAnchorsAndOffsetsPreset(LayoutPreset.FullRect);
            _surface = new PlanetSurfaceView(); _stage.AddChild(_surface);
            var inspector = new PanelContainer { CustomMinimumSize = new(292, 0) }; inspector.AddThemeStyleboxOverride("panel", VisualUi.Surface(true, 16)); main.AddChild(inspector);
            var scroll = new ScrollContainer { HorizontalScrollMode = ScrollContainer.ScrollMode.Disabled }; inspector.AddChild(scroll);
            var details = new VBoxContainer { SizeFlagsHorizontal = SizeFlags.ExpandFill, CustomMinimumSize = new(260, 0) }; details.AddThemeConstantOverride("separation", 12); scroll.AddChild(details);
            details.AddChild(VisualUi.Text("SYNTHETIC SURVEY SPECIMEN", 11, VisualUi.Gold));
            _title = VisualUi.Text("", 23, VisualUi.Accent, true); details.AddChild(_title);
            _subtitle = VisualUi.Text("", 15, VisualUi.PrimaryText, true); details.AddChild(_subtitle);
            _thumbnail = new TextureRect
            {
                Texture = CelestialBodyMaterials.WhiteTexture,
                CustomMinimumSize = new(92, 92),
                ExpandMode = TextureRect.ExpandModeEnum.IgnoreSize,
                SizeFlagsHorizontal = SizeFlags.ShrinkCenter
            };
            details.AddChild(_thumbnail);
            details.AddChild(new HSeparator());
            _facts = VisualUi.Text("", 13, VisualUi.Muted, true); details.AddChild(_facts);
            _footer = VisualUi.Text("Development gallery · shared game renderers · no campaign is opened or changed", 12, VisualUi.Muted, true); layout.AddChild(_footer);
            FillVariants(); await Frames(3); Present();
            if (System.Environment.GetEnvironmentVariable("STELLAR_PLANET_CAPTURE_DIR") is { } output)
            {
                _capturing = true; await Capture(output);
                _surface.Close(); _surface.QueueFree(); _surface = null!;
                _orbit.Clear(); _orbit.QueueFree(); _orbit = null!;
                _thumbnail.Material = null; CelestialBodyMaterials.ReleasePlanetMaterial(100);
                await Frames(8); GC.Collect(); GC.WaitForPendingFinalizers(); await Frames(8);
                GD.Print("STELLAR_PLANET_GALLERY_COMPLETE"); GetTree().Quit();
            }
        }
        catch (Exception e) { GD.PushError("Planet gallery failed: " + e); GetTree().Quit(1); }
    }
    public override void _Process(double delta) { if (_orbit is not null && _orbit.Visible) _orbit.Advance(_capturing ? .15 : delta); }
    private void FillVariants() { _variants.Clear(); foreach (var v in PlanetVisualCatalog.Get(_class).Variants) _variants.AddItem(v.Name); }
    private void Present(bool unknown = false, bool partial = false, bool inhabited = false)
    {
        var selection = _skies.Selected;
        var system = PlanetIdentityExamples.System(star: selection == 1 ? StellarPrimaryClass.MRedDwarf : selection == 2 ? StellarPrimaryClass.HotBlueStar : StellarPrimaryClass.GYellowDwarf,
            secondary: selection is 3 or 7 ? StellarPrimaryClass.KOrangeDwarf : null,
            archetype: selection == 4 ? StarArchetype.Nebula : StarArchetype.Standard);
        if (selection == 7) system = system with { TertiaryStellarClass = StellarPrimaryClass.AWhiteStar };
        var body = PlanetIdentityExamples.Body(_class);
        var identity = PlanetPresentationResolver.Resolve(body, system, 928317, knownPopulationMillions: inhabited ? 8000 : 0, radialFraction: selection == 5 ? .92 : selection == 6 ? .1 : .5)!;
        identity = identity with { Variant = PlanetVisualCatalog.Get(_class).Variants[_variants.Selected] };
        if (identity.Variant.Id.EndsWith("05") && !identity.CanShowSolidSurface) identity = identity with { Modifiers = identity.Modifiers | PlanetVisualModifier.Ringed };
        var marker = Marker(body, identity);
        if (unknown || partial) marker = marker with
        {
            HasDetailedEnvironment = false,
            Presentation = null,
            VisualClass = SystemSpatialBodyVisualClass.UnknownPlanet,
            SurfaceKey = null,
            MassEarth = null,
            GravityG = null,
            TemperatureKelvin = null,
            PressureKPa = null,
            Atmosphere = null,
            AvailableSolvent = null,
            RadiationHazard = null,
            HasSolidSurface = null
        };
        var sky = unknown || partial ? SystemSkyResolver.Resolve(928317, 42, null) : identity.Sky;
        _thumbnail.Material = CelestialBodyMaterials.GetPlanetMaterial(marker);
        var snapshot = new SystemSpatialSnapshot(42, "Atlas specimen", unknown ? SystemSurveyLevel.Unknown : partial ? SystemSurveyLevel.PartiallySurveyed : SystemSurveyLevel.FullySurveyed,
            unknown ? 0 : partial ? .4 : 1, unknown || partial ? null : system.Archetype, 7000, new[] { marker },
            StellarClass: unknown || partial ? null : system.StellarClass, SecondaryStellarClass: unknown || partial ? null : system.SecondaryStellarClass,
            TertiaryStellarClass: unknown || partial ? null : system.TertiaryStellarClass, Sky: sky, CampaignSeed: 928317);
        var ground = _ground && identity.CanShowSolidSurface && !unknown && !partial;
        _mode.Text = ground ? "Orbital view" : identity.CanShowSolidSurface ? "Surface view" : "No solid surface";
        _mode.Disabled = !identity.CanShowSolidSurface;
        _surface.Close(); _orbit.Visible = !ground;
        if (ground)
        {
            var companions = new List<SystemSpatialBodyMarker>();
            if (body.Kind == PlanetaryBodyKind.Moon)
            {
                var parent = PlanetIdentityExamples.Body(PlanetClass.GasGiant, 99);
                var p = PlanetPresentationResolver.Resolve(parent, system, 928317)!;
                p = p with { Modifiers = p.Modifiers | PlanetVisualModifier.Ringed };
                companions.Add(Marker(parent, p));
            }
            _surface.OpenVisualPreview(identity, companions);
        }
        else { _orbit.Present(snapshot); _orbit.FocusBody(marker.BodyId); if (inhabited) _orbit.Rotate(new Vector2(175, 0)); _orbit.Advance(4); }
        _title.Text = unknown ? "Unsurveyed world" : partial ? "Partially surveyed" : identity.Classification.Name;
        _subtitle.Text = unknown || partial ? "Classification pending" : identity.Variant.Name + "\n" + (ground ? "Surface environment" : "Orbital survey");
        var e = body.Environment;
        _facts.Text = unknown || partial ? "A full environmental survey is required.\n\nTrue class, chemistry, materials and development remain concealed." :
            $"{e.TemperatureKelvin:0.#} K  ·  {e.GravityG:0.00} g\n{e.PressureKPa:N1} kPa\n{e.Atmosphere}\nSolvent: {e.AvailableSolvent}\nRadius: {body.RadiusEarth:0.00} Earth\nMass: {body.MassEarth:0.000} Earth\nRadiation: {e.RadiationHazard:0.00}\n\n{identity.Classification.Reason}\n\nVISUAL IDENTITY\n{identity.Variant.Id}\n{identity.Identity:x16}\nSeed {identity.ShaderSeed:0.000}\n{identity.OrbitalFamily} / {identity.SurfaceFamily}\n{identity.Modifiers}\n\nSYSTEM SKY\n{sky.Primary.Class}\n{sky.Background}\n{sky.GalacticContext}\n{sky.StarDensity} bounded star instances\n\n{(identity.CanShowSolidSurface ? "Local construction heightfield remains authoritative." : "Gas envelope: ground access unavailable.")}";
    }
    public static SystemSpatialBodyMarker Marker(PlanetaryBodyState body, PlanetPresentation identity) => new(body.Id, body.ParentBodyId, body.OrbitIndex, body.Name, body.Kind,
        !identity.CanShowSolidSurface ? SystemSpatialBodyVisualClass.GasGiant : SystemSpatialBodyVisualClass.Rocky,
        4800, 1600, 5100, 100, true, false, false, false, body.RadiusEarth, body.MassEarth, body.Environment.GravityG,
        body.Environment.TemperatureKelvin, body.Environment.PressureKPa, body.Environment.Atmosphere, identity.CanonicalKey,
        Presentation: identity, AvailableSolvent: body.Environment.AvailableSolvent, RadiationHazard: body.Environment.RadiationHazard, HasSolidSurface: body.Environment.HasSolidSurface);
    private async Task Frames(int count) { for (int i = 0; i < count; i++) await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame); }
    private async Task Capture(string output)
    {
        Directory.CreateDirectory(output);
        GetWindow().Mode = Window.ModeEnum.Windowed; GetWindow().Borderless = false; GetWindow().Position = new(5000, 5000);
        GetWindow().ContentScaleMode = Window.ContentScaleModeEnum.Disabled;
        foreach (var size in new[] { new Vector2I(1920, 1080), new Vector2I(1280, 720) })
        {
            GetWindow().Size = size; await Frames(8);
            foreach (var c in Enum.GetValues<PlanetClass>())
            {
                _class = c; _classes.Select((int)c); FillVariants(); _variants.Select(c == PlanetClass.Desert ? 1 : 0); _skies.Select(0);
                _ground = false; Present(); await Save(output, PlanetVisualCatalog.ToId(c) + "-orbit", size);
                if (c is not (PlanetClass.GasGiant or PlanetClass.IceGiant)) { _ground = true; Present(); await Save(output, PlanetVisualCatalog.ToId(c) + "-surface", size); }
            }
            for (var sky = 1; sky < 8; sky++)
            {
                _class = sky == 5 ? PlanetClass.AirlessRocky : PlanetClass.Terran; _classes.Select((int)_class); FillVariants(); _skies.Select(sky); _ground = true; Present(); await Save(output, "sky-" + sky + "-surface", size);
                _ground = false; Present(); await Save(output, "sky-" + sky + "-orbit", size);
            }
            _class = PlanetClass.Terran; _classes.Select(0); FillVariants(); _skies.Select(0); _ground = false;
            Present(inhabited: true); await Save(output, "inhabited-night-lights", size);
            Present(unknown: true); await Save(output, "unknown-world", size);
            Present(partial: true); await Save(output, "partial-survey", size);
        }
        File.WriteAllText(Path.Combine(output, "capture-report.json"), JsonSerializer.Serialize(_evidence, new JsonSerializerOptions { WriteIndented = true }));
    }
    private async Task Save(string output, string name, Vector2I size)
    {
        var timer = Stopwatch.StartNew(); await Frames(10); await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
        using var image = GetViewport().GetTexture().GetImage();
        if (image.GetWidth() != size.X || image.GetHeight() != size.Y) throw new InvalidOperationException("Capture is not at the requested native resolution.");
        var file = $"{name}-{size.X}x{size.Y}.png"; var error = image.SavePng(Path.Combine(output, file));
        if (error != Error.Ok) throw new IOException("Capture failed: " + error);
        _evidence.Add(new
        {
            file,
            width = size.X,
            height = size.Y,
            settleMilliseconds = timer.ElapsedMilliseconds,
            processBytes = Process.GetCurrentProcess().PrivateMemorySize64,
            objects = Performance.GetMonitor(Performance.Monitor.ObjectCount),
            nodes = Performance.GetMonitor(Performance.Monitor.ObjectNodeCount),
            videoBytes = Performance.GetMonitor(Performance.Monitor.RenderVideoMemUsed)
        });
        GD.Print("PLANET_CAPTURE " + file);
    }
}
