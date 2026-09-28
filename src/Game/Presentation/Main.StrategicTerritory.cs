using System;
using System.Linq;
using Godot;
using Game.Simulation.Diplomacy;

namespace Game.Presentation;

public partial class Main
{
    private object? _territoryCampaign;
    private int _territoryFingerprint;
    private ulong _territoryNextCheckFrame;
    private StrategicTerritoryProjection? _territoryProjection;
    private ImageTexture? _territoryFogTexture;
    private readonly System.Collections.Generic.Dictionary<int, System.Collections.Generic.List<TerritoryFillChunk>> _territoryFillMeshes = new();
    private StrategicTerritoryProjection? _territoryScreenProjection;
    private Vector2 _territoryScreenOrigin;
    private Vector2 _territoryScreenBasisX;
    private Vector2 _territoryScreenBasisY;
    private readonly System.Collections.Generic.Dictionary<int, System.Collections.Generic.List<TerritoryScreenContour>> _territoryScreenContours = new();
    private readonly System.Collections.Generic.Dictionary<int, Rect2> _territoryScreenBounds = new();
    private readonly record struct TerritoryScreenContour(Vector2[] Points, Rect2 Bounds);
    private sealed record TerritoryFillChunk(ArrayMesh Mesh, Rect2 ProjectionBounds);
    private sealed class TerritoryFillChunkBuilder
    {
        public readonly System.Collections.Generic.List<Vector3> Vertices = new();
        public Rect2? Bounds;
        public void Add(System.Numerics.Vector2 first, System.Numerics.Vector2 second, System.Numerics.Vector2 third)
        {
            Vertices.Add(ToGodot3(first)); Vertices.Add(ToGodot3(second)); Vertices.Add(ToGodot3(third));
            foreach (var point in new[] { ToGodot(first), ToGodot(second), ToGodot(third) })
                Bounds = Bounds is Rect2 bounds ? bounds.Expand(point) : new Rect2(point, Vector2.Zero);
        }
    }
    public bool UiTerritoryMapVisible { get; private set; }

    public void UiToggleTerritoryMap()
    {
        UiTerritoryMapVisible = !UiTerritoryMapVisible;
        SetStatus(UiTerritoryMapVisible ? "Territorial influence overlay enabled." : "Territorial influence overlay hidden.");
        QueueRedraw();
    }

    public override void _ExitTree()
    {
        DrainPendingScheduledAutosave();
        ClearTerritoryProjectionCache();
        base._ExitTree();
    }

    private void DrawStrategicTerritoryOverlay(Vector2 center, int playerId)
    {
        if (_galaxy is null || !UiTerritoryMapVisible) return;
        RefreshTerritoryProjection(playerId);
        var projection = _territoryProjection; if (projection is null) return;
        // Territory remains readable for every known civilization. Geometry is batched per
        // owner and contours are one polyline each; regional zoom additionally culls regions
        // whose anchors and label are outside the current camera.
        var detail = .34f + .46f * RegionalOpacity;
        var viewport = GetViewportRect().Grow(42);
        bool Visible(Vector2 point) => viewport.HasPoint(point);
        var meshOrigin = ProjectionToScreen(System.Numerics.Vector2.Zero, center);
        var meshScale = new Vector2(
            ProjectionToScreen(System.Numerics.Vector2.UnitX, center).X - meshOrigin.X,
            ProjectionToScreen(System.Numerics.Vector2.UnitY, center).Y - meshOrigin.Y);
        RefreshTerritoryScreenContours(projection, center);
        // A fully surveyed catalogue has no hidden territory to mask. Avoid blending a
        // viewport-sized transparent texture in that common late-game overview case.
        if (_territoryFogTexture is not null && projection.UnexploredSystemIds.Count > 0)
        {
            var fog = projection.FogMask;
            var rect = new Rect2(ProjectionToScreen(fog.Position, center), ToGodot(fog.Size) * UiMapZoom);
            DrawTextureRect(_territoryFogTexture, rect, false,
                MapAlpha(VisualPalette.Canvas, .095f + .095f * detail));
        }
        foreach (var region in projection.Territories)
        {
            var labelPoint = ProjectionToScreen(region.LabelPosition, center);
            // A connected region can cross the viewport while its label and every anchor sit
            // outside it. Cull against the projected contour bounds so panning never punches
            // an artificial gap through a territory that is actually on screen.
            var regionVisible = UiOverviewBlend > .82f ||
                _territoryScreenBounds.TryGetValue(region.CivilizationId, out var bounds) && viewport.Intersects(bounds) ||
                Visible(labelPoint) || region.Anchors.Any(anchor => Visible(ProjectionToScreen(anchor.Position, center)));
            if (!regionVisible)
                continue;
            var color = TerritoryColor(region.CivilizationId, playerId);
            var fillColor = MapAlpha(color, .075f + .025f * detail);
            if (_territoryFillMeshes.TryGetValue(region.CivilizationId, out var fillChunks))
            {
                DrawSetTransform(meshOrigin, 0f, meshScale);
                foreach (var chunk in fillChunks)
                    if (ProjectionBoundsVisible(chunk.ProjectionBounds, center, viewport))
                        DrawMesh(chunk.Mesh, null, null, fillColor);
                DrawSetTransform(Vector2.Zero, 0f, Vector2.One);
            }
            if (_territoryScreenContours.TryGetValue(region.CivilizationId, out var contours))
            foreach (var contour in contours)
            {
                if (!viewport.Intersects(contour.Bounds)) continue;
                // The antialiased fringe supplies the soft edge around one uniform border;
                // a second coincident polyline doubled regional contour work and darkened it.
                DrawPolyline(contour.Points, MapAlpha(color, .78f * detail), 3.4f, true);
            }
            if (region.Anchors.Count == 0 || (UiOverviewBlend > .82f && region.Anchors.Count < 2)) continue;
            var point = labelPoint; var label = region.CivilizationName.ToUpperInvariant();
            DrawString(_font, point + Vector2.One, label, HorizontalAlignment.Center, 180, 13, MapAlpha(VisualPalette.Canvas, .9f));
            DrawString(_font, point, label, HorizontalAlignment.Center, 180, 13, MapAlpha(color, .82f * detail));
        }
        if (RegionalOpacity > .45f)
        {
            var claimLines = new System.Collections.Generic.Dictionary<int, System.Collections.Generic.List<Vector2>>();
            foreach (var claim in projection.Claims)
            {
                var point = ProjectionToScreen(claim.Position, center);
                if (!Visible(point)) continue;
                var radius = claim.Radius * UiMapZoom;
                if (radius <= 5) continue;
                if (!claimLines.TryGetValue(claim.CivilizationId, out var lines))
                    claimLines[claim.CivilizationId] = lines = new();
                AppendDashedArcLines(lines, point, radius);
            }
            foreach (var pair in claimLines)
                DrawMultiline(pair.Value.ToArray(), MapAlpha(TerritoryColor(pair.Key, playerId), .74f * detail), 1.1f, true);
        }
        // Contested systems stay compact and legible: a broken amber ring communicates
        // competing control without turning the strategic map into opaque territory fills.
        var contestedLines = new System.Collections.Generic.List<Vector2>();
        foreach (var contested in projection.ContestedSystems)
        {
            var point = ProjectionToScreen(contested.Position, center);
            if (!Visible(point)) continue;
            var radius = Mathf.Max(5f, 9f * UiMapZoom);
            AppendDashedArcLines(contestedLines, point, radius);
        }
        if (contestedLines.Count > 0)
            DrawMultiline(contestedLines.ToArray(), MapAlpha(new Color("e4aa55"), .74f * detail), 1.1f, true);
    }

    // Map rendering may use this cached observer snapshot to fade public-but-unexplored stars.
    private float StrategicUnexploredStarAlpha(int systemId) => _territoryProjection?.UnexploredSystemIds.Contains(systemId) == true ? .28f : 1f;

    private void RefreshTerritoryProjection(int playerId)
    {
        var frame = Engine.GetProcessFrames(); if (ReferenceEquals(_territoryCampaign, _galaxy) && frame < _territoryNextCheckFrame) return;
        // Territory changes on simulation review, not per render tick. Checking the cached
        // runtime once per 120 rendered frames avoids rebuilding diplomacy/projection inputs
        // while panning a large catalogue. Authoritative updates remain on simulation time.
        _territoryNextCheckFrame = frame + 120;
        var claims = _diplomacyRuntime?.BuildView(playerId).Claims ?? Array.Empty<TerritorialClaimSnapshot>();
        var fingerprint = TerritoryFingerprint(playerId, claims);
        if (!ReferenceEquals(_territoryCampaign, _galaxy) || fingerprint != _territoryFingerprint)
        {
            _territoryCampaign = _galaxy;
            _territoryFingerprint = fingerprint;
            var nextProjection = StrategicTerritoryProjection.Build(_galaxy!, playerId, claims, UiCatalogVisualCoordinateScale);
            if (ReferenceEquals(nextProjection, _territoryProjection)) return;
            _territoryProjection = nextProjection;
            DisposeTerritoryFillMeshes();
            var fog = _territoryProjection.FogMask;
            var pixels = new byte[fog.Width * fog.Height * 4];
            for (var index = 0; index < fog.Alpha.Length; index++)
            {
                pixels[index * 4] = pixels[index * 4 + 1] = pixels[index * 4 + 2] = 255;
                pixels[index * 4 + 3] = fog.Alpha[index];
            }
            using (var image = Image.CreateFromData(fog.Width, fog.Height, false, Image.Format.Rgba8, pixels))
                _territoryFogTexture = ImageTexture.CreateFromImage(image);
            foreach (var region in _territoryProjection.Territories)
            {
                const int chunksPerAxis = 12;
                var chunkWidth = Math.Max(fog.Size.X / chunksPerAxis, 1);
                var chunkHeight = Math.Max(fog.Size.Y / chunksPerAxis, 1);
                var builders = new System.Collections.Generic.Dictionary<(int X, int Y), TerritoryFillChunkBuilder>();
                void AddTriangle(System.Numerics.Vector2 first, System.Numerics.Vector2 second, System.Numerics.Vector2 third)
                {
                    var centroid = (first + second + third) / 3;
                    var key = ((int)MathF.Floor((centroid.X - fog.Position.X) / chunkWidth),
                        (int)MathF.Floor((centroid.Y - fog.Position.Y) / chunkHeight));
                    if (!builders.TryGetValue(key, out var builder))
                    {
                        builder = new TerritoryFillChunkBuilder();
                        builders.Add(key, builder);
                    }
                    builder.Add(first, second, third);
                }
                foreach (var polygon in region.FillPolygons)
                    for (var index = 1; index + 1 < polygon.Points.Count; index++)
                        AddTriangle(polygon.Points[0], polygon.Points[index], polygon.Points[index + 1]);
                foreach (var run in region.FillRuns)
                {
                    var topLeft = run.Position;
                    var topRight = run.Position + new System.Numerics.Vector2(run.Size.X, 0);
                    var bottomLeft = run.Position + new System.Numerics.Vector2(0, run.Size.Y);
                    var bottomRight = run.Position + run.Size;
                    AddTriangle(topLeft, topRight, bottomRight);
                    AddTriangle(topLeft, bottomRight, bottomLeft);
                }
                var chunks = new System.Collections.Generic.List<TerritoryFillChunk>(builders.Count);
                foreach (var builder in builders.Values)
                {
                    if (builder.Vertices.Count == 0 || builder.Bounds is not Rect2 bounds) continue;
                    var arrays = new Godot.Collections.Array();
                    arrays.Resize((int)Mesh.ArrayType.Max);
                    arrays[(int)Mesh.ArrayType.Vertex] = builder.Vertices.ToArray();
                    var mesh = new ArrayMesh();
                    mesh.AddSurfaceFromArrays(Mesh.PrimitiveType.Triangles, arrays);
                    chunks.Add(new TerritoryFillChunk(mesh, bounds));
                }
                if (chunks.Count > 0) _territoryFillMeshes[region.CivilizationId] = chunks;
            }
        }
    }
    private void ClearTerritoryProjectionCache()
    {
        DisposeTerritoryFillMeshes();
        _territoryProjection = null;
        _territoryCampaign = null;
        _territoryFingerprint = 0;
        _territoryNextCheckFrame = 0;
        _territoryScreenProjection = null;
        _territoryScreenContours.Clear();
        _territoryScreenBounds.Clear();
    }
    private void DisposeTerritoryFillMeshes()
    {
        _territoryFogTexture?.Dispose();
        _territoryFogTexture = null;
        foreach (var mesh in _territoryFillMeshes.Values.SelectMany(chunks => chunks)) mesh.Mesh.Dispose();
        _territoryFillMeshes.Clear();
    }
    private int TerritoryFingerprint(int playerId, System.Collections.Generic.IReadOnlyList<TerritorialClaimSnapshot> claims)
    {
        var hash = new HashCode(); hash.Add(playerId); hash.Add(UiCatalogVisualCoordinateScale);
        var territory = Game.Simulation.Territory.TerritorialRuntime.Peek(_galaxy!);
        hash.Add(territory?.Revision ?? 0);
        foreach (var item in _galaxy!.Civilizations) { hash.Add(item.Id); hash.Add(_galaxy.Knowledge.IsCivilizationKnown(playerId, item.Id)); }
        foreach (var item in _galaxy.Systems)
        {
            hash.Add(item.Id);
            hash.Add(item.Position.X);
            hash.Add(item.Position.Y);
            hash.Add((int)_galaxy.Knowledge.GetSystemSurveyLevel(playerId, item.Id));
        }
        foreach (var item in claims) { hash.Add(item.ClaimId); hash.Add(item.ClaimantCivilizationId); hash.Add(item.SystemId); hash.Add(item.Active); }
        return hash.ToHashCode();
    }
    private static void AppendDashedArcLines(System.Collections.Generic.List<Vector2> lines, Vector2 point, float radius)
    {
        const int segments = 24;
        const int subdivisions = 3;
        for (var index = 0; index < segments; index += 2)
        {
            var start = Mathf.Tau * index / segments;
            var end = start + Mathf.Tau / segments;
            var previous = point + Vector2.FromAngle(start) * radius;
            for (var step = 1; step <= subdivisions; step++)
            {
                var current = point + Vector2.FromAngle(Mathf.Lerp(start, end, step / (float)subdivisions)) * radius;
                lines.Add(previous); lines.Add(current);
                previous = current;
            }
        }
    }
    private static Vector2 ToGodot(System.Numerics.Vector2 value) => new(value.X, value.Y);
    private bool ProjectionBoundsVisible(Rect2 bounds, Vector2 center, Rect2 viewport)
    {
        var first = ProjectionToScreen(new System.Numerics.Vector2(bounds.Position.X, bounds.Position.Y), center);
        var second = ProjectionToScreen(new System.Numerics.Vector2(bounds.End.X, bounds.End.Y), center);
        var screenBounds = new Rect2(new Vector2(Math.Min(first.X, second.X), Math.Min(first.Y, second.Y)),
            new Vector2(Math.Abs(second.X - first.X), Math.Abs(second.Y - first.Y)));
        return viewport.Intersects(screenBounds);
    }
    private void RefreshTerritoryScreenContours(StrategicTerritoryProjection projection, Vector2 center)
    {
        var origin = ProjectionToScreen(System.Numerics.Vector2.Zero, center);
        var basisX = ProjectionToScreen(System.Numerics.Vector2.UnitX, center);
        var basisY = ProjectionToScreen(System.Numerics.Vector2.UnitY, center);
        if (ReferenceEquals(_territoryScreenProjection, projection) && origin.IsEqualApprox(_territoryScreenOrigin) &&
            basisX.IsEqualApprox(_territoryScreenBasisX) && basisY.IsEqualApprox(_territoryScreenBasisY)) return;
        _territoryScreenProjection = projection;
        _territoryScreenOrigin = origin;
        _territoryScreenBasisX = basisX;
        _territoryScreenBasisY = basisY;
        _territoryScreenContours.Clear();
        _territoryScreenBounds.Clear();
        foreach (var region in projection.Territories)
        {
            var converted = new System.Collections.Generic.List<TerritoryScreenContour>(region.Contours.Count);
            Rect2? bounds = null;
            foreach (var contour in region.Contours)
            {
                if (contour.Count < 3) continue;
                var points = new Vector2[contour.Count + 1];
                Rect2? contourBounds = null;
                for (var index = 0; index < contour.Count; index++)
                {
                    points[index] = ProjectionToScreen(contour[index], center);
                    bounds = bounds is Rect2 existing ? existing.Expand(points[index]) : new Rect2(points[index], Vector2.Zero);
                    contourBounds = contourBounds is Rect2 existingContour
                        ? existingContour.Expand(points[index]) : new Rect2(points[index], Vector2.Zero);
                }
                points[^1] = points[0];
                points = SimplifyScreenContour(points, .55f);
                converted.Add(new TerritoryScreenContour(points, contourBounds!.Value));
            }
            _territoryScreenContours[region.CivilizationId] = converted;
            if (bounds is Rect2 regionBounds) _territoryScreenBounds[region.CivilizationId] = regionBounds;
        }
    }
    private static Vector2[] SimplifyScreenContour(Vector2[] closed, float tolerance)
    {
        if (closed.Length <= 9) return closed;
        var points = closed.Take(closed.Length - 1).ToList();
        var changed = true;
        while (changed && points.Count > 8)
        {
            changed = false;
            var keep = new System.Collections.Generic.List<Vector2>(points.Count);
            for (var index = 0; index < points.Count; index++)
            {
                var previous = points[(index + points.Count - 1) % points.Count];
                var current = points[index];
                var next = points[(index + 1) % points.Count];
                var segment = next - previous;
                var distance = segment.LengthSquared() <= .0001f ? current.DistanceTo(previous) :
                    Math.Abs(segment.Cross(current - previous)) / segment.Length();
                if (distance > tolerance || index % 2 == 0) keep.Add(current);
                else changed = true;
            }
            points = keep;
        }
        points.Add(points[0]);
        return points.ToArray();
    }
    private Vector2 ProjectionToScreen(System.Numerics.Vector2 position, Vector2 center) =>
        _regionalCameraReady && ReferenceEquals(_regionalCameraCampaign, _galaxy)
            ? new(_regionalCamera.ProjectX(position.X), _regionalCamera.ProjectY(position.Y))
            : center + ToGodot(position) * UiMapZoom;
    private static Vector3 ToGodot3(System.Numerics.Vector2 value) => new(value.X, value.Y, 0f);
    private static Color TerritoryColor(int civ, int player) => civ == player ? VisualPalette.Selected : (civ % 6) switch { 0 => VisualPalette.Diplomacy, 1 => VisualPalette.Science, 2 => VisualPalette.Economy, 3 => VisualPalette.Military, 4 => VisualPalette.Success, _ => new Color("d484b8") };
}
