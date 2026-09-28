using System;
using System.Collections.Generic;
using System.Linq;
using Godot;
using Game.Simulation.Knowledge;
using Game.Simulation.Models;
using Game.Simulation.Generation;
using Game.Simulation.Exploration;
using Game.Presentation.Spatial;

namespace Game.Presentation;

public partial class Main
{
    private const float DeepFieldOverviewOpacity = .46f;
    private const float RegionalMaximumZoom = 192f;
    private const float RegionalSystemEntryZoom = 18f;
    private const int RegionalBackdropStarCount = 260;
    private const int RegionalBackdropClusterStarCount = 96;
    private readonly List<RegionalBackdropStar> _regionalBackdropStars = new();
    private int _visibleRegionalPointCount;
    private static Texture2D? _regionalPointBloom;
    private static Texture2D? _regionalPointCore;
    private Vector2 _regionalBackdropSize;
    private long _regionalBackdropSeed = long.MinValue;
    private float RegionalOpacity => Math.Clamp(1 - UiOverviewBlend * 2, 0, 1);
    private float CatalogOpacity => 0.90f + RegionalOpacity * 0.10f;
    /// <summary>
    /// The nearby-star catalog stores projected light-years. This rendering multiplier is only
    /// a camera convenience: it never changes positions used by travel, lanes, or distance UI.
    /// </summary>
    private bool UsesSolarNeighborhoodMap => _galaxy?.GenerationMetadata?.GalaxyShape == "Solar neighborhood";
    private bool UsesFullGalaxyMap => _galaxy?.GenerationMetadata?.GalaxyShape == "Full galaxy";
    public float UiCatalogVisualCoordinateScale => UsesSolarNeighborhoodMap || UsesFullGalaxyMap ? 14.0f : 1.0f;
    public string UiOverviewName => UsesSolarNeighborhoodMap || UsesFullGalaxyMap ? "Galaxy" : "Milky Way";
    private object? _artworkFrameCampaign;
    private Rect2 _artworkWorldFrame;
    private readonly Dictionary<int, StarSystemState> _mapSystemsById = new();
    private Color MapColor(Color color) => VisualPalette.WithAlpha(color, color.A * CatalogOpacity);
    private Color MapAlpha(Color color, float alpha) => VisualPalette.WithAlpha(color, alpha * CatalogOpacity);
    private static bool IsPulsarClass(StellarPrimaryClass? stellarClass) =>
        stellarClass?.ToString() == "Pulsar";
    private string PublicSystemName(StarSystemState system, int playerId) =>
        _galaxy.Knowledge.GetSystemSurveyLevel(playerId, system.Id) == SystemSurveyLevel.Unknown
            ? "Unknown" : system.Name;
    public Rect2 UiGalaxyArtworkScreenRect
    {
        get
        {
            var frame = GalaxyArtworkWorldFrame();
            return new(ProjectionToScreen(new(frame.Position.X, frame.Position.Y), UiMapOriginScreen),
                frame.Size * UiMapZoom);
        }
    }

    /// <summary>The cosmetic galaxy is present only in the overview and never changes map hits.</summary>
    public bool UiHasVisibleGalaxyArtwork => !UiIsSystemSpatialView && UiOverviewBlend > .002f &&
        UiGalaxyArtworkScreenRect.Size.X > 1 && UiGalaxyArtworkScreenRect.Size.Y > 1;

    private Rect2 GalaxyArtworkWorldFrame()
    {
        if (ReferenceEquals(_artworkFrameCampaign, _galaxy)) return _artworkWorldFrame;
        _artworkFrameCampaign = _galaxy;
        _mapSystemsById.Clear();
        if (_galaxy is not null)
            foreach (var system in _galaxy.Systems) _mapSystemsById[system.Id] = system;
        return _artworkWorldFrame = ComputeGalaxyArtworkWorldFrame();
    }

    private Rect2 ComputeGalaxyArtworkWorldFrame()
    {
        var frame = SpatialNavigationLayout.GalaxyWorldFrame;
        if (UsesFullGalaxyMap && _galaxy?.GalacticCore is { } core)
        {
            var coordinateScale = UiCatalogVisualCoordinateScale;
            var center = new Vector2(core.X, core.Y) * coordinateScale;
            var radius = core.ExclusionRadius / .14f;
            // Share the generator's centre and inclined arms, with padding for local clusters.
            var required = radius;
            foreach (var system in _galaxy.Systems)
            {
                var dx = system.Position.X - core.X;
                var dy = (system.Position.Y - core.Y) / .72f;
                required = Math.Max(required, MathF.Sqrt(dx * dx + dy * dy));
            }
            var side = required * coordinateScale * 2f * 1.04f / .81818182f;
            return new(center - Vector2.One * side * .5f, Vector2.One * side);
        }
        if (UsesSolarNeighborhoodMap && _galaxy?.Systems.Count > 0)
        {
            var scale = UiCatalogVisualCoordinateScale;
            var first = _galaxy.Systems[0].Position;
            var bounds = new Rect2(first.X * scale, first.Y * scale, 0, 0);
            foreach (var system in _galaxy.Systems)
                bounds = bounds.Expand(new Vector2(system.Position.X * scale, system.Position.Y * scale));

            // The shader's inclined disc reaches .818 of the frame half-width and .72 of that
            // vertically. Size the shared square from elliptical distance so every measured
            // star is inside visible dust, including the catalogue's tall outer coordinates.
            var center = bounds.GetCenter();
            var requiredRadius = 0.0f;
            foreach (var system in _galaxy.Systems)
            {
                var offset = new Vector2(system.Position.X * scale, system.Position.Y * scale) - center;
                requiredRadius = Math.Max(requiredRadius,
                    MathF.Sqrt(offset.X * offset.X + offset.Y * offset.Y / (.72f * .72f)));
            }
            var side = Math.Max(144.0f, requiredRadius * 2.0f * 1.08f / .81818182f);
            return new Rect2(center - Vector2.One * side * .5f, Vector2.One * side);
        }
        if (_galaxy?.GenerationMetadata?.GalaxyShape != "Barred spiral" && _galaxy?.Systems.Count > 0)
        {
            // Preserve old disk-save coordinates and surround that catalog with matching disk dust.
            var radius = _galaxy.Systems.Max(system => system.Position.Length());
            var diameter = Math.Max(100, radius * 2.0f / .81818182f);
            return new Rect2(-Vector2.One * diameter * .5f, Vector2.One * diameter);
        }
        return new Rect2(frame.Left, frame.Top, frame.Width, frame.Height);
    }
    private readonly Dictionary<(FleetRole Role, System.Numerics.Vector2 Position), (FleetState Fleet, int Count)> _visualFleetGroups = new();
    private object? _laneCampaign;
    private IReadOnlyList<InterstellarLane> _interstellarLanes = Array.Empty<InterstellarLane>();
    public bool UiHasDeepField => SpaceArtwork.DeepField is not null;
    /// <summary>Resolved background galaxies belong to the whole-galaxy view only.</summary>
    public float UiGalaxyDeepFieldOpacity => UiIsSystemSpatialView ? 0 : DeepFieldOverviewOpacity * UiOverviewBlend;
    public float UiRegionalMaximumZoom => RegionalMaximumZoom;
    public float UiRegionalSystemEntryZoom => RegionalSystemEntryZoom;
    public int UiRegionalBackdropStarCount => _regionalBackdropStars.Count;
    public float UiRegionalBackdropOpacity => UiIsSystemSpatialView ? 0 : RegionalOpacity;
    public int UiVisibleRegionalPointCount => UiIsSystemSpatialView || UiIsSurfaceOpen ? 0 : _visibleRegionalPointCount;

    /// <summary>Apparent catalogue-star radius shared by drawing and pointer hit testing.</summary>
    public float UiCatalogStarRadius(int systemId)
    {
        GalaxyArtworkWorldFrame();
        if (!_mapSystemsById.TryGetValue(systemId, out var system)) return 0;
        var radius = StarMapDiscGeometry.For(system.StellarClass, _zoom).HaloRadius;
        if (_galaxy!.Knowledge.GetSystemSurveyLevel(_galaxy.PlayerCivilizationId, systemId) == SystemSurveyLevel.Unknown)
            radius = Math.Max(12.0f, radius * .86f);
        // At the complete-galaxy scale the catalogue reads as fine positional points over
        // the arms. Regional and close zoom retain the larger inspectable flare unchanged.
        return Mathf.Lerp(radius, 3.2f, UiOverviewBlend);
    }

    /// <summary>Drawn stellar disc grows with approach and the saved stellar class.</summary>
    public float UiCatalogStarCoreRadius(int systemId) => UiCatalogStarRadius(systemId) <= 0 ? 0 :
        Mathf.Lerp(StarMapDiscGeometry.For(_mapSystemsById[systemId].StellarClass, _zoom).CoreRadius, 1.05f, UiOverviewBlend);

    /// <summary>
    /// Complete regional presentation. Stellar coordinates are the existing catalog transform;
    /// physical stellar hue is visible with the public coordinate catalog; names, class labels,
    /// hazards and system facts retain their established survey and civilization gates.
    /// </summary>
    protected void DrawVisualMapOverlay()
    {
        _visibleRegionalPointCount = 0;
        if (UiIsSystemSpatialView || UiIsSurfaceOpen)
            return;
        var viewport = GetViewportRect().Size;
        DrawRegionalSpace(viewport);
        if (_galaxy is null)
            return;

        var playerId = _galaxy.PlayerCivilizationId;
        var homeId = _galaxy.Civilizations.First(civilization => civilization.Id == playerId).HomeSystemId;
        var center = viewport * 0.5f + _pan;
        DrawGalacticCore(center);
        DrawStrategicTerritoryOverlay(center, playerId);
        DrawKnownInterstellarLanes(center, playerId);
        DrawVisualPlayerRoutes(center, playerId);

        foreach (var system in _galaxy.Systems)
        {
            var position = ToScreen(system.Position, center);
            var survey = _galaxy.Knowledge.GetSystemSurveyLevel(playerId, system.Id);
            var selected = system.Id == _selectedSystemId;
            var home = system.Id == homeId;
            // A star's apparent hue is part of the public sky/catalog view. Do not use the
            // archetype as a fallback here: it encodes survey-gated strategic information.
            var hasSpectralHue = system.StellarClass.HasValue;
            var color = MapColor(hasSpectralHue
                ? IsPulsarClass(system.StellarClass) ? new Color("79cfff") : GetSpectralStarColor(system.StellarClass)
                : new Color(0.63f, 0.70f, 0.79f));
            var radius = UiCatalogStarRadius(system.Id);
            // Close stars retain a broad corona, so their cull margin grows with the same
            // apparent radius used by the point flare and hit target.
            var visualExtent = Math.Max(48.0f, radius * 2.4f + 18.0f);
            if (position.X < -visualExtent || position.Y < -visualExtent ||
                position.X > viewport.X + visualExtent || position.Y > viewport.Y + visualExtent)
                continue;
            var isBlackHole = system.StellarClass == StellarPrimaryClass.BlackHole ||
                (!hasSpectralHue && system.Archetype == StarArchetype.BlackHole);
            if (survey == SystemSurveyLevel.FullySurveyed && isBlackHole)
            {
                CinematicArt.DrawStarlight(this, position, radius * 1.25f, new Color("db9460"), .85f);
                DrawCircle(position, radius * .65f, Colors.Black, true, -1, true);
            }
            else if (hasSpectralHue)
                DrawSpectralCatalogStar(system.Id, position, radius, color, StrategicUnexploredStarAlpha(system.Id));
            else
                CinematicArt.DrawStarlight(this, position, radius, color,
                    (.72f + RegionalOpacity * .28f) * StrategicUnexploredStarAlpha(system.Id));

            if (survey == SystemSurveyLevel.FullySurveyed)
            {
                var isNeutronStar = system.StellarClass is StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar ||
                    (!hasSpectralHue && system.Archetype == StarArchetype.NeutronPulsar);
                if (isNeutronStar)
                    DrawLine(position + new Vector2(-radius * 2.8f, radius * .65f),
                        position + new Vector2(radius * 2.8f, -radius * .65f), MapAlpha(color, .72f), 1.1f, true);
                else if (system.Archetype == StarArchetype.Dangerous)
                    DrawArc(position, radius + 2.8f, -.65f, .5f, 14, MapAlpha(new Color("ffb35f"), .78f), 1.2f, true);
                else if (system.Archetype == StarArchetype.Legendary)
                {
                    DrawLine(position + new Vector2(-radius * 2.2f, 0), position + new Vector2(radius * 2.2f, 0), MapAlpha(color, .42f), 1, true);
                    DrawLine(position + new Vector2(0, -radius * 2.2f), position + new Vector2(0, radius * 2.2f), MapAlpha(color, .42f), 1, true);
                }
                else if (system.Archetype == StarArchetype.Nebula)
                    DrawCircle(position, radius + 4.5f, MapAlpha(new Color("9a6bd5"), .12f));
            }

            // At complete-galaxy scale the physical star point already shows a fully
            // surveyed system. Keep progress arcs for incomplete surveys, and restore the
            // complete ring as the camera approaches the high-detail regional view.
            if (survey >= SystemSurveyLevel.Detected &&
                (survey != SystemSurveyLevel.FullySurveyed || UiOverviewBlend < .65f))
            {
                var extent = survey switch
                {
                    SystemSurveyLevel.Detected => MathF.PI * 0.45f,
                    SystemSurveyLevel.PartiallySurveyed => MathF.PI * 1.25f,
                    _ => MathF.PI * 2.0f,
                };
                var surveyRingGap = Mathf.Lerp(5.0f, 1.5f, UiOverviewBlend);
                // At galaxy scale these rings are only a few pixels wide. Twelve segments
                // stay visually round there and avoid tessellating 20,000 tiny line segments
                // every frame in a fully surveyed 500-system catalogue.
                var surveyRingSegments = (int)Mathf.Round(Mathf.Lerp(40, 12, UiOverviewBlend));
                DrawArc(position, radius * 1.14f + surveyRingGap, -MathF.PI * 0.5f, -MathF.PI * 0.5f + extent, surveyRingSegments,
                    MapAlpha(survey == SystemSurveyLevel.FullySurveyed ? color : VisualPalette.TextSecondary, 0.48f), 1.0f, true);
            }

            if (selected)
                DrawRegionalReticle(position, Math.Max(18.0f, radius + 11.0f), MapColor(VisualPalette.Selected));
            // Hundreds of overlapping names are unreadable in the complete-galaxy view and
            // dominate its draw cost. Full catalogue labels return continuously as the camera
            // approaches regional scale; the selected and home systems always remain named.
            if (selected || home || (survey >= SystemSurveyLevel.PartiallySurveyed && _zoom >= 0.88f && UiOverviewBlend < .65f))
            {
                var label = PublicSystemName(system, playerId);
                var labelColor = MapColor(selected ? VisualPalette.TextPrimary : VisualPalette.TextSecondary);
                var fontSize = selected || home ? 14 : 12;
                var labelWidth = _font.GetStringSize(label, HorizontalAlignment.Left, -1, fontSize).X;
                var labelPosition = position + new Vector2(-labelWidth * .5f,
                    Math.Max(radius + 23.0f, radius * 1.72f + 14.0f));
                DrawString(_font, labelPosition + Vector2.One, label, HorizontalAlignment.Left, -1, fontSize, MapColor(VisualPalette.Canvas));
                DrawString(_font, labelPosition, label, HorizontalAlignment.Left, -1, fontSize, labelColor);
                if (home)
                {
                    const string homeLabel = "HOME SYSTEM";
                    var homeWidth = _font.GetStringSize(homeLabel, HorizontalAlignment.Left, -1, 9).X;
                    DrawString(_font, labelPosition + new Vector2((labelWidth - homeWidth) * .5f, 15.0f), homeLabel,
                        HorizontalAlignment.Left, -1, 9, MapColor(VisualPalette.Success));
                }
            }
        }

        DrawVisualColonies(center, playerId);
        DrawVisualKnownCivilizationHomes(center, playerId);
        DrawVisualPlayerFleets(center, playerId);
    }

    private void DrawGalacticCore(Vector2 mapCenter)
    {
        var core = _galaxy?.GalacticCore;
        if (core is null)
            return;

        var center = ToScreen(new System.Numerics.Vector2(core.X, core.Y), mapCenter);
        if (UiGalacticCore is null)
        {
            DrawUndisclosedCoreFog(center, core.ExclusionRadius * UiCatalogVisualCoordinateScale * UiMapZoom);
            return;
        }
        // This mask maps the exact generated exclusion radius into the current world view.
        // The icon itself is capped separately, so zoom never makes the void larger than its
        // authoritative star-free region.
        var reservedRadius = UiGalacticCoreScreenRadius;
        // A transparent falloff softens the boundary against dust. The opaque void starts at
        // precisely the generated radius, so it never conceals a real catalogue coordinate.
        for (var fade = 4; fade >= 1; fade--)
            DrawCircle(center, reservedRadius * (1.0f + fade * .045f),
                MapAlpha(new Color("120b0b"), .016f + fade * .010f), false, 1.4f, true);
        DrawCircle(center, reservedRadius, new Color("02050a"), true, -1, true);
        var ringRadius = Math.Min(Math.Clamp(reservedRadius * .42f, 8.0f, 66.0f), reservedRadius * .68f);
        var rotation = -.36f;
        var horizontal = ringRadius * 1.78f;
        var vertical = ringRadius * .31f;

        // A tilted accretion disc: a subdued far side, particulate intermediate strokes, then
        // a hot foreground arc. It is deliberately drawn from a fixed small number of vectors.
        Vector2 Ellipse(float angle, float scale = 1.0f)
        {
            var x = MathF.Cos(angle) * horizontal * scale;
            var y = MathF.Sin(angle) * vertical * scale;
            return center + new Vector2(x * MathF.Cos(rotation) - y * MathF.Sin(rotation),
                x * MathF.Sin(rotation) + y * MathF.Cos(rotation));
        }
        for (var band = 0; band < 3; band++)
        {
            var scale = 1.0f - band * .115f;
            for (var segment = 0; segment < 28; segment++)
            {
                var first = segment * MathF.Tau / 28.0f;
                var second = (segment + 1) * MathF.Tau / 28.0f;
                var foreground = MathF.Sin((first + second) * .5f) > -.10f;
                var colour = foreground
                    ? MapAlpha(band == 0 ? new Color("ffbd62") : new Color("d96b2c"), .34f - band * .075f)
                    : MapAlpha(new Color("6a2518"), .24f - band * .045f);
                DrawLine(Ellipse(first, scale), Ellipse(second, scale), colour,
                    foreground ? 1.8f - band * .28f : 1.0f, true);
            }
        }
        // Compact lensing arcs bend around the horizon instead of reading as a UI target ring.
        DrawArc(center + new Vector2(-ringRadius * .42f, -ringRadius * .32f), ringRadius * .80f,
            -.95f, .18f, 18, MapAlpha(new Color("ffd38a"), .46f), 1.15f, true);
        DrawArc(center + new Vector2(ringRadius * .38f, ringRadius * .20f), ringRadius * .98f,
            2.22f, 3.04f, 18, MapAlpha(new Color("e9863e"), .38f), 1.0f, true);
        DrawCircle(center, ringRadius * .52f, new Color("000104"), true, -1, true);
        DrawArc(center, ringRadius * .54f, .18f, 2.86f, 24, MapAlpha(new Color("ffcb75"), .52f), 1.0f, true);
        if (UiOverviewBlend > .08f || _zoom < .72f)
        {
            var label = center + new Vector2(ringRadius * 1.55f, -ringRadius * .52f);
            DrawString(_font, label + Vector2.One, "Galactic core", HorizontalAlignment.Left, -1, 14, Colors.Black);
            DrawString(_font, label, "Galactic core", HorizontalAlignment.Left, -1, 14, MapColor(VisualPalette.TextPrimary));
            DrawString(_font, label + new Vector2(0, 15), "Supermassive black hole · Access unavailable",
                HorizontalAlignment.Left, -1, 10, MapAlpha(new Color("f0ae67"), .90f));
        }
    }

    private void DrawKnownInterstellarLanes(Vector2 center, int playerId)
    {
        if (!ReferenceEquals(_laneCampaign, _galaxy))
        {
            _laneCampaign = _galaxy;
            _interstellarLanes = new InterstellarLaneNetwork().Build(_galaxy.Systems);
        }
        var maximumPlayerLeg = _galaxy.Fleets
            .Where(fleet => fleet.IsActive && fleet.CivilizationId == playerId)
            .Select(fleet => fleet.MaximumLegRangeLightYears)
            .DefaultIfEmpty(0.0)
            .Max();
        foreach (var lane in _interstellarLanes)
        {
            if (!_galaxy.Knowledge.IsSystemKnown(playerId, lane.FirstSystemId) ||
                !_galaxy.Knowledge.IsSystemKnown(playerId, lane.SecondSystemId)) continue;
            var first = _galaxy.Systems.First(system => system.Id == lane.FirstSystemId);
            var second = _galaxy.Systems.First(system => system.Id == lane.SecondSystemId);
            var start = ToScreen(first.Position, center);
            var end = ToScreen(second.Position, center);
            if (lane.LengthLightYears <= maximumPlayerLeg + 0.0001)
            {
                var reachable = MapAlpha(VisualPalette.Selected, .15f + RegionalOpacity * .20f);
                DrawLine(start, end, reachable, 1.05f, true);
            }
            else
            {
                var blocked = MapAlpha(new Color("d08b62"), .10f + RegionalOpacity * .08f);
                DrawDashedLine(start, end, blocked, .75f, 9.0f);
            }
        }
    }

    private void DrawRegionalSpace(Vector2 size)
    {
        DrawRect(new Rect2(Vector2.Zero, size), new Color("02050a"));
        SpaceArtwork.DrawDeepField(this, size, UiGalaxyDeepFieldOpacity);
        var regionalOpacity = UiRegionalBackdropOpacity;
        if (regionalOpacity > .002f)
        {
            // At stellar-region scale the background is a local sky, not a deep-field photo:
            // visible stars, clusters, and nebula replace resolved external galaxies.
            DrawRegionalBackdrop(size, regionalOpacity);
            SpaceArtwork.DrawNebula(this, size, _pan, .56f * regionalOpacity);
        }
        if (UiOverviewBlend > 0)
        {
            if (UsesSolarNeighborhoodMap || UsesFullGalaxyMap)
            {
                // A soft local underlay separates the primary spiral from the detailed deep
                // field without dimming the resolved background galaxies outside its frame.
                var frame = UiGalaxyArtworkScreenRect;
                DrawTextureRect(RegionalPointBloom, frame, false,
                    new Color(.004f, .008f, .016f, .52f * UiOverviewBlend));
            }
            SpaceArtwork.DrawGalaxyOverview(this, UiGalaxyArtworkScreenRect, _galaxy?.Seed ?? 0, UiOverviewBlend,
                UsesSolarNeighborhoodMap || UsesFullGalaxyMap || _galaxy?.GenerationMetadata?.GalaxyShape == "Barred spiral",
                UsesSolarNeighborhoodMap || UsesFullGalaxyMap ? 1.38f : 1.0f,
                _galaxy?.GalacticCore is null ? 0 : .14f);
        }
    }

    private readonly record struct RegionalBackdropStar(Vector2 Position, float Radius, float Alpha, Color Color);

    private void DrawRegionalBackdrop(Vector2 size, float opacity)
    {
        var seed = _galaxy?.Seed ?? 0;
        if (_regionalBackdropSize != size || _regionalBackdropSeed != seed)
            RebuildRegionalBackdrop(size, seed);

        // A slight pan parallax makes the field read as distant scenery. The coordinates are
        // decorative only and intentionally never enter catalogue hit testing.
        var parallaxFactor = .012f + Math.Min(.045f, _zoom * .0012f);
        var parallax = _pan * parallaxFactor;
        foreach (var star in _regionalBackdropStars)
        {
            var position = new Vector2(WrapBackdropCoordinate(star.Position.X + parallax.X, size.X),
                WrapBackdropCoordinate(star.Position.Y + parallax.Y, size.Y));
            // A textured point is a four-vertex quad. The previous procedurally tessellated
            // circle produced dozens of vertices for each of the 356 decorative stars every
            // frame, although these sub-pixel points have no visible geometric detail to keep.
            var diameter = star.Radius * 2.0f;
            DrawTextureRect(RegionalPointCore, new Rect2(position - Vector2.One * star.Radius,
                new Vector2(diameter, diameter)), false,
                new Color(star.Color.R, star.Color.G, star.Color.B, star.Alpha * opacity));
        }
    }

    private static float WrapBackdropCoordinate(float value, float extent)
    {
        if (extent <= 0) return 0;
        var wrapped = value % extent;
        return wrapped < 0 ? wrapped + extent : wrapped;
    }

    private void RebuildRegionalBackdrop(Vector2 size, long seed)
    {
        _regionalBackdropSize = size;
        _regionalBackdropSeed = seed;
        _regionalBackdropStars.Clear();
        var random = new Random(unchecked((int)(seed ^ (seed >> 32) ^ 0x4d4150)));
        void AddStar(float x, float y, bool clustered)
        {
            var cool = random.NextDouble();
            var color = cool < .16 ? new Color("96bfff") : cool > .87 ? new Color("ffd6ab") : new Color("d8e5ff");
            _regionalBackdropStars.Add(new RegionalBackdropStar(new Vector2(x, y),
                clustered ? .42f + (float)random.NextDouble() * .72f : .32f + (float)random.NextDouble() * .62f,
                clustered ? .16f + (float)random.NextDouble() * .25f : .09f + (float)random.NextDouble() * .19f, color));
        }
        for (var index = 0; index < RegionalBackdropStarCount; index++)
            AddStar((float)random.NextDouble() * size.X, (float)random.NextDouble() * size.Y, false);
        for (var cluster = 0; cluster < 3; cluster++)
        {
            var center = new Vector2((.18f + (float)random.NextDouble() * .64f) * size.X,
                (.18f + (float)random.NextDouble() * .64f) * size.Y);
            for (var index = 0; index < RegionalBackdropClusterStarCount / 3; index++)
            {
                var angle = (float)random.NextDouble() * MathF.Tau;
                var distance = MathF.Sqrt((float)random.NextDouble()) * Math.Min(size.X, size.Y) * .105f;
                AddStar(center.X + MathF.Cos(angle) * distance, center.Y + MathF.Sin(angle) * distance, true);
            }
        }
    }

    private void DrawStrategicCoordinateLayer(Vector2 size)
    {
        var opacity = RegionalOpacity * Math.Clamp((_zoom - 0.08f) * 1.8f, 0.0f, 0.16f);
        if (opacity <= 0.002f) return;
        var spacing = Math.Clamp(260.0f * _zoom, 72.0f, 210.0f);
        var origin = size * 0.5f + _pan;
        var offsetX = ((origin.X % spacing) + spacing) % spacing;
        var offsetY = ((origin.Y % spacing) + spacing) % spacing;
        var minor = VisualPalette.WithAlpha(VisualPalette.Keyline, opacity * 0.48f);
        var major = VisualPalette.WithAlpha(VisualPalette.Selected, opacity);
        var index = 0;
        for (var x = offsetX; x < size.X; x += spacing, index++)
            DrawLine(new Vector2(x, 72), new Vector2(x, size.Y), index % 4 == 0 ? major : minor, index % 4 == 0 ? 1.0f : 0.6f, true);
        index = 0;
        for (var y = offsetY; y < size.Y; y += spacing, index++)
            DrawLine(new Vector2(0, y), new Vector2(size.X, y), index % 4 == 0 ? major : minor, index % 4 == 0 ? 1.0f : 0.6f, true);
        DrawCircle(UiMapOriginScreen, 34.0f, VisualPalette.WithAlpha(VisualPalette.Selected, opacity * 1.6f), false, 1.2f, true);
    }

    private void DrawVisualColonies(Vector2 center, int playerId)
    {
        foreach (var colony in _galaxy.Colonies)
        {
            var own = colony.CivilizationId == playerId;
            if (!own &&
                (!_galaxy.Knowledge.IsSystemFullySurveyed(playerId, colony.SystemId) ||
                 !_galaxy.Knowledge.IsCivilizationKnown(playerId, colony.CivilizationId)))
                continue;
            var system = _galaxy.Systems.First(candidate => candidate.Id == colony.SystemId);
            var anchor = ToScreen(system.Position, center);
            var marker = anchor + new Vector2(-18.0f, -19.0f);
            var color = MapColor(own ? VisualPalette.Success : new Color(0.66f, 0.62f, 0.77f));
            DrawLine(anchor + new Vector2(-5.0f, -5.0f), marker, MapAlpha(color, 0.42f), 1.0f, true);
            DrawCircle(marker, 9.0f, MapColor(VisualPalette.Canvas));
            DrawVisualIcon(VisualIconLibrary.Colony, marker, own ? 17.0f : 15.0f, color);
            if (!own)
                DrawCircle(marker, 10.0f, MapAlpha(color, 0.52f), false, 0.8f, true);
        }
    }

    private void DrawVisualKnownCivilizationHomes(Vector2 center, int playerId)
    {
        foreach (var civilization in _galaxy.Civilizations)
        {
            if (civilization.Id == playerId ||
                !_galaxy.Knowledge.IsCivilizationKnown(playerId, civilization.Id) ||
                !_galaxy.Knowledge.IsSystemFullySurveyed(playerId, civilization.HomeSystemId))
                continue;
            var home = _galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
            var anchor = ToScreen(home.Position, center);
            var marker = anchor + new Vector2(17.0f, -34.0f);
            var color = MapColor(civilization.IsSeededAncient
                ? new Color(0.80f, 0.67f, 0.42f) : new Color(0.66f, 0.62f, 0.77f));
            DrawLine(anchor + new Vector2(4.0f, -8.0f), marker, MapAlpha(color, 0.34f), 1.0f, true);
            DrawCircle(marker, 9.0f, MapColor(VisualPalette.Canvas));
            DrawVisualIcon(VisualIconLibrary.DiplomacyContact, marker, 15.0f, color);
        }
    }

    private void DrawVisualPlayerRoutes(Vector2 center, int playerId)
    {
        foreach (var fleet in _galaxy.Fleets)
        {
            if (!fleet.IsActive || fleet.CivilizationId != playerId || fleet.DestinationSystemId is not int destinationId)
                continue;
            var start = ToScreen(fleet.Position, center);
            // Ownership is conveyed consistently at every map scale. Role remains in the
            // silhouette, so a player never mistakes a foreign palette for an owned vessel.
            var color = MapColor(VisualPalette.Success);
            var routeIds = fleet.PlannedRouteSystemIds.Count > 0
                ? fleet.PlannedRouteSystemIds
                : new List<int> { destinationId };
            var currentLeg = true;
            foreach (var routeSystemId in routeIds)
            {
                var destination = _galaxy.Systems.First(system => system.Id == routeSystemId);
                var end = ToScreen(destination.Position, center);
                DrawLine(start, end, MapAlpha(color, 0.07f), 5.0f, true);
                DrawDashedLine(start, end, MapAlpha(color, 0.60f), 1.15f, 8.0f);
                if (start.DistanceSquaredTo(end) > 1600.0f)
                {
                    var direction = (end - start).Normalized();
                    var normal = new Vector2(-direction.Y, direction.X);
                    var tip = start.Lerp(end, 0.62f);
                    DrawLine(tip, tip - direction * 7.0f + normal * 3.5f, color, 1.3f, true);
                    DrawLine(tip, tip - direction * 7.0f - normal * 3.5f, color, 1.3f, true);
                }
                if (currentLeg && start.DistanceSquaredTo(end) > 16.0f)
                {
                    // The state-owned fleet position is the route-progress marker. These three
                    // bounded strokes only appear while a real destination is active, so they
                    // freeze with simulation time and never invent a separate travel animation.
                    var heading = (end - start).Normalized();
                    for (var trail = 0; trail < 3; trail++)
                    {
                        var offset = 4.0f + trail * 4.0f;
                        DrawLine(start - heading * offset, start - heading * (offset + 2.4f),
                            MapAlpha(color, .58f - trail * .16f), 1.15f - trail * .18f, true);
                    }
                    DrawCircle(start, 2.2f, MapAlpha(color, .86f), true, -1, true);
                }
                start = end;
                currentLeg = false;
            }
        }
    }

    private void DrawVisualPlayerFleets(Vector2 center, int playerId)
    {
        // Reuse the dictionary rather than allocating LINQ groups each frame. Only exact-own
        // co-located ships share a count; every actual course is still drawn separately above.
        _visualFleetGroups.Clear();
        foreach (var fleet in _galaxy.Fleets)
        {
            if (!fleet.IsActive || fleet.CivilizationId != playerId)
                continue;
            var key = (fleet.Role, fleet.Position);
            _visualFleetGroups[key] = _visualFleetGroups.TryGetValue(key, out var group)
                ? (group.Fleet, group.Count + 1) : (fleet, 1);
        }
        foreach (var group in _visualFleetGroups.Values)
        {
            var fleet = group.Fleet;
            var anchor = ToScreen(fleet.Position, center);
            var position = FleetMarkerScreenPosition(fleet, center);
            if (SelectedFleet is { } selected && selected.Role == fleet.Role && selected.Position == fleet.Position)
                DrawRegionalReticle(position, 17, VisualUi.Accent);
            var color = MapColor(VisualPalette.Success);
            DrawLine(anchor, position, MapAlpha(color, 0.36f), 1.0f, true);
            DrawCircle(position, 13.0f, MapColor(new Color(0.025f, 0.055f, 0.080f, 0.96f)));
            DrawCircle(position, 13.0f, MapAlpha(color, 0.50f), false, 1.0f, true);
            DrawVisualIcon(FleetRoleTexture(fleet.Role), position, 23.0f, color);
            if (group.Count > 1)
            {
                var badge = position + new Vector2(10.0f, -10.0f);
                DrawCircle(badge, 8.0f, MapColor(VisualPalette.SurfacePrimary));
                DrawCircle(badge, 8.0f, color, false, 1.0f, true);
                DrawString(_font, badge + new Vector2(-8.0f, 3.0f), group.Count > 99 ? "99+" : group.Count.ToString(),
                    HorizontalAlignment.Center, 16.0f, 9, MapColor(VisualPalette.TextPrimary));
            }
        }
    }

    private void DrawRegionalReticle(Vector2 position, float radius, Color color)
    {
        for (var corner = 0; corner < 4; corner++)
        {
            var angle = corner * MathF.PI * 0.5f + 0.18f;
            DrawArc(position, radius, angle, angle + MathF.PI * 0.5f - 0.36f, 14, color, 1.6f, true);
        }
        DrawCircle(position, radius + 4.0f, MapAlpha(color, 0.11f), false, 1.0f, true);
    }

    /// <summary>Regional catalogue stars are luminous points: a compact hot core, colored
    /// corona, and tapered diffraction rays. They never resolve into a solar surface.</summary>
    private void DrawSpectralCatalogStar(int systemId, Vector2 position, float haloRadius, Color spectral, float surveyOpacity)
    {
        _visibleRegionalPointCount++;
        var opacity = CatalogOpacity * surveyOpacity;
        var coreRadius = UiCatalogStarCoreRadius(systemId);
        var regional = RegionalOpacity;
        if (UiOverviewBlend > .02f)
        {
            // These subpixel stars cannot resolve six diffraction rays and diagonal strokes.
            // Keep their real colour and position in two same-texture quads; Godot can batch
            // the complete distant catalogue instead of switching material thousands of times.
            DrawTextureRect(RegionalPointBloom,
                new(position - Vector2.One * haloRadius, Vector2.One * haloRadius * 2), false,
                new Color(spectral.R, spectral.G, spectral.B, .70f * opacity));
            DrawTextureRect(RegionalPointBloom,
                new(position - Vector2.One * coreRadius, Vector2.One * coreRadius * 2), false,
                new Color(Mathf.Lerp(spectral.R, 1f, .60f), Mathf.Lerp(spectral.G, 1f, .60f),
                    Mathf.Lerp(spectral.B, 1f, .60f), .98f * opacity));
            return;
        }
        // RegionalPointBloom has a broad radial falloff; the shared CinematicArt glow is
        // intentionally much tighter and therefore unsuitable for a visible map corona.
        var halo = haloRadius * Mathf.Lerp(1.55f, 1.0f, UiOverviewBlend);
        DrawTextureRect(RegionalPointBloom, new Rect2(position - Vector2.One * halo, Vector2.One * halo * 2), false,
            new Color(spectral.R, spectral.G, spectral.B, (.48f + regional * .24f) * opacity));
        var innerHalo = haloRadius * .64f;
        DrawTextureRect(RegionalPointBloom, new Rect2(position - Vector2.One * innerHalo, Vector2.One * innerHalo * 2), false,
            new Color(spectral.R, spectral.G, spectral.B, (.42f + regional * .22f) * opacity));

        var regionalRay = Math.Clamp(haloRadius * 1.65f, 22.0f, 30.0f);
        var ray = Mathf.Lerp(regionalRay, Math.Max(4.8f, haloRadius * 1.45f), UiOverviewBlend);
        var rayColor = new Color(spectral.R, spectral.G, spectral.B, (.34f + regional * .20f) * opacity);
        var brightRay = new Color(1f, .97f, .91f, (.34f + regional * .18f) * opacity);
        // Thin stretched radial gradients naturally taper from the hot core to transparent
        // endpoints without generated geometry or a per-star shader.
        DrawTextureRect(RegionalPointBloom, new Rect2(position - new Vector2(ray, 1.0f), new Vector2(ray * 2, 2.0f)), false, rayColor);
        DrawTextureRect(RegionalPointBloom, new Rect2(position - new Vector2(1.0f, ray), new Vector2(2.0f, ray * 2)), false, rayColor);
        DrawTextureRect(RegionalPointBloom, new Rect2(position - new Vector2(ray * .56f, .62f), new Vector2(ray * 1.12f, 1.24f)), false, brightRay);
        DrawTextureRect(RegionalPointBloom, new Rect2(position - new Vector2(.62f, ray * .56f), new Vector2(1.24f, ray * 1.12f)), false, brightRay);
        var diagonal = ray * .63f;
        var diagonalColor = new Color(spectral.R, spectral.G, spectral.B, (.11f + regional * .12f) * opacity);
        DrawLine(position - new Vector2(diagonal, diagonal), position + new Vector2(diagonal, diagonal), diagonalColor, .46f, true);
        DrawLine(position - new Vector2(diagonal, -diagonal), position + new Vector2(diagonal, -diagonal), diagonalColor, .46f, true);
        // Keep a compact, bright circular core without asking CanvasItem to tessellate 500
        // individual discs every redraw. This shares a sharp radial texture with the distant
        // backdrop rather than replacing the star's visible corona or diffraction rays.
        var coreDiameter = coreRadius * 2.0f;
        DrawTextureRect(RegionalPointCore, new Rect2(position - Vector2.One * coreRadius,
            new Vector2(coreDiameter, coreDiameter)), false, new Color(1f, .985f, .94f, .98f * opacity));
    }

    // These cached bitmaps keep several transparent texels outside the visible falloff.
    // Linear filtering preserves this padding. Mipmaps are intentionally absent so thin rays
    // cannot select an opaque averaged mip. The core stays smooth at the 192x map limit.
    private static Texture2D RegionalPointBloom => _regionalPointBloom ??=
        RadialLightTexture.Create(256, RadialLightProfile.Bloom);

    private static Texture2D RegionalPointCore => _regionalPointCore ??=
        RadialLightTexture.Create(512, RadialLightProfile.Core);

    private static Texture2D FleetRoleTexture(FleetRole role) => role switch
    {
        FleetRole.Scout => VisualIconLibrary.Scout,
        FleetRole.Science => VisualIconLibrary.ScienceVessel,
        FleetRole.Military => VisualIconLibrary.PatrolCorvette,
        FleetRole.Colony => VisualIconLibrary.ColonyShip,
        _ => VisualIconLibrary.Scout,
    };

    private static Color FleetRoleColor(FleetRole role) => role switch
    {
        FleetRole.Scout => VisualPalette.Selected,
        FleetRole.Science => VisualPalette.Science,
        FleetRole.Military => VisualPalette.Military,
        FleetRole.Colony => VisualPalette.Success,
        _ => VisualPalette.TextPrimary,
    };

    private void DrawVisualIcon(Texture2D texture, Vector2 center, float size, Color color)
    {
        var half = size * 0.5f;
        DrawTextureRect(texture, new Rect2(center.X - half, center.Y - half, size, size), false, color);
    }
}

/// <summary>Compressed map-disc geometry: relative stellar classes remain legible without
/// attempting literal astronomical scale on a strategic chart.</summary>
public readonly record struct StarMapDiscGeometry(float CoreRadius, float HaloRadius)
{
    public static StarMapDiscGeometry For(StellarPrimaryClass? stellarClass, float zoom)
    {
        var relativeRadius = stellarClass switch
        {
            StellarPrimaryClass.MRedDwarf => .45f,
            StellarPrimaryClass.Giant => 5f,
            StellarPrimaryClass.WhiteDwarf => .35f,
            StellarPrimaryClass.NeutronStar or StellarPrimaryClass.Pulsar => .30f,
            StellarPrimaryClass.HotBlueStar => 1.7f,
            StellarPrimaryClass.AWhiteStar => 1.35f,
            StellarPrimaryClass.FYellowWhiteDwarf => 1.15f,
            StellarPrimaryClass.KOrangeDwarf => .75f,
            _ => 1f,
        };
        var closeFraction = Math.Clamp((zoom - 1f) / 191f, 0f, 1f);
        var solarCore = Mathf.Lerp(4f, 60f, MathF.Sqrt(closeFraction));
        var core = Math.Max(2f, solarCore * relativeRadius);
        return new(core, Math.Max(12f, core * 2.8f));
    }
}
