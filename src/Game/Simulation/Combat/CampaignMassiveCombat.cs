using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Models;

namespace Game.Simulation.Combat;

public sealed class CampaignMassiveEncounter
{
    public const int MaxEngagementEvidence = 65_536;
    public int SystemId { get; set; }
    public double StartedDay { get; set; }
    public MassiveCombatBattleState Battle { get; set; } = new();
    public List<CampaignCombatBinding> Vessels { get; set; } = new();
    public List<CampaignCombatEngagement> EngagedFormationPairs { get; set; } = new();
    public long LastObservedEventSequence { get; set; }
    public bool Reconciled { get; set; }

    public void Validate(GalaxyState galaxy)
    {
        Battle.Validate();
        if (!galaxy.Systems.Any(s => s.Id == SystemId) || !double.IsFinite(StartedDay) || StartedDay < 0 ||
            Vessels.Count == 0 || Vessels.Count > MassiveCombatLimits.MaxShips ||
            Vessels.Select(v => v.FleetId).Distinct().Count() != Vessels.Count || LastObservedEventSequence < 0 ||
            EngagedFormationPairs.Count > MaxEngagementEvidence ||
            EngagedFormationPairs.Distinct().Count() != EngagedFormationPairs.Count)
            throw new InvalidDataException("Campaign combat encounter identity, participants, or engagement evidence is invalid.");
        var fleets = galaxy.Fleets.ToDictionary(f => f.Id);
        var formations = Battle.Formations.ToDictionary(f => f.Id);
        var bindingsByFormation = Vessels.GroupBy(x => x.FormationId).ToDictionary(x => x.Key, x => x.ToArray());
        foreach (var binding in Vessels)
            if (!fleets.TryGetValue(binding.FleetId, out var fleet) ||
                !formations.TryGetValue(binding.FormationId, out var formation) ||
                fleet.CivilizationId != formation.CivilizationId ||
                (!formation.ImportantVessels.Any(v => v.Id == binding.FleetId) &&
                    !formation.Cohorts.Any(c => c.DesignId == (fleet.DesignId ?? fleet.Combat?.ProfileId))))
                throw new InvalidDataException("Campaign combat participant does not match its persistent vessel.");
        foreach (var formation in formations.Values)
        {
            var bound = bindingsByFormation.GetValueOrDefault(formation.Id) ?? Array.Empty<CampaignCombatBinding>();
            var importantIds = formation.ImportantVessels.Select(x => x.Id).ToHashSet();
            if (bound.Length != formation.InitialShipCount ||
                bound.Count(x => importantIds.Contains(x.FleetId)) != formation.ImportantVessels.Count ||
                bound.Count(x => !importantIds.Contains(x.FleetId)) != formation.Cohorts.Sum(x => x.InitialCount))
                throw new InvalidDataException("Campaign combat formation does not conserve its bound vessel inventory.");
        }
        if (EngagedFormationPairs.Any(pair => pair.FirstFormationId >= pair.SecondFormationId ||
            !formations.ContainsKey(pair.FirstFormationId) || !formations.ContainsKey(pair.SecondFormationId)))
            throw new InvalidDataException("Campaign combat engagement evidence references invalid formations.");
    }
}

public sealed record CampaignCombatBinding(int FleetId, long FormationId);
public sealed record CampaignCombatEngagement(long FirstFormationId, long SecondFormationId)
{
    public static CampaignCombatEngagement Create(long first, long second) => first < second ? new(first, second) : new(second, first);
}

/// <summary>Owns one live tactical encounter while the strategic clock is suspended.</summary>
public sealed class CampaignMassiveCombat
{
    private readonly ICombatHostilityView _hostility;
    private EngagementEvidenceIndex? _engagementEvidenceIndex;
    public CampaignCombatAdvanceTiming? LastAdvanceTiming { get; private set; }
    public MassiveCombatEngine Engine { get; }
    public CampaignMassiveCombat(ICombatHostilityView hostility)
    {
        _hostility = hostility ?? throw new ArgumentNullException(nameof(hostility));
        Engine = new MassiveCombatEngine(new HostilityAdapter(hostility));
    }

    public CombatOrderResult Begin(GalaxyState galaxy, int civilizationId, int actorFleetId, double day)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (!double.IsFinite(day) || day < 0) return new(false, "Combat time is invalid.");
        if (galaxy.ActiveCombatEncounter is { Reconciled: false })
            return new(false, "An encounter is already active. Use its tactical orders.");
        var actor = galaxy.Fleets.FirstOrDefault(f => f.Id == actorFleetId && f.IsActive &&
            f.CivilizationId == civilizationId && f.Role == FleetRole.Military);
        if (actor?.CurrentSystemId is not int systemId || actor.DestinationSystemId is not null)
            return new(false, "An armed fleet must be stationed in a system before engaging.");
        var participants = galaxy.Fleets.Where(f => f.IsActive && f.CurrentSystemId == systemId &&
            f.DestinationSystemId is null && (f.CivilizationId == civilizationId || _hostility.AreHostile(civilizationId, f.CivilizationId)))
            .OrderBy(f => f.Id).ToArray();
        if (!participants.Any(f => _hostility.AreHostile(civilizationId, f.CivilizationId)))
            return new(false, "No attackable hostile formation is detected in this system.");
        if (participants.Length > MassiveCombatLimits.MaxShips)
            return new(false, $"This encounter exceeds the {MassiveCombatLimits.MaxShips:N0}-ship tactical limit.");

        var legacyLoadouts = new Dictionary<string, MassiveCombatLoadout>(StringComparer.Ordinal);
        var loadoutIdentities = new Dictionary<MassiveCombatLoadout, string>(ReferenceEqualityComparer.Instance);
        var prepared = participants.Select(fleet => Prepare(fleet, legacyLoadouts, loadoutIdentities)).ToArray();
        if (prepared.Any(x => x.Combat.Hull <= 0))
            return new(false, "A participating vessel has no combat-ready hull.");
        var groups = prepared.GroupBy(x => new FormationCompatibility(
                x.Fleet.CivilizationId,
                x.Fleet.Role == FleetRole.Military,
                x.Combat.ProfileId,
                BitConverter.DoubleToInt64Bits(x.Combat.Shields),
                BitConverter.DoubleToInt64Bits(x.Combat.Armor),
                BitConverter.DoubleToInt64Bits(x.Combat.Hull),
                x.LoadoutIdentity),
            x => x).OrderBy(x => x.Key.CivilizationId).ThenBy(x => x.Min(v => v.Fleet.Id)).ToArray();
        var formationCount = groups.Sum(group => Math.Max(1, (group.Count(x => IsImportant(x.Vessel)) +
            MassiveCombatLimits.MaxImportantVesselsPerFormation - 1) / MassiveCombatLimits.MaxImportantVesselsPerFormation));
        if (formationCount > MassiveCombatLimits.MaxFormations)
            return new(false, "The participating vessels require more damage-compatible tactical groups than the formation limit permits.");

        var formations = new List<MassiveFormationState>(formationCount);
        var bindings = new List<CampaignCombatBinding>(participants.Length);
        var membersByFormation = new Dictionary<long, PreparedVessel[]>();
        foreach (var group in groups)
        {
            var members = group.OrderBy(x => x.Fleet.Id).ToArray();
            var ordinary = members.Where(x => !IsImportant(x.Vessel)).ToArray();
            var important = members.Where(x => IsImportant(x.Vessel)).ToArray();
            var chunks = Math.Max(1, (important.Length + MassiveCombatLimits.MaxImportantVesselsPerFormation - 1) /
                MassiveCombatLimits.MaxImportantVesselsPerFormation);
            for (var chunk = 0; chunk < chunks; chunk++)
            {
                var selectedImportant = important.Skip(chunk * MassiveCombatLimits.MaxImportantVesselsPerFormation)
                    .Take(MassiveCombatLimits.MaxImportantVesselsPerFormation).ToArray();
                var selectedOrdinary = chunk == 0 ? ordinary : Array.Empty<PreparedVessel>();
                var selected = selectedOrdinary.Concat(selectedImportant).OrderBy(x => x.Fleet.Id).ToArray();
                if (selected.Length == 0) continue;
                var sample = selected[0];
                var formationId = formations.Count + 1L;
                var side = sample.Fleet.CivilizationId == civilizationId ? -1f : 1f;
                var tacticalVessels = selectedImportant.Select(x => Clone(x.Vessel)).ToList();
                foreach (var vessel in tacticalVessels) { vessel.Destroyed = false; vessel.Escaped = false; }
                var formation = new MassiveFormationState
                {
                    Id = formationId, FleetId = sample.Fleet.Id, TaskForceId = sample.Fleet.CivilizationId,
                    CivilizationId = sample.Fleet.CivilizationId, Name = $"Task Force {formationId:N0}",
                    Position = new(side * 420f, (formations.Count % 24 - 12) * 55f),
                    Heading = new(-side, 0), Objective = new(0, 0),
                    Order = group.Key.IsMilitary ? MassiveCombatOrderType.Engage : MassiveCombatOrderType.Retreat,
                    Shape = group.Key.IsMilitary ? MassiveFormationShape.Line : MassiveFormationShape.RetreatColumn,
                    Loadout = Clone(sample.Loadout), ImportantVessels = tacticalVessels,
                    HullLossThresholdPerShip = (float)sample.Combat.Hull,
                };
                if (selectedOrdinary.Length > 0)
                    formation.Cohorts.Add(new()
                    {
                        Id = formationId * 1_000_000L + 1,
                        DesignId = sample.Fleet.DesignId ?? sample.Combat.ProfileId,
                        InitialCount = selectedOrdinary.Length,
                        ActiveCount = selectedOrdinary.Length,
                    });
                formations.Add(formation);
                bindings.AddRange(selected.Select(x => new CampaignCombatBinding(x.Fleet.Id, formationId)));
                membersByFormation.Add(formationId, selected);
            }
        }
        var battle = MassiveCombatBattleState.Create(
            unchecked((ulong)galaxy.Seed ^ (ulong)BitConverter.DoubleToInt64Bits(day) ^ (uint)actorFleetId), formations);
        foreach (var formation in formations)
        {
            var members = membersByFormation[formation.Id];
            formation.ShieldPool = (float)members.Sum(x => x.Combat.Shields);
            formation.ArmorPool = (float)members.Sum(x => x.Combat.Armor);
            formation.HullPool = (float)members.Sum(x => x.Combat.Hull);
        }
        _engagementEvidenceIndex = null;
        galaxy.ActiveCombatEncounter = new() { SystemId = systemId, StartedDay = day, Battle = battle, Vessels = bindings };
        galaxy.ActiveCombatEncounter.Validate(galaxy);
        return new(true, $"Encounter established: {participants.Length:N0} commissioned vessels. Tactical orders ready.");
    }

    public MassiveCombatSnapshot Observe(GalaxyState galaxy, int observerId, bool scanningCapability)
    {
        var encounter = galaxy.ActiveCombatEncounter ?? throw new InvalidOperationException("No tactical encounter is active.");
        return MassiveCombatObserver.BuildSnapshot(encounter.Battle, observerId, new EncounterSensors(encounter, scanningCapability));
    }

    public IReadOnlyList<CombatEvent> Advance(GalaxyState galaxy, double elapsedSeconds, Func<int, bool>? hasCombatScanner = null)
    {
        var encounter = galaxy.ActiveCombatEncounter;
        if (encounter is null)
        {
            _engagementEvidenceIndex = null;
            throw new InvalidOperationException("No tactical encounter is active.");
        }
        if (encounter.Reconciled)
        {
            _engagementEvidenceIndex = null;
            return Array.Empty<CombatEvent>();
        }
        var diagnostic = string.Equals(Environment.GetEnvironmentVariable("STELLAR_MASSIVE_PERFORMANCE_DIAGNOSTIC"), "1", StringComparison.Ordinal);
        var stopwatch = diagnostic ? Stopwatch.StartNew() : null;
        Engine.Advance(encounter.Battle, elapsedSeconds);
        var engineMilliseconds = stopwatch?.Elapsed.TotalMilliseconds ?? 0;
        CaptureEngagementEvidence(galaxy, encounter);
        var evidenceMilliseconds = stopwatch?.Elapsed.TotalMilliseconds - engineMilliseconds ?? 0;
        ApplyObserverSafeDoctrine(galaxy, encounter, hasCombatScanner);
        var doctrineMilliseconds = stopwatch?.Elapsed.TotalMilliseconds - engineMilliseconds - evidenceMilliseconds ?? 0;
        var result = Reconcile(galaxy);
        if (stopwatch is not null)
            LastAdvanceTiming = new(engineMilliseconds, evidenceMilliseconds, doctrineMilliseconds,
                stopwatch.Elapsed.TotalMilliseconds - engineMilliseconds - evidenceMilliseconds - doctrineMilliseconds);
        return result;
    }

    public IReadOnlyList<CombatEvent> Reconcile(GalaxyState galaxy)
    {
        var encounter = galaxy.ActiveCombatEncounter;
        if (encounter is null || encounter.Reconciled)
        {
            _engagementEvidenceIndex = null;
            return Array.Empty<CombatEvent>();
        }
        if (!encounter.Battle.IsComplete && Engine.HasActiveHostilities(encounter.Battle)) return Array.Empty<CombatEvent>();
        var events = new List<CombatEvent>();
        var fleetMap = galaxy.Fleets.ToDictionary(f => f.Id);
        var bindingsByFormation = encounter.Vessels.GroupBy(x => x.FormationId).ToDictionary(x => x.Key, x => x.OrderBy(v => v.FleetId).ToArray());
        foreach (var formation in encounter.Battle.Formations)
        {
            var bound = bindingsByFormation.GetValueOrDefault(formation.Id) ?? Array.Empty<CampaignCombatBinding>();
            var important = formation.ImportantVessels.ToDictionary(x => x.Id);
            var ordinary = bound.Where(x => !important.ContainsKey(x.FleetId)).ToArray();
            var ordinarySurvivors = ordinary.Take(formation.Cohorts.Sum(x => x.ActiveCount)).Select(x => x.FleetId);
            var survivorIds = important.Values.Where(x => !x.Destroyed).Select(x => checked((int)x.Id)).Concat(ordinarySurvivors).ToHashSet();
            var surviving = bound.Where(x => survivorIds.Contains(x.FleetId)).ToArray();
            var shieldsEach = surviving.Length == 0 ? 0 : formation.ShieldPool / surviving.Length;
            var armorEach = surviving.Length == 0 ? 0 : formation.ArmorPool / surviving.Length;
            var hullEach = surviving.Length == 0 ? 0 : formation.HullPool / surviving.Length;
            foreach (var binding in bound)
            {
                if (!fleetMap.TryGetValue(binding.FleetId, out var fleet)) continue;
                var profile = Profile(fleet); var combat = CombatProfileRegistry.EnsureState(fleet);
                var survived = survivorIds.Contains(fleet.Id);
                var vessel = important.TryGetValue(fleet.Id, out var tracked) ? Clone(tracked) :
                    Clone(fleet.TacticalVessel ?? Vessel(fleet, combat, profile));
                vessel.Name = fleet.Name; vessel.Destroyed = !survived; vessel.Escaped = survived && formation.Escaped;
                vessel.BattlesFought++;
                fleet.TacticalLoadout = Clone(formation.Loadout); fleet.TacticalVessel = vessel;
                combat.Shields = survived ? Math.Min(profile.MaxShields, shieldsEach) : 0;
                combat.Armor = survived ? Math.Min(profile.MaxArmor, armorEach) : 0;
                combat.Hull = survived ? Math.Min(profile.MaxHull, hullEach) : 0;
                vessel.HullFraction = (float)Math.Clamp(combat.Hull / Math.Max(1, profile.MaxHull), 0, 1);
                combat.Order = MilitaryOrderType.Hold; combat.TargetFleetId = null;
                combat.IsDisengaged = survived && formation.Escaped;
                combat.DisengagedSystemId = combat.IsDisengaged ? encounter.SystemId : null;
                if (survived) continue;
                fleet.IsActive = false;
                var casualties = Math.Max(0, fleet.EmbarkedPopulationMillions);
                fleet.EmbarkedPopulationMillions = 0; fleet.EmbarkedPopulationSpeciesId = null;
                fleet.DestinationSystemId = null; fleet.PlannedRouteSystemIds.Clear(); fleet.DestinationPlanetaryBodyId = null;
                if (events.Count < 128) events.Add(new(CombatEventType.FleetDestroyed, encounter.SystemId,
                    fleet.CivilizationId, fleet.Id, null, null, 0, 0, profile.MaxHull,
                    fleet.Name + " was lost in combat.", casualties));
            }
        }
        encounter.Reconciled = true;
        _engagementEvidenceIndex = null;
        events.Add(new(CombatEventType.EngagementEnded, encounter.SystemId, galaxy.PlayerCivilizationId, 0,
            null, null, 0, 0, 0, "Tactical encounter concluded. Damage and losses are persistent."));
        return events;
    }

    private void CaptureEngagementEvidence(GalaxyState galaxy, CampaignMassiveEncounter encounter)
    {
        if (!encounter.Battle.Events.Any(combatEvent => combatEvent.Sequence > encounter.LastObservedEventSequence)) return;
        var formations = encounter.Battle.Formations.ToDictionary(formation => formation.Id);
        EngagementEvidenceIndex? index = null;
        foreach (var combatEvent in encounter.Battle.Events.Where(x => x.Sequence > encounter.LastObservedEventSequence).OrderBy(x => x.Sequence))
        {
            encounter.LastObservedEventSequence = Math.Max(encounter.LastObservedEventSequence, combatEvent.Sequence);
            if (combatEvent.TargetFormationId is not long targetId ||
                combatEvent.Type is not (MassiveCombatEventType.BeamVolley or MassiveCombatEventType.KineticVolley or MassiveCombatEventType.MissileSalvo or MassiveCombatEventType.Damage) ||
                !formations.TryGetValue(combatEvent.ActorFormationId, out var actor) || !formations.TryGetValue(targetId, out var target)) continue;
            var pair = CampaignCombatEngagement.Create(actor.Id, target.Id);
            var newlyObserved = !encounter.EngagedFormationPairs.Contains(pair) && encounter.EngagedFormationPairs.Count < CampaignMassiveEncounter.MaxEngagementEvidence;
            if (newlyObserved) encounter.EngagedFormationPairs.Add(pair);
            if (actor.CivilizationId == target.CivilizationId) continue;
            if (!newlyObserved) continue;
            index ??= EngagementEvidenceIndex.For(galaxy, encounter, ref _engagementEvidenceIndex);
            FleetCombatPower.ObserveMany(galaxy, actor.CivilizationId,
                index.FleetsFor(target.Id), encounter.StartedDay, true, false, index.FleetsById);
            FleetCombatPower.ObserveMany(galaxy, target.CivilizationId,
                index.FleetsFor(actor.Id), encounter.StartedDay, true, false, index.FleetsById);
        }
        encounter.EngagedFormationPairs.Sort((a, b) => a.FirstFormationId != b.FirstFormationId
            ? a.FirstFormationId.CompareTo(b.FirstFormationId) : a.SecondFormationId.CompareTo(b.SecondFormationId));
    }

    private sealed class EngagementEvidenceIndex
    {
        private readonly GalaxyState _galaxy;
        private readonly CampaignMassiveEncounter _encounter;
        private readonly Dictionary<int, FleetState> _fleetsById;
        private readonly Dictionary<int, long> _formationByFleetId;
        private readonly Dictionary<long, FleetState[]> _fleetsByFormationId;

        private EngagementEvidenceIndex(GalaxyState galaxy, CampaignMassiveEncounter encounter)
        {
            _galaxy = galaxy;
            _encounter = encounter;
            _fleetsById = galaxy.Fleets.ToDictionary(fleet => fleet.Id);
            _formationByFleetId = encounter.Vessels.ToDictionary(binding => binding.FleetId, binding => binding.FormationId);
            _fleetsByFormationId = encounter.Vessels.GroupBy(binding => binding.FormationId)
                .ToDictionary(group => group.Key, group => group.Select(binding => _fleetsById[binding.FleetId]).ToArray());
        }

        public static EngagementEvidenceIndex For(GalaxyState galaxy, CampaignMassiveEncounter encounter,
            ref EngagementEvidenceIndex? cached)
        {
            if (cached is not null && cached.IsCurrent(galaxy, encounter)) return cached;
            return cached = new EngagementEvidenceIndex(galaxy, encounter);
        }

        public IEnumerable<FleetState> FleetsFor(long formationId) => _fleetsByFormationId[formationId];
        public IReadOnlyDictionary<int, FleetState> FleetsById => _fleetsById;

        private bool IsCurrent(GalaxyState galaxy, CampaignMassiveEncounter encounter)
        {
            if (!ReferenceEquals(_galaxy, galaxy) || !ReferenceEquals(_encounter, encounter) ||
                _fleetsById.Count != galaxy.Fleets.Count || _formationByFleetId.Count != encounter.Vessels.Count)
                return false;
            foreach (var fleet in galaxy.Fleets)
                if (!_fleetsById.TryGetValue(fleet.Id, out var cachedFleet) || !ReferenceEquals(cachedFleet, fleet)) return false;
            foreach (var binding in encounter.Vessels)
                if (!_formationByFleetId.TryGetValue(binding.FleetId, out var formationId) || formationId != binding.FormationId) return false;
            return true;
        }
    }

    private void ApplyObserverSafeDoctrine(GalaxyState galaxy, CampaignMassiveEncounter encounter, Func<int, bool>? hasCombatScanner)
    {
        if (encounter.Battle.Tick % 10 != 0) return;
        foreach (var civilizationId in encounter.Battle.Formations.Where(x => x.Active).Select(x => x.CivilizationId)
            .Distinct().Where(x => x != galaxy.PlayerCivilizationId).OrderBy(x => x))
        {
            var snapshot = Observe(galaxy, civilizationId, hasCombatScanner?.Invoke(civilizationId) == true);
            foreach (var order in MassiveCombatDoctrine.Decide(snapshot, civilizationId)) Engine.IssueOrder(encounter.Battle, civilizationId, order);
        }
    }

    public static T Clone<T>(T value) => JsonSerializer.Deserialize<T>(JsonSerializer.Serialize(value))!;
    private static PreparedVessel Prepare(FleetState fleet, IDictionary<string, MassiveCombatLoadout> legacyLoadouts,
        IDictionary<MassiveCombatLoadout, string> loadoutIdentities)
    {
        var profile = Profile(fleet);
        var combat = CombatProfileRegistry.EnsureState(fleet);
        if (!legacyLoadouts.TryGetValue(profile.Id, out var legacyLoadout))
            legacyLoadouts[profile.Id] = legacyLoadout = MassiveCombatLoadouts.FromLegacy(profile);
        var loadout = fleet.TacticalLoadout ?? legacyLoadout;
        if (!loadoutIdentities.TryGetValue(loadout, out var loadoutIdentity))
            loadoutIdentities[loadout] = loadoutIdentity = JsonSerializer.Serialize(loadout);
        var vessel = fleet.TacticalVessel is null ? Vessel(fleet, combat, profile) : Clone(fleet.TacticalVessel);
        vessel.Name = fleet.Name;
        vessel.HullFraction = (float)Math.Clamp(combat.Hull / Math.Max(1, profile.MaxHull), 0, 1);
        vessel.Destroyed = false; vessel.Escaped = false;
        return new(fleet, combat, loadout, vessel, loadoutIdentity);
    }
    private static MassiveVesselState Vessel(FleetState fleet, FleetCombatState combat, CombatProfileDefinition profile) => new()
    {
        Id = fleet.Id, Name = fleet.Name, DesignId = fleet.DesignId ?? combat.ProfileId,
        IsStoryShip = fleet.Role == FleetRole.Colony,
        HullFraction = (float)Math.Clamp(combat.Hull / Math.Max(1, profile.MaxHull), 0, 1),
    };
    private static bool IsImportant(MassiveVesselState vessel) =>
        vessel.IsFlagship || vessel.IsCarrier || vessel.IsInterdictor || vessel.IsStoryShip;
    private sealed record PreparedVessel(FleetState Fleet, FleetCombatState Combat, MassiveCombatLoadout Loadout,
        MassiveVesselState Vessel, string LoadoutIdentity);
    private sealed record FormationCompatibility(int CivilizationId, bool IsMilitary, string ProfileId,
        long Shields, long Armor, long Hull, string LoadoutIdentity);
    private static CombatProfileDefinition Profile(FleetState fleet) =>
        CombatProfileRegistry.TryGet(fleet.Combat?.ProfileId ?? string.Empty, out var profile) ? profile :
            CombatProfileRegistry.Get(CombatProfileRegistry.DefaultProfileId(fleet.Role));
    private sealed class HostilityAdapter(ICombatHostilityView source) : IMassiveCombatHostilityView
    { public bool AreHostile(int first, int second) => source.AreHostile(first, second); }

    private sealed class EncounterSensors : IMassiveCombatSensorView
    {
        private readonly bool _scanningCapability;
        private readonly HashSet<(int Observer, long Formation)> _engaged = new();
        public EncounterSensors(CampaignMassiveEncounter encounter, bool scanningCapability)
        {
            _scanningCapability = scanningCapability;
            var owners = encounter.Battle.Formations.ToDictionary(x => x.Id, x => x.CivilizationId);
            foreach (var pair in encounter.EngagedFormationPairs)
            {
                _engaged.Add((owners[pair.FirstFormationId], pair.SecondFormationId));
                _engaged.Add((owners[pair.SecondFormationId], pair.FirstFormationId));
            }
        }
        public float Confidence(int observer, long formation) => .65f;
        public bool IdentifiesCohorts(int observer, long formation) => _scanningCapability || _engaged.Contains((observer, formation));
        public bool IdentifiesImportantVessels(int observer, long formation) => _scanningCapability || _engaged.Contains((observer, formation));
        public bool CanEstimateCombatPower(int observer, long formation) => _scanningCapability || _engaged.Contains((observer, formation));
    }
}

public sealed record CampaignCombatAdvanceTiming(double EngineMilliseconds, double EvidenceMilliseconds,
    double DoctrineMilliseconds, double ReconcileMilliseconds);
