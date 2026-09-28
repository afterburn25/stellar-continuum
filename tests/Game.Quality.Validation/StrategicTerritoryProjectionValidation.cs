using System;
using System.Linq;
using System.Reflection;
using Game.Presentation;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Quality.Validation;

internal static class StrategicTerritoryProjectionValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        VerifyZeroBoundaryVertexIsCanonicalized();
        VerifyNativeFailureSliverIsPreservedForExplicitFan();
        VerifyLocalCatalogVisualScale();
        var galaxy = new GalaxyGenerator().Generate(0x54455252L, new GalaxyGenerationSettings { SystemCount = 48, Radius = 620, PreWarpCivilizationCount = 3, AncientCivilizationCount = 0 });
        var player = galaxy.PlayerCivilizationId; var foreign = galaxy.Civilizations.First(x => x.Id != player);
        Require(player == 0, "projection regression fixture must exercise civilization zero as the human player");
        var home = galaxy.Civilizations.Single(x => x.Id == player).HomeSystemId;
        var writableSystems = (System.Collections.Generic.IList<StarSystemState>)galaxy.Systems;
        var companionIndex = Enumerable.Range(0, writableSystems.Count).First(index => writableSystems[index].Id != home && !galaxy.Civilizations.Any(c => c.HomeSystemId == writableSystems[index].Id));
        var companion = writableSystems[companionIndex] with { Position = writableSystems.Single(x => x.Id == home).Position + new System.Numerics.Vector2(40, 0) };
        writableSystems[companionIndex] = companion;
        galaxy.Colonies.Add(new ColonyState { Id = galaxy.Colonies.Max(x => x.Id) + 1, CivilizationId = player, SystemId = companion.Id, Name = "Projection companion" });
        var claimSystem = galaxy.Systems.First(x => x.Id != foreign.HomeSystemId && x.Id != galaxy.Civilizations.Single(y => y.Id == player).HomeSystemId).Id;
        var claims = new[] { new TerritorialClaimSnapshot(7, foreign.Id, claimSystem, 1, true, new[] { player, foreign.Id }) };
        var hidden = StrategicTerritoryProjection.Build(galaxy, player, claims);
        Require(hidden.Territories.All(x => x.CivilizationId == player) && hidden.Claims.Count == 0, "hidden ownership or claim leaked into projection");
        Require(hidden.UnownedCellCount > 0, "unclaimed cells were treated as civilization zero territory");
        Require(hidden.UnexploredSystemIds.Contains(foreign.HomeSystemId) && hidden.FogRuns.Count > 0, "unexplored space did not retain a fog veil");
        var fog = hidden.FogMask;
        Require(fog.Width <= 176 && fog.Height <= 176 && fog.Alpha.Any(alpha => alpha == 255) &&
                fog.Alpha.Any(alpha => alpha > 0 && alpha < 255), "fog must remain bounded with opaque coverage and a soft frontier");
        Require(Enumerable.Range(0, fog.Width).All(x => fog.Alpha[x] == 0 && fog.Alpha[(fog.Height - 1) * fog.Width + x] == 0) &&
                Enumerable.Range(0, fog.Height).All(y => fog.Alpha[y * fog.Width] == 0 && fog.Alpha[y * fog.Width + fog.Width - 1] == 0),
            "fog veil exposes the outer rectangular texture edge");
        var hiddenColonySystem = galaxy.Systems.First(x => x.Id != home && x.Id != companion.Id && x.Id != foreign.HomeSystemId);
        var hiddenColony = new ColonyState { Id = galaxy.Colonies.Max(x => x.Id) + 1, CivilizationId = foreign.Id, SystemId = hiddenColonySystem.Id, Name = "Hidden projection holding" };
        galaxy.Colonies.Add(hiddenColony);
        Require(Fingerprint(hidden) == Fingerprint(StrategicTerritoryProjection.Build(galaxy, player, claims)), "hidden foreign ownership changed observer territory geometry");
        galaxy.Colonies.Remove(hiddenColony);

        galaxy.Knowledge.RevealCivilization(player, foreign.Id); galaxy.Knowledge.MarkSystemFullySurveyed(player, foreign.HomeSystemId); galaxy.Knowledge.MarkSystemFullySurveyed(player, claimSystem);
        var visible = StrategicTerritoryProjection.Build(galaxy, player, claims); var region = visible.Territories.Single(x => x.CivilizationId == foreign.Id);
        Require(region.Anchors.Any(x => x.SystemId == foreign.HomeSystemId), "surveyed foreign settlement was not projected");
        Require(visible.Claims.Single().SystemId == claimSystem && !region.Anchors.Any(x => x.SystemId == claimSystem), "diplomatic claim was merged into filled ownership");
        Require(visible.Territories.All(x => x.Contours.Count > 0 && HasFill(x)), "territory cells did not create exterior contours");
        Require(visible.Territories.Any(x => x.FillPolygons.Count > 0), "territory boundaries were not converted into smooth fill polygons");
        Require(visible.Territories.SelectMany(x => x.FillPolygons).All(IsRenderablePolygon),
            "territory clipping emitted a duplicate-edge or zero-area polygon that the renderer cannot triangulate");
        Require(visible.Territories.All(AnchorsAreCovered), "an owned anchor fell outside its civilization's territory");
        Require(visible.Territories.All(region => region.Anchors.All(anchor => ContoursContain(region, anchor.Position))),
            "a continuous territory outline did not enclose one of its visible authority anchors");
        Require(visible.Territories.All(region => visible.Territories
                .Where(other => other.CivilizationId != region.CivilizationId)
                .SelectMany(other => other.Anchors)
                .All(anchor => !ContoursContain(region, anchor.Position))),
            "a continuous territory outline enclosed a rival authority anchor");
        var hiddenInvader = galaxy.Civilizations.First(civilization => civilization.Id != player && civilization.Id != foreign.Id);
        var foreignHomeColony = galaxy.Colonies.First(colony => colony.SystemId == foreign.HomeSystemId);
        galaxy.Colonies.Remove(foreignHomeColony);
        var hiddenOccupation = new ColonyState { Id = foreignHomeColony.Id, CivilizationId = hiddenInvader.Id, SystemId = foreign.HomeSystemId, Name = "Unobserved occupation" };
        galaxy.Colonies.Add(hiddenOccupation);
        Require(Fingerprint(visible) == Fingerprint(StrategicTerritoryProjection.Build(galaxy, player, claims)), "an unseen occupier changed visible foreign territory geometry");
        galaxy.Colonies.Remove(hiddenOccupation);
        galaxy.Colonies.Add(foreignHomeColony);
        var own = visible.Territories.Single(x => x.CivilizationId == player);
        var midpoint = (writableSystems.Single(x => x.Id == home).Position + companion.Position) * .5f;
        Require(own.Anchors.Any(x => x.SystemId == companion.Id) && Contains(own, midpoint), "nearby same-owner settlements did not union into an exterior territory patch");
        Require(Contains(own, own.LabelPosition), "disconnected territory label was not placed inside an owned patch");
        Require(NoOverlappingArea(visible), "opposing owners overlap in territory geometry");
        Require(visible.FogRuns.Count > 0 && visible.FogContours.Count > 0, "overview-capable fog geometry was not built");
        var repeat = StrategicTerritoryProjection.Build(galaxy, player, claims);
        Require(Fingerprint(visible) == Fingerprint(repeat), "territory geometry changed without a state change");
        writableSystems[companionIndex] = companion with { Position = new System.Numerics.Vector2(50_000, 50_000) };
        var largeSparse = StrategicTerritoryProjection.Build(galaxy, player);
        Require(largeSparse.GridCellCount <= 25_600, "large sparse campaign exceeded the bounded projection grid");
        Require(largeSparse.Territories.Select(region => region.CivilizationId).OrderBy(id => id).SequenceEqual(new[] { player, foreign.Id }.OrderBy(id => id)), "large sparse campaign dropped a visible owner");
        Require(largeSparse.Territories.All(x => HasFill(x) && LabelIsCovered(x)), "large sparse campaign lost an owner or placed a label outside territory");
        Require(largeSparse.Territories.Single(x => x.CivilizationId == player).Contours.Count >= 2, "distant holdings were incorrectly joined into one territory patch");
        Require(NoOverlappingArea(largeSparse), "large sparse opposing territories overlap");
        for (var index = 0; index < writableSystems.Count; index++) if (index != companionIndex && !galaxy.Colonies.Any(colony => colony.SystemId == writableSystems[index].Id)) galaxy.Colonies.Add(new ColonyState { Id = galaxy.Colonies.Max(colony => colony.Id) + 1, CivilizationId = player, SystemId = writableSystems[index].Id, Name = "Dense projection holding" });
        var largeDense = StrategicTerritoryProjection.Build(galaxy, player);
        Require(largeDense.GridCellCount <= 25_600 && largeDense.Territories.Single(x => x.CivilizationId == player).Anchors.Count >= 20, "large dense campaign exceeded the bounded projection grid");
        Require(largeDense.Territories.SelectMany(x => x.FillPolygons).All(IsRenderablePolygon),
            "dense territory clipping emitted a polygon that the renderer cannot triangulate");
        Console.WriteLine("PASS: observer-safe territory uses contiguous exterior cells, separate claims, clipped opponents, and exploration fog");
    }
    private static void VerifyLocalCatalogVisualScale()
    {
        const float coordinateScale = 14f;
        var galaxy = new GalaxyGenerator().Generate(0x4C4F_4341_4CL,
            GalaxyGenerationMetadata.MilkyWay500("territory-local-scale", 0x4C4F_4341_4CL).ToSettings());
        var originalPositions = galaxy.Systems.Select(system => system.Position).ToArray();
        var local = StrategicTerritoryProjection.Build(galaxy, galaxy.PlayerCivilizationId, coordinateScale: coordinateScale);
        Require(galaxy.Systems.Select(system => system.Position).SequenceEqual(originalPositions),
            "presentation territory scaling mutated authoritative nearby-star positions");
        var scaled = new GalaxyState
        {
            Seed = galaxy.Seed,
            Systems = galaxy.Systems.Select(system => system with { Position = system.Position * coordinateScale }).ToArray(),
            PlanetaryBodies = galaxy.PlanetaryBodies,
            Civilizations = galaxy.Civilizations,
            Fleets = galaxy.Fleets,
            Colonies = galaxy.Colonies,
            Economies = galaxy.Economies,
            Technologies = galaxy.Technologies,
            ConstructionStates = galaxy.ConstructionStates,
            ShipyardStates = galaxy.ShipyardStates,
            PlayerCivilizationId = galaxy.PlayerCivilizationId,
            Knowledge = galaxy.Knowledge,
        };
        var explicitVisualGeometry = StrategicTerritoryProjection.Build(scaled, galaxy.PlayerCivilizationId);
        Require(Fingerprint(local) == Fingerprint(explicitVisualGeometry),
            "local territory projection diverged from the same catalogue expressed in visual coordinates");
        Require(local.GridCellCount > 0 && local.GridCellCount <= 25_600 && GeometryIsFinite(local),
            "local territory projection produced unbounded or non-finite visual geometry");
    }
    private static bool GeometryIsFinite(StrategicTerritoryProjection projection) =>
        projection.Territories.SelectMany(region => region.FillRuns.SelectMany(run => new[] { run.Position, run.Size })
                .Concat(region.FillPolygons.SelectMany(polygon => polygon.Points))
                .Concat(region.Contours.SelectMany(contour => contour)))
            .Concat(projection.FogRuns.SelectMany(run => new[] { run.Position, run.Size }))
            .Concat(projection.FogContours.SelectMany(contour => contour))
            .Concat(projection.Claims.Select(claim => claim.Position))
            .All(point => float.IsFinite(point.X) && float.IsFinite(point.Y));
    private static void VerifyZeroBoundaryVertexIsCanonicalized()
    {
        var clip = typeof(StrategicTerritoryProjection).GetMethod(
            "ClipPositiveTriangle", BindingFlags.Static | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException("Territory clipping helper was not found.");
        var result = (System.Collections.Generic.IReadOnlyList<System.Numerics.Vector2>?)clip.Invoke(null, new object[]
        {
            new[] { new System.Numerics.Vector2(0, 0), new(12, 0), new(6, 6) },
            new[] { 1f, 0f, 1f },
        }) ?? throw new InvalidOperationException("Territory clipping returned no result.");
        Require(IsRenderablePolygon(new StrategicTerritoryFillPolygon(result)),
            "a zero-valued boundary vertex was duplicated and produced a polygon Godot cannot triangulate");
    }
    private static void VerifyNativeFailureSliverIsPreservedForExplicitFan()
    {
        var normalize = typeof(StrategicTerritoryProjection).GetMethod(
            "NormalizeFillPolygon", BindingFlags.Static | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException("Territory polygon normalization helper was not found.");
        var result = (System.Collections.Generic.IReadOnlyList<System.Numerics.Vector2>?)normalize.Invoke(null, new object[]
        {
            new[]
            {
                new System.Numerics.Vector2(-18.147076f, -55.087666f),
                new(-18.150757f, -55.083984f),
                new(-18.15281f, -55.086037f),
            },
        }) ?? throw new InvalidOperationException("Territory normalization returned no result.");
        Require(result.Count == 3 && IsRenderablePolygon(new StrategicTerritoryFillPolygon(result)),
            "the native Player-campaign tangent sliver was discarded instead of being retained for explicit triangle rendering");
    }
    private static bool IsRenderablePolygon(StrategicTerritoryFillPolygon polygon)
    {
        if (polygon.Points.Count < 3 || polygon.Points.Any(point => !float.IsFinite(point.X) || !float.IsFinite(point.Y))) return false;
        double twiceArea = 0;
        for (var index = 0; index < polygon.Points.Count; index++)
        {
            var point = polygon.Points[index];
            var next = polygon.Points[(index + 1) % polygon.Points.Count];
            if (System.Numerics.Vector2.DistanceSquared(point, next) <= .00000001f) return false;
            twiceArea += (double)point.X * next.Y - (double)next.X * point.Y;
        }
        return Math.Abs(twiceArea) > .000001;
    }
    private static bool HasFill(StrategicTerritoryRegion region) => region.FillRuns.Count > 0 || region.FillPolygons.Count > 0;
    private static bool AnchorsAreCovered(StrategicTerritoryRegion region) => region.Anchors.All(anchor => Contains(region, anchor.Position));
    private static bool LabelIsCovered(StrategicTerritoryRegion region) => Contains(region, region.LabelPosition);
    private static bool Contains(StrategicTerritoryRegion region, System.Numerics.Vector2 point) =>
        region.FillRuns.Any(run => Contains(run, point)) || region.FillPolygons.Any(polygon => Contains(polygon, point));
    private static bool Contains(StrategicTerritoryFillRun run, System.Numerics.Vector2 point) => point.X >= run.Position.X && point.X <= run.Position.X + run.Size.X && point.Y >= run.Position.Y && point.Y <= run.Position.Y + run.Size.Y;
    private static bool Contains(StrategicTerritoryFillPolygon polygon, System.Numerics.Vector2 point)
    {
        var inside = false;
        for (var current = 0; current < polygon.Points.Count; current++)
        {
            var previous = (current + polygon.Points.Count - 1) % polygon.Points.Count;
            var a = polygon.Points[current]; var b = polygon.Points[previous];
            if ((a.Y > point.Y) != (b.Y > point.Y)
                && point.X < (b.X - a.X) * (point.Y - a.Y) / (b.Y - a.Y) + a.X) inside = !inside;
        }
        return inside;
    }
    private static bool ContoursContain(StrategicTerritoryRegion region, System.Numerics.Vector2 point)
    {
        var inside = false;
        foreach (var contour in region.Contours)
            if (Contains(new StrategicTerritoryFillPolygon(contour), point)) inside = !inside;
        return inside;
    }
    private static bool NoOverlappingArea(StrategicTerritoryProjection projection)
    {
        var runs = projection.Territories.SelectMany(region => region.FillRuns.Select(run => (region.CivilizationId, Run: run))).ToArray();
        for (var left = 0; left < runs.Length; left++)
        for (var right = left + 1; right < runs.Length; right++)
        {
            if (runs[left].CivilizationId == runs[right].CivilizationId) continue;
            var a = runs[left].Run;
            var b = runs[right].Run;
            if (Math.Min(a.Position.X + a.Size.X, b.Position.X + b.Size.X) > Math.Max(a.Position.X, b.Position.X)
                && Math.Min(a.Position.Y + a.Size.Y, b.Position.Y + b.Size.Y) > Math.Max(a.Position.Y, b.Position.Y)) return false;
        }
        var polygons = projection.Territories.SelectMany(region => region.FillPolygons.Select(polygon => (region.CivilizationId, Polygon: polygon))).ToArray();
        foreach (var polygon in polygons)
        {
            var center = polygon.Polygon.Points.Aggregate(System.Numerics.Vector2.Zero, (sum, point) => sum + point) / polygon.Polygon.Points.Count;
            if (projection.Territories.Any(region => region.CivilizationId != polygon.CivilizationId && Contains(region, center))) return false;
        }
        return true;
    }
    private static string Fingerprint(StrategicTerritoryProjection projection)
    {
        static string Point(System.Numerics.Vector2 point) => $"{point.X:R},{point.Y:R}";
        return string.Join("|",
            projection.Territories.SelectMany(region =>
                region.FillRuns.Select(run => $"T{region.CivilizationId}:{Point(run.Position)}:{Point(run.Size)}")
                    .Concat(region.FillPolygons.SelectMany(polygon => polygon.Points.Select(point => $"P{region.CivilizationId}:{Point(point)}")))
                    .Concat(region.Contours.SelectMany(contour => contour.Select(point => $"B{region.CivilizationId}:{Point(point)}"))))
                .Concat(projection.Claims.Select(claim => $"C{claim.CivilizationId}:{claim.SystemId}:{Point(claim.Position)}:{claim.Radius:R}"))
                .Concat(projection.FogRuns.Select(run => $"F:{Point(run.Position)}:{Point(run.Size)}"))
                .Concat(projection.FogContours.SelectMany(contour => contour.Select(point => $"E:{Point(point)}"))));
    }
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
}
