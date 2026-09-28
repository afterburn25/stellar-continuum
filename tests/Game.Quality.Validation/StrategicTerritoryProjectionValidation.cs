using System;
using System.Linq;
using System.Reflection;
using System.Diagnostics;
using Game.Presentation;
using Game.Simulation.Diplomacy;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Territory;

namespace Game.Quality.Validation;

internal static class StrategicTerritoryProjectionValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        VerifyZeroBoundaryVertexIsCanonicalized();
        VerifyNativeFailureSliverIsPreservedForExplicitFan();
        VerifyLocalCatalogVisualScale();
        VerifyOwnSourcesRefreshBesideHiddenRivals();
        VerifyPhysicalConnectedGeometryAndEmptyControlAnchors();
        VerifyOptimizedFogMatchesBruteForce();
        ProfileFullGalaxyProjectionCache();
        var galaxy = new GalaxyGenerator().Generate(0x54455252L, new GalaxyGenerationSettings { SystemCount = 48, Radius = 620, PreWarpCivilizationCount = 3, AncientCivilizationCount = 0 });
        var player = galaxy.PlayerCivilizationId; var foreign = galaxy.Civilizations.First(x => x.Id != player);
        Require(player == 0, "projection regression fixture must exercise civilization zero as the human player");
        var home = galaxy.Civilizations.Single(x => x.Id == player).HomeSystemId;
        var writableSystems = (System.Collections.Generic.IList<StarSystemState>)galaxy.Systems;
        var companionIndex = Enumerable.Range(0, writableSystems.Count).First(index => writableSystems[index].Id != home && !galaxy.Civilizations.Any(c => c.HomeSystemId == writableSystems[index].Id));
        var companion = writableSystems[companionIndex] with { Position = writableSystems.Single(x => x.Id == home).Position + new System.Numerics.Vector2(40, 0) };
        writableSystems[companionIndex] = companion;
        galaxy.Colonies.Add(new ColonyState { Id = galaxy.Colonies.Max(x => x.Id) + 1, CivilizationId = player, SystemId = companion.Id, Name = "Projection companion", PopulationMillions = 400, Infrastructure = 3, Stability = 1 });
        var territorial = TerritorialRuntime.Initialize(galaxy); territorial.Recompute(galaxy);
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
        territorial.Recompute(galaxy);
        Require(Fingerprint(hidden) == Fingerprint(StrategicTerritoryProjection.Build(galaxy, player, claims)), "hidden foreign ownership changed observer territory geometry");
        galaxy.Colonies.Remove(hiddenColony); territorial.Recompute(galaxy);

        galaxy.Knowledge.RevealCivilization(player, foreign.Id); galaxy.Knowledge.MarkSystemFullySurveyed(player, foreign.HomeSystemId); galaxy.Knowledge.MarkSystemFullySurveyed(player, claimSystem);
        var visible = StrategicTerritoryProjection.Build(galaxy, player, claims); var region = visible.Territories.Single(x => x.CivilizationId == foreign.Id);
        Require(region.Anchors.Any(x => x.SystemId == foreign.HomeSystemId), "surveyed foreign settlement was not projected");
        Require(galaxy.Territory!.Observations.Any(item => item.ObserverCivilizationId == player && item.SystemId == foreign.HomeSystemId && item.CivilizationId == foreign.Id),
            "visible foreign control was not retained as observer memory before later fog changes");
        Require(visible.Claims.Single().SystemId == claimSystem && !region.Anchors.Any(x => x.SystemId == claimSystem), "diplomatic claim was merged into filled ownership");
        Require(visible.Territories.All(x => x.Contours.Count > 0 && HasFill(x)), "territory cells did not create exterior contours");
        Require(visible.Territories.SelectMany(x => x.Contours).All(contour => contour.Count >= 3),
            "runtime-backed territory boundaries omitted renderable exterior contours");
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
        galaxy.Colonies.Add(hiddenOccupation); territorial.Recompute(galaxy);
        var afterOccupation = StrategicTerritoryProjection.Build(galaxy, player, claims);
        Require(Fingerprint(visible) == Fingerprint(afterOccupation), "an unseen occupier changed visible foreign territory geometry: before=" +
            string.Join(',', visible.Territories.SelectMany(region => region.Anchors).Select(anchor => $"{anchor.CivilizationId}/{anchor.SystemId}")) + " after=" +
            string.Join(',', afterOccupation.Territories.SelectMany(region => region.Anchors).Select(anchor => $"{anchor.CivilizationId}/{anchor.SystemId}")));
        galaxy.Colonies.Remove(hiddenOccupation); galaxy.Colonies.Add(foreignHomeColony); territorial.Recompute(galaxy);
        var own = visible.Territories.Single(x => x.CivilizationId == player);
        var midpoint = (writableSystems.Single(x => x.Id == home).Position + companion.Position) * .5f;
        Require(own.Anchors.Any(x => x.SystemId == companion.Id) && Contains(own, midpoint), "nearby same-owner settlements did not union into an exterior territory patch");
        Require(Contains(own, own.LabelPosition), "disconnected territory label was not placed inside an owned patch");
        Require(NoOverlappingArea(visible), "opposing owners overlap in territory geometry");
        Require(visible.FogRuns.Count > 0 && visible.FogContours.Count > 0, "overview-capable fog geometry was not built");
        var repeat = StrategicTerritoryProjection.Build(galaxy, player, claims);
        Require(Fingerprint(visible) == Fingerprint(repeat), "territory geometry changed without a state change");
        writableSystems[companionIndex] = companion with { Position = new System.Numerics.Vector2(50_000, 50_000) };
        territorial.Recompute(galaxy);
        var largeSparse = StrategicTerritoryProjection.Build(galaxy, player);
        Require(largeSparse.GridCellCount <= 25_600, "large sparse campaign exceeded the bounded projection grid");
        Require(largeSparse.Territories.Select(region => region.CivilizationId).OrderBy(id => id).SequenceEqual(new[] { player, foreign.Id }.OrderBy(id => id)), "large sparse campaign dropped a visible owner");
        Require(largeSparse.Territories.All(x => HasFill(x) && LabelIsCovered(x)), "large sparse campaign lost an owner or placed a label outside territory");
        Require(largeSparse.Territories.Single(x => x.CivilizationId == player).Contours.Count >= 2, "distant holdings were incorrectly joined into one territory patch");
        Require(NoOverlappingArea(largeSparse), "large sparse opposing territories overlap: " + OverlapDescription(largeSparse));
        for (var index = 0; index < writableSystems.Count; index++) if (index != companionIndex && !galaxy.Colonies.Any(colony => colony.SystemId == writableSystems[index].Id)) galaxy.Colonies.Add(new ColonyState { Id = galaxy.Colonies.Max(colony => colony.Id) + 1, CivilizationId = player, SystemId = writableSystems[index].Id, Name = "Dense projection holding", PopulationMillions = 400, Infrastructure = 3, Stability = 1 });
        territorial.Recompute(galaxy);
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
        TerritorialRuntime.Initialize(galaxy);
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
        TerritorialRuntime.Initialize(scaled);
        var explicitVisualGeometry = StrategicTerritoryProjection.Build(scaled, galaxy.PlayerCivilizationId);
        // Physical influence is deliberately recalculated in physical light years for a cloned
        // catalogue. Presentation scaling must only be checked against the original runtime
        // snapshot, where it cannot change authority or mutate star positions.
        Require(local.Territories.Count > 0 && explicitVisualGeometry.Territories.Count > 0 && GeometryIsFinite(explicitVisualGeometry),
            "a represented initialized territory snapshot disappeared when projected at local catalogue scale");
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
        const float epsilon = .001f; // Adjacent float grid runs can differ by a few ULPs at 50k coordinates.
        var runs = projection.Territories.SelectMany(region => region.FillRuns.Select(run => (region.CivilizationId, Run: run))).ToArray();
        for (var left = 0; left < runs.Length; left++)
        for (var right = left + 1; right < runs.Length; right++)
        {
            if (runs[left].CivilizationId == runs[right].CivilizationId) continue;
            var a = runs[left].Run;
            var b = runs[right].Run;
            if (Math.Min(a.Position.X + a.Size.X, b.Position.X + b.Size.X) > Math.Max(a.Position.X, b.Position.X) + epsilon
                && Math.Min(a.Position.Y + a.Size.Y, b.Position.Y + b.Size.Y) > Math.Max(a.Position.Y, b.Position.Y) + epsilon) return false;
        }
        var polygons = projection.Territories.SelectMany(region => region.FillPolygons.Select(polygon => (region.CivilizationId, Polygon: polygon))).ToArray();
        foreach (var polygon in polygons)
        {
            var center = polygon.Polygon.Points.Aggregate(System.Numerics.Vector2.Zero, (sum, point) => sum + point) / polygon.Polygon.Points.Count;
            if (projection.Territories.Any(region => region.CivilizationId != polygon.CivilizationId && Contains(region, center))) return false;
        }
        return true;
    }
    private static void VerifyOwnSourcesRefreshBesideHiddenRivals()
    {
        var galaxy = new GalaxyGenerator().Generate(0x4F57_4E52L, new GalaxyGenerationSettings { SystemCount = 32, Radius = 420, PreWarpCivilizationCount = 3, AncientCivilizationCount = 0 });
        var player = galaxy.PlayerCivilizationId;
        var occupied = galaxy.Colonies.Select(colony => colony.SystemId).ToHashSet();
        var target = galaxy.Systems.First(system => !occupied.Contains(system.Id));
        var runtime = TerritorialRuntime.Initialize(galaxy);
        _ = StrategicTerritoryProjection.Build(galaxy, player); // establishes any foreign last-known reports.
        var source = new ColonyState { Id = galaxy.Colonies.Max(colony => colony.Id) + 1, CivilizationId = player, SystemId = target.Id, Name = "Observed own expansion", PopulationMillions = 400, Infrastructure = 3, Stability = 1 };
        galaxy.Colonies.Add(source); runtime.Recompute(galaxy);
        Require(StrategicTerritoryProjection.Build(galaxy, player).Territories.Single(region => region.CivilizationId == player).Anchors.Any(anchor => anchor.SystemId == target.Id),
            "a represented player colony did not appear while an unrelated rival remained hidden");
        galaxy.Colonies.Remove(source); runtime.Recompute(galaxy);
        Require(StrategicTerritoryProjection.Build(galaxy, player).Territories.Single(region => region.CivilizationId == player).Anchors.All(anchor => anchor.SystemId != target.Id),
            "destroyed player territorial source remained frozen by unrelated hidden rival state");
    }
    private static void VerifyPhysicalConnectedGeometryAndEmptyControlAnchors()
    {
        const float coordinateScale = 14f;
        var galaxy = new GalaxyGenerator().Generate(0x5048_5953L,
            GalaxyGenerationMetadata.FullGalaxy500("physical-projection-regression", 0x5048_5953L, systemCount: 500).ToSettings());
        var owner = galaxy.PlayerCivilizationId; var rival = galaxy.Civilizations.First(civilization => civilization.Id != owner).Id;
        var systems = (System.Collections.Generic.IList<StarSystemState>)galaxy.Systems;
        for (var index = 0; index < systems.Count; index++)
            systems[index] = systems[index] with { GalacticDepthLightYears = 0 };
        var holdings = Enumerable.Range(0, 6).Select(index => systems[index].Id).ToArray();
        for (var index = 0; index < holdings.Length; index++)
            systems[index] = systems[index] with { Position = new System.Numerics.Vector2(20 + index * 6.5f, 30 + index * 4.5f) };
        systems[6] = systems[6] with { Position = new System.Numerics.Vector2(125, -110) };
        systems[7] = systems[7] with { Position = new System.Numerics.Vector2(65, 62) };
        galaxy.Colonies.Clear();
        foreach (var holding in holdings) galaxy.Colonies.Add(Colony(galaxy, owner, holding));
        galaxy.Colonies.Add(Colony(galaxy, rival, systems[6].Id));
        galaxy.Knowledge.MarkSystemFullySurveyed(owner, systems[6].Id);
        var runtime = TerritorialRuntime.Initialize(galaxy); runtime.Recompute(galaxy);
        var projection = StrategicTerritoryProjection.Build(galaxy, owner, coordinateScale: coordinateScale); var region = projection.Territories.Single(item => item.CivilizationId == owner);
        for (var index = 0; index < holdings.Length - 1; index++)
        {
            var left = systems[index].Position * coordinateScale; var right = systems[index + 1].Position * coordinateScale;
            Require(Contains(region, (left + right) * .5f), $"physical full-galaxy lane midpoint {index} was not continuously covered");
        }
        Require(region.FillPolygons.Any(polygon => polygon.Points.Zip(polygon.Points.Skip(1).Append(polygon.Points[0]), (a, b) => a - b).Any(delta => Math.Abs(delta.X) > .001f && Math.Abs(delta.Y) > .001f)),
            "physical connected holdings did not produce an interpolated diagonal boundary");
        Require(!ContoursContain(region, systems[6].Position * coordinateScale), "smooth same-owner field swallowed a distant rival anchor");
        var empty = systems[7].Id;
        Require(!galaxy.Colonies.Any(colony => colony.SystemId == empty), "empty-control fixture accidentally founded a colony");
        galaxy.Knowledge.MarkSystemFullySurveyed(owner, empty);
        AddComplete(galaxy, owner, empty, TerritorialInstallationKind.Relay); AddComplete(galaxy, owner, empty, TerritorialInstallationKind.Administration); runtime.Recompute(galaxy);
        var controlled = StrategicTerritoryProjection.Build(galaxy, owner, coordinateScale: coordinateScale).Territories.Single(item => item.CivilizationId == owner);
        Require(controlled.Anchors.Any(anchor => anchor.SystemId == empty), "controlled uninhabited regional installation system was omitted from player territory anchors");
    }
    private static void VerifyOptimizedFogMatchesBruteForce()
    {
        foreach (var (count, scale) in new[] { (12, 1f), (500, 14f) })
        {
            var settings = count == 500
                ? GalaxyGenerationMetadata.FullGalaxy500("fog-equivalence", 0x464F_475F_0000L + count, systemCount: count).ToSettings()
                : new GalaxyGenerationSettings { SystemCount = count, Radius = 190, PreWarpCivilizationCount = 2, AncientCivilizationCount = 0 };
            var galaxy = new GalaxyGenerator().Generate(0x464F_475F_0000L + count, settings);
            var observer = galaxy.PlayerCivilizationId;
            foreach (var system in galaxy.Systems.Where(system => system.Id % 3 == 0))
                galaxy.Knowledge.MarkSystemFullySurveyed(observer, system.Id);
            TerritorialRuntime.Initialize(galaxy);
            var projection = StrategicTerritoryProjection.Build(galaxy, observer, coordinateScale: scale);
            Require(projection.FogMask.Alpha.SequenceEqual(BruteForceFogAlpha(galaxy, observer, projection.FogMask, scale)),
                $"optimized nearest-system fog differed from the original brute-force mask for {count} systems");
        }
    }
    private static byte[] BruteForceFogAlpha(GalaxyState galaxy, int observer, StrategicFogMask mask, float coordinateScale)
    {
        const int padding = 8;
        var cell = mask.Size.X / mask.Width; var width = mask.Width - padding * 2; var height = mask.Height - padding * 2;
        var source = new float[mask.Width * mask.Height];
        for (var y = 0; y < height; y++) for (var x = 0; x < width; x++)
        {
            var point = mask.Position + new System.Numerics.Vector2((padding + x + .5f) * cell, (padding + y + .5f) * cell);
            var best = 0; var distance = float.PositiveInfinity;
            for (var index = 0; index < galaxy.Systems.Count; index++)
            {
                var candidate = System.Numerics.Vector2.DistanceSquared(point, galaxy.Systems[index].Position * coordinateScale);
                if (candidate < distance) { distance = candidate; best = index; }
            }
            source[(y + padding) * mask.Width + x + padding] = galaxy.Knowledge.IsSystemKnown(observer, galaxy.Systems[best].Id) ? 0 : 1;
        }
        var horizontal = new float[source.Length]; var alpha = new byte[source.Length]; int[] weights = [1, 6, 15, 20, 15, 6, 1];
        for (var y = 3; y < mask.Height - 3; y++) for (var x = 3; x < mask.Width - 3; x++)
        {
            float value = 0; for (var offset = -3; offset <= 3; offset++) value += source[y * mask.Width + x + offset] * weights[offset + 3];
            horizontal[y * mask.Width + x] = value / 64f;
        }
        for (var y = 3; y < mask.Height - 3; y++) for (var x = 3; x < mask.Width - 3; x++)
        {
            float value = 0; for (var offset = -3; offset <= 3; offset++) value += horizontal[(y + offset) * mask.Width + x] * weights[offset + 3];
            alpha[y * mask.Width + x] = (byte)Math.Clamp(MathF.Round(value * 255f / 64f), 0, 255);
        }
        return alpha;
    }
    private static void ProfileFullGalaxyProjectionCache()
    {
        foreach (var count in new[] { 500, 1000, 2500 })
        {
            var galaxy = new GalaxyGenerator().Generate(0x5052_4F4A_0000L + count,
                GalaxyGenerationMetadata.FullGalaxy500("projection-profile", 0x5052_4F4A_0000L + count, systemCount: count).ToSettings());
            TerritorialRuntime.Initialize(galaxy); var player = galaxy.PlayerCivilizationId; var watch = Stopwatch.StartNew();
            var normal = StrategicTerritoryProjection.Build(galaxy, player, coordinateScale: 14); watch.Stop(); var normalMs = watch.Elapsed.TotalMilliseconds;
            watch.Restart(); var cached = StrategicTerritoryProjection.Build(galaxy, player, coordinateScale: 14); watch.Stop(); var cachedMs = watch.Elapsed.TotalMilliseconds;
            foreach (var civilization in galaxy.Civilizations) galaxy.Knowledge.RevealCivilization(player, civilization.Id);
            foreach (var system in galaxy.Systems) galaxy.Knowledge.MarkSystemFullySurveyed(player, system.Id);
            watch.Restart(); var revealed = StrategicTerritoryProjection.Build(galaxy, player, coordinateScale: 14); watch.Stop();
            Require(ReferenceEquals(normal, cached) && normal.GridCellCount <= 25_600 && revealed.GridCellCount <= 25_600 && GeometryIsFinite(revealed),
                $"full-galaxy {count} projection cache or bounded geometry contract failed");
            Console.WriteLine($"BENCH projection full_galaxy systems={count} normal_ms={normalMs:0.###} cached_ms={cachedMs:0.###} all_revealed_ms={watch.Elapsed.TotalMilliseconds:0.###} normal_regions={normal.Territories.Count} revealed_regions={revealed.Territories.Count}");
        }
    }
    private static void AddComplete(GalaxyState galaxy, int owner, int system, TerritorialInstallationKind kind)
    {
        galaxy.Territory ??= new TerritorialState(); var definition = TerritorialBalance.Definition(kind);
        galaxy.Territory.Installations.Add(new TerritorialInstallation { Id = galaxy.Territory.Installations.Count + 1, CivilizationId = owner, SystemId = system, Kind = kind, RequiredDays = definition.Days, CompletedDays = definition.Days, PaidCredits = definition.Credits, PaidIndustry = definition.Industry });
    }
    private static ColonyState Colony(GalaxyState galaxy, int owner, int system) => new()
    {
        Id = galaxy.Colonies.Select(colony => colony.Id).DefaultIfEmpty(0).Max() + 1,
        CivilizationId = owner, SystemId = system, Name = $"Projection {owner}-{system}", PopulationMillions = 600, Infrastructure = 4, Stability = 1, SurfaceHubLevel = 3,
    };
    private static string OverlapDescription(StrategicTerritoryProjection projection)
    {
        var runs = projection.Territories.SelectMany(region => region.FillRuns.Select(run => (region.CivilizationId, Run: run))).ToArray();
        for (var left = 0; left < runs.Length; left++) for (var right = left + 1; right < runs.Length; right++)
        {
            if (runs[left].CivilizationId == runs[right].CivilizationId) continue;
            var a = runs[left].Run; var b = runs[right].Run;
            if (Math.Min(a.Position.X + a.Size.X, b.Position.X + b.Size.X) > Math.Max(a.Position.X, b.Position.X) &&
                Math.Min(a.Position.Y + a.Size.Y, b.Position.Y + b.Size.Y) > Math.Max(a.Position.Y, b.Position.Y))
                return $"{runs[left].CivilizationId} {a.Position}/{a.Size} versus {runs[right].CivilizationId} {b.Position}/{b.Size}";
        }
        return "polygon overlap";
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
