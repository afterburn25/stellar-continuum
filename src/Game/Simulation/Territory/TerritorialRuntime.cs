using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using Game.Simulation.Models;
using Game.Simulation.Exploration;
using Game.Simulation.Economy;
using Game.Simulation.Diplomacy;

namespace Game.Simulation.Territory;

/// <summary>Scheduled authoritative derivation. Read/Peek never recompute: cameras cannot advance territory.</summary>
public sealed class TerritorialRuntime
{
    private static readonly ConditionalWeakTable<GalaxyState, TerritorialRuntime> Runtimes = new();
    private readonly Dictionary<int, SystemTerritory> _systems = new();
    private readonly Dictionary<(int Civilization, int System), CivilizationSystemTerritory> _scores = new();
    private int _sourceSignature;
    internal DiplomacyState? Diplomacy { get; set; }
    public long Revision { get; private set; }
    public long RecomputeCount { get; private set; }
    public double LastCalculationMilliseconds { get; private set; }
    public IReadOnlyDictionary<int, SystemTerritory> Systems => _systems;
    public static TerritorialRuntime? Peek(GalaxyState galaxy) => Runtimes.TryGetValue(galaxy, out var runtime) ? runtime : null;
    public static TerritorialRuntime Initialize(GalaxyState galaxy)
    {
        var runtime = Runtimes.GetValue(galaxy, _ => new TerritorialRuntime());
        if (galaxy.Territory is null)
        {
            galaxy.Territory = new TerritorialState();
            // Existing save-game expeditions already paid the old base authorization.
            // Preserve that capital and completion time; retargeting only pays a surcharge.
            foreach (var fleet in galaxy.Fleets.Where(f => f.IsActive && !f.PreventAutomaticSettlement &&
                f.Role == FleetRole.Colony && (f.DestinationPlanetaryBodyId.HasValue || f.SettlementBodyId.HasValue)))
            {
                var body = fleet.DestinationPlanetaryBodyId ?? fleet.SettlementBodyId;
                if (!galaxy.PlanetaryBodies.Any(b => b.Id == body)) continue;
                var outpost = Game.Simulation.Colonization.ResourceOutpostOpportunityPlanner.IsOutpostFleet(fleet);
                galaxy.Territory.Expeditions.Add(new(fleet.Id, body!.Value,
                    outpost ? Game.Simulation.Colonization.ColonizationSimulation.ResourceOutpostExpeditionCreditCost : Game.Simulation.Colonization.ColonizationSimulation.ColonyExpeditionCreditCost,
                    Game.Simulation.Colonization.ColonizationSimulation.EstablishmentDays(fleet)));
            }
        }
        if (runtime.Revision == 0)
        {
            runtime.Recompute(galaxy);
            if (galaxy.Territory.NextReviewDay <= galaxy.Territory.ElapsedDays)
                galaxy.Territory.NextReviewDay = galaxy.Territory.ElapsedDays + TerritorialBalance.ReviewDays;
        }
        return runtime;
    }
    public CivilizationSystemTerritory? Read(int civilizationId, int systemId) => _scores.GetValueOrDefault((civilizationId, systemId));

    public void Advance(GalaxyState galaxy, double days)
    {
        if (!double.IsFinite(days) || days < 0) throw new ArgumentOutOfRangeException(nameof(days));
        if (days == 0) return;
        var state = galaxy.Territory!;
        TerritorialConstruction.Advance(galaxy, days);
        state.ElapsedDays += days;
        var signature = SourceSignature(galaxy);
        if (signature != _sourceSignature || state.ElapsedDays >= state.NextReviewDay)
        {
            Recompute(galaxy);
            state.NextReviewDay = state.ElapsedDays + TerritorialBalance.ReviewDays;
        }
    }

    public void Recompute(GalaxyState galaxy)
    {
        var started = Stopwatch.GetTimestamp();
        var positions = galaxy.Systems.ToDictionary(s => s.Id);
        var colonyLocations = galaxy.Colonies.Where(c => c.Kind == SettlementKind.Colony)
            .Select(c => (c.CivilizationId, c.SystemId)).ToHashSet();
        var sources = BuildSources(galaxy, positions);
        var lanes = new InterstellarLaneNetwork().Build(galaxy.Systems);
        var distanceScale = LegacyDistanceScale(galaxy, lanes);
        foreach (var source in sources) source.Range *= distanceScale;
        var diplomatic = Diplomacy?.Snapshot();
        // Recognition reinforces a represented political presence; paper claims alone project nothing.
        if (diplomatic is not null)
            foreach (var claim in diplomatic.Claims.Where(c => c.Active && positions.ContainsKey(c.SystemId)))
            {
                if (!diplomatic.ClaimResponses.Any(r => r.ClaimId == claim.ClaimId && r.Response == TerritorialClaimResponse.Recognized)) continue;
                var source = sources.Where(s => s.Owner == claim.ClaimantCivilizationId && s.Political > 0 && s.Kind != "Recognized claim")
                    .OrderBy(s => InterstellarDistance.Between(positions[s.System], positions[claim.SystemId])).FirstOrDefault();
                if (source is null || InterstellarDistance.Between(positions[source.System], positions[claim.SystemId]) > source.Range) continue;
                sources.Add(new Source { Owner = claim.ClaimantCivilizationId, System = claim.SystemId,
                    Kind = "Recognized claim", Political = TerritorialBalance.RecognizedClaimInfluence, Range = 8 * distanceScale });
            }
        var adjacency = galaxy.Systems.ToDictionary(s => s.Id, _ => new List<(int Id, double Distance)>());
        foreach (var lane in lanes)
        {
            adjacency[lane.FirstSystemId].Add((lane.SecondSystemId, lane.LengthLightYears));
            adjacency[lane.SecondSystemId].Add((lane.FirstSystemId, lane.LengthLightYears));
        }
        var raw = new Dictionary<int, List<CivilizationSystemTerritory>>();
        foreach (var system in galaxy.Systems) raw[system.Id] = new();
        foreach (var civilization in galaxy.Civilizations.OrderBy(c => c.Id))
        {
            var funding = CivilizationOperatingCapacity.GetFundingFraction(galaxy, civilization.Id);
            var own = sources.Where(s => s.Owner == civilization.Id).ToArray();
            var support = Supply(galaxy, civilization.Id, own, adjacency, Diplomacy, distanceScale);
            ConnectAdministration(own, positions, distanceScale);
            var colonies = galaxy.Colonies.Where(c => c.CivilizationId == civilization.Id).ToArray();
            var adminCapacity = 4 + colonies.Sum(c => c.SurfaceHubLevel * 2 + c.PopulationMillions / 1000);
            var load = Math.Max(1, (colonies.Length + own.Count(s => !s.Colony) * .4) / adminCapacity);
            var military = galaxy.Fleets.Where(f => f.IsActive && f.CivilizationId == civilization.Id &&
                f.Role == FleetRole.Military && f.CurrentSystemId.HasValue && f.TransitPhase != FleetTransitPhase.InterstellarWarp)
                .GroupBy(f => f.CurrentSystemId!.Value).ToDictionary(g => g.Key, g =>
                    Math.Min(1, g.Sum(Game.Simulation.Combat.FleetCombatPower.OwnPower) / 1000));
            foreach (var system in galaxy.Systems)
            {
                double political = 0, admin = 0, trade = 0, defense = military.GetValueOrDefault(system.Id);
                var contributions = new List<TerritorialContribution>(3);
                foreach (var source in own)
                {
                    var distance = InterstellarDistance.Between(positions[source.System], system);
                    if (distance > source.Range * 4) continue;
                    var falloff = Math.Exp(-distance / source.Range * TerritorialBalance.PoliticalFalloff);
                    var p = source.Political * falloff;
                    var a = source.AdminConnection * Math.Exp(-distance / source.Range * TerritorialBalance.AdministrationFalloff) / load;
                    political += p;
                    admin = Math.Max(admin, a);
                    trade = Math.Max(trade, source.Trade * source.AdminConnection * falloff);
                    defense = Math.Max(defense, source.Military * falloff);
                    if (p >= .5)
                    {
                        var slot = contributions.FindIndex(c => p > c.Political || p == c.Political && source.System < c.SystemId);
                        if (slot < 0) slot = contributions.Count;
                        if (slot < 3) { contributions.Insert(slot, new(source.System, source.Kind, p, a)); if (contributions.Count > 3) contributions.RemoveAt(3); }
                    }
                }
                var supply = support.GetValueOrDefault(system.Id) * funding;
                var expansion = political >= TerritorialBalance.EstablishedPolitical && admin >= TerritorialBalance.EstablishedAdministration && supply >= TerritorialBalance.EstablishedSupply
                    ? ExpansionRegion.Established
                    : political >= TerritorialBalance.FrontierMinimumPolitical &&
                      admin >= TerritorialBalance.FrontierMinimumAdministration && supply >= TerritorialBalance.FrontierMinimumSupply
                        ? ExpansionRegion.Frontier : ExpansionRegion.Remote;
                raw[system.Id].Add(new(civilization.Id, system.Id, political, 0, Math.Clamp(admin, 0, 1),
                    supply, Math.Clamp(trade, 0, 1), defense, 0, 1, 1, expansion,
                    contributions.ToArray()));
            }
        }
        _systems.Clear(); _scores.Clear();
        foreach (var (id, values) in raw)
        {
            var total = TerritorialBalance.IndependentWeight + values.Sum(v => v.Political);
            var revised = values.Select(v =>
            {
                var share = v.Political / total;
                // Defence can help hold an existing political position, but never creates one.
                var control = Math.Clamp(share * (.50 * v.Administration + .30 * v.Supply + .10 * v.Trade + .10 * v.Military), 0, 1);
                var tax = TerritorialBalance.MinimumTaxCollection + (1 - TerritorialBalance.MinimumTaxCollection) * Math.Min(1, control / .6);
                var ownColony = colonyLocations.Contains((v.CivilizationId, id));
                return v with { Share = share, EffectiveControl = control,
                    TaxCollection = ownColony && v.Administration >= .95 && v.Supply >= .95 ? 1 : tax,
                    AdministrationMultiplier = 1 + (1 - v.Administration) * TerritorialBalance.AdministrationPenalty + (1 - v.Supply) * TerritorialBalance.SupplyPenalty };
            }).OrderByDescending(v => v.Share).ThenBy(v => v.CivilizationId).ToArray();
            var first = revised.FirstOrDefault(); var second = revised.Skip(1).FirstOrDefault();
            var contested = first is not null && second is not null && second.Share >= TerritorialBalance.ContestedMinimumShare && first.Share - second.Share < TerritorialBalance.ContestedMaximumGap;
            var status = contested ? TerritorialControlStatus.Contested : first?.EffectiveControl >= TerritorialBalance.ControlMinimum && first.Share >= TerritorialBalance.DominanceMinimumShare
                ? TerritorialControlStatus.Controlled : first?.Share >= TerritorialBalance.DominanceMinimumShare ? TerritorialControlStatus.Dominant
                : first?.Political >= 4 ? TerritorialControlStatus.Influenced : TerritorialControlStatus.Independent;
            var controller = status == TerritorialControlStatus.Controlled ? first!.CivilizationId : (int?)null;
            _systems[id] = new(id, controller, status, TerritorialBalance.IndependentWeight / total, revised);
            foreach (var score in revised) _scores[(score.CivilizationId, id)] = score;
        }
        Revision++; RecomputeCount++; _sourceSignature = SourceSignature(galaxy);
        LastCalculationMilliseconds = Stopwatch.GetElapsedTime(started).TotalMilliseconds;
    }

    private sealed class Source
    {
        public int Owner, System;
        public string Kind = "";
        public bool Colony, AdminRoot, SupplyRoot, Refuel;
        public double Political, Admin, AdminConnection, Trade, Military, Range;
    }
    private static List<Source> BuildSources(GalaxyState galaxy, Dictionary<int, StarSystemState> positions)
    {
        var result = new List<Source>();
        foreach (var colony in galaxy.Colonies.OrderBy(c => c.Id))
        {
            if (colony.PopulationMillions <= 0 || !positions.ContainsKey(colony.SystemId)) continue;
            var full = colony.Kind == SettlementKind.Colony;
            var funding = CivilizationOperatingCapacity.GetFundingFraction(galaxy, colony.CivilizationId);
            var civic = Math.Clamp(colony.Stability, .1, 1) * (.5 + .5 * funding);
            var pop = Math.Log10(1 + colony.PopulationMillions) / 4;
            result.Add(new Source { Owner = colony.CivilizationId, System = colony.SystemId, Kind = full ? "Colony" : "Resource outpost",
                Colony = full, AdminRoot = full, SupplyRoot = full, Refuel = true,
                Political = (full ? TerritorialBalance.ColonyStrength + pop * TerritorialBalance.PopulationStrength + colony.Infrastructure * TerritorialBalance.InfrastructureStrength + colony.SurfaceHubLevel * TerritorialBalance.CapitalTierStrength : TerritorialBalance.OutpostStrength) * civic,
                Admin = full ? Math.Clamp(.35 + pop * .35 + colony.Infrastructure * .2 + colony.SurfaceHubLevel * .1, .3, 1) * (.5 + .5 * funding) : .25,
                Trade = full ? Math.Clamp(.3 + colony.Infrastructure * .14, 0, .9) : .12,
                Range = full ? TerritorialBalance.ColonyRange : TerritorialBalance.OutpostRange });
        }
        // Home-system orbital programs have no invented remote sites.
        foreach (var construction in galaxy.ConstructionStates)
        {
            var home = galaxy.Civilizations.First(c => c.Id == construction.CivilizationId).HomeSystemId;
            var source = result.FirstOrDefault(s => s.Owner == construction.CivilizationId && s.System == home && s.Colony);
            if (source is null) continue;
            if (construction.CompletedProjectIds.Contains("research_network")) source.Political += TerritorialBalance.ResearchNetworkInfluence;
            if (construction.CompletedProjectIds.Contains("industrial_automation")) source.Trade += .1;
            if (construction.CompletedProjectIds.Contains("orbital_shipyard")) { source.Political += TerritorialBalance.OrbitalShipyardInfluence; source.Military = .2; }
            if (construction.CompletedProjectIds.Contains("asteroid_resource_network")) { source.Political += TerritorialBalance.MiningNetworkInfluence; source.Trade += .1; }
        }
        foreach (var site in galaxy.Territory?.Installations.Where(i => i.IsComplete).OrderBy(i => i.Id) ?? Enumerable.Empty<TerritorialInstallation>())
        {
            if (!positions.ContainsKey(site.SystemId)) continue;
            var definition = TerritorialBalance.Definition(site.Kind);
            var funding = CivilizationOperatingCapacity.GetFundingFraction(galaxy, site.CivilizationId);
            if (funding <= .01) continue;
            result.Add(new Source { Owner = site.CivilizationId, System = site.SystemId, Kind = definition.Name,
                Political = definition.Political * funding, Admin = definition.Administration * funding,
                Trade = definition.Trade * funding, Military = definition.Military * funding, Range = definition.Range,
                Refuel = site.Kind is TerritorialInstallationKind.SupplyDepot or TerritorialInstallationKind.NavalBase });
        }
        return result;
    }
    private static void ConnectAdministration(Source[] sources, Dictionary<int, StarSystemState> positions, double distanceScale)
    {
        foreach (var source in sources) source.AdminConnection = source.AdminRoot ? source.Admin : 0;
        // Max-product propagation cannot self-amplify a relay loop.
        for (var pass = 0; pass < sources.Length; pass++)
        {
            var changed = false;
            foreach (var target in sources.Where(s => !s.AdminRoot && s.Admin > 0))
                foreach (var origin in sources)
                {
                    if (ReferenceEquals(origin, target) || origin.AdminConnection <= .01) continue;
                    var distance = InterstellarDistance.Between(positions[origin.System], positions[target.System]);
                    if (distance > TerritorialBalance.RelayConnectionRange * distanceScale) continue;
                    var value = origin.AdminConnection * TerritorialBalance.RelayAttenuation * Math.Exp(-distance / (80 * distanceScale)) * Math.Min(1, target.Admin + .3);
                    if (value <= target.AdminConnection + 1e-8) continue;
                    target.AdminConnection = value; changed = true;
                }
            if (!changed) break;
        }
    }
    private static Dictionary<int, double> Supply(GalaxyState galaxy, int owner, Source[] sources,
        Dictionary<int, List<(int Id, double Distance)>> graph, DiplomacyState? diplomacy, double distanceScale)
    {
        // Operational fleet support only. This is not food/water delivery or freight capacity.
        var range = galaxy.Fleets.Where(f => f.IsActive && f.CivilizationId == owner)
            .Select(f => Math.Min(f.MaximumLegRangeLightYears, f.FuelCapacityLightYears)).DefaultIfEmpty(12).Max();
        range = Math.Clamp(range, 6, 60 * distanceScale);
        var depots = sources.Where(s => s.Refuel).Select(s => s.System).ToHashSet();
        var distance = new Dictionary<int, double>(); var queue = new PriorityQueue<int, double>();
        foreach (var root in sources.Where(s => s.SupplyRoot)) { distance[root.System] = 0; queue.Enqueue(root.System, 0); }
        var foreign = galaxy.Colonies.Where(c => c.CivilizationId != owner && galaxy.Knowledge.IsSystemFullySurveyed(owner, c.SystemId) &&
            diplomacy?.GetAccessPermission(c.CivilizationId, owner) != AccessPermission.Granted)
            .Select(c => c.SystemId).ToHashSet();
        while (queue.TryDequeue(out var current, out var used))
        {
            if (used > distance[current] + 1e-8) continue;
            foreach (var edge in graph[current])
            {
                if (foreign.Contains(edge.Id) || edge.Distance > range) continue;
                var next = used + edge.Distance;
                if (next > range * 2) continue;
                if (depots.Contains(edge.Id)) next = 0;
                if (distance.TryGetValue(edge.Id, out var previous) && next >= previous - 1e-8) continue;
                distance[edge.Id] = next; queue.Enqueue(edge.Id, next);
            }
        }
        return distance.ToDictionary(p => p.Key, p => Math.Exp(-p.Value / range));
    }
    private static double LegacyDistanceScale(GalaxyState galaxy, System.Collections.Generic.IReadOnlyList<InterstellarLane> lanes)
    {
        // Pre-astronomy campaigns used an arbitrary 900-unit disk. Preserve their geography
        // while giving their old coordinate units a consistent territorial range. Physical
        // XYZ catalogs and current FullGalaxy maps always use the unscaled light-year rules.
        if (galaxy.Systems.Any(s => s.GalacticDepthLightYears.HasValue)) return 1;
        var nearest = galaxy.Systems.ToDictionary(s => s.Id, _ => double.PositiveInfinity);
        foreach (var lane in lanes)
        {
            nearest[lane.FirstSystemId] = Math.Min(nearest[lane.FirstSystemId], lane.LengthLightYears);
            nearest[lane.SecondSystemId] = Math.Min(nearest[lane.SecondSystemId], lane.LengthLightYears);
        }
        var values = nearest.Values.Where(double.IsFinite).OrderBy(v => v).ToArray();
        var median = values.Length == 0 ? 0 : values[values.Length / 2];
        return median <= 20 ? 1 : Math.Clamp(median / 8, 1, 16);
    }
    private static int SourceSignature(GalaxyState galaxy)
    {
        var hash = new HashCode();
        foreach (var c in galaxy.Colonies) { hash.Add(c.Id); hash.Add(c.CivilizationId); hash.Add(c.SystemId); hash.Add(c.Kind); hash.Add(c.SurfaceHubLevel); hash.Add(c.PopulationMillions > 0); }
        foreach (var i in galaxy.Territory!.Installations) { hash.Add(i.Id); hash.Add(i.CivilizationId); hash.Add(i.SystemId); hash.Add(i.IsComplete); hash.Add(i.Kind); }
        foreach (var f in galaxy.Fleets) { hash.Add(f.Id); hash.Add(f.IsActive); hash.Add(f.CurrentSystemId); hash.Add(f.CivilizationId); }
        foreach (var c in galaxy.ConstructionStates) foreach (var id in c.CompletedProjectIds.OrderBy(x => x)) hash.Add(id);
        return hash.ToHashCode();
    }
}
