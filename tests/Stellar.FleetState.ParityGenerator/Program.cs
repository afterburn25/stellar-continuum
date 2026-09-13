using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Models;

if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
var options = new JsonSerializerOptions {
    WriteIndented = true,
    IncludeFields = true,
    NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals
};
var cases = new List<object>();
JsonElement Clone(object? value) => JsonSerializer.SerializeToElement(value, options);
void Add(string name, string kind, object arguments, Func<object?> operation) {
    var result = Clone(null); object? error = null;
    try { result = Clone(operation()); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Result = result, Error = error });
}
void AddFleet(string name, string kind, FleetState fleet, object arguments, Func<object?> operation) {
    var before = Clone(fleet); var result = Clone(null); object? error = null;
    try { result = Clone(operation()); }
    catch (Exception exception) { error = new { Type = exception.GetType().Name, exception.Message }; }
    cases.Add(new { Name = name, Kind = kind, Arguments = arguments, Before = before,
        Result = result, Error = error, After = Clone(fleet) });
}
FleetState Fleet(FleetRole role = FleetRole.Scout, int civilizationId = 2, bool active = true) => new() {
    Id = 7, CivilizationId = civilizationId, Name = "Fixture", Role = role,
    Position = new Vector2(3, 4), IsActive = active
};

var profileIds = new[] { "civilian_light_v1", "civilian_science_v1", "civilian_heavy_v1", "patrol_corvette_mk1" };
cases.Add(new { Name = "combat-profile-catalog", Kind = "Catalog",
    Catalog = profileIds.Select(CombatProfileRegistry.Get).ToArray() });
Add("profile-get-valid", "ProfileGet", new { ProfileId = "civilian_science_v1" },
    () => CombatProfileRegistry.Get("civilian_science_v1"));
Add("profile-get-missing", "ProfileGet", new { ProfileId = "missing" },
    () => CombatProfileRegistry.Get("missing"));
Add("profile-find-valid", "ProfileFind", new { ProfileId = "civilian_heavy_v1" },
    () => CombatProfileRegistry.TryGet("civilian_heavy_v1", out var profile) ? profile : null);
Add("profile-find-missing", "ProfileFind", new { ProfileId = "missing" },
    () => CombatProfileRegistry.TryGet("missing", out var profile) ? profile : null);
foreach (var role in Enum.GetValues<FleetRole>())
    Add("initial-" + role, "Initial", new { Role = (int)role, ProfileId = (string?)null },
        () => CombatProfileRegistry.CreateInitialState(null, role));
Add("initial-explicit", "Initial", new { Role = (int)FleetRole.Scout, ProfileId = "patrol_corvette_mk1" },
    () => CombatProfileRegistry.CreateInitialState("patrol_corvette_mk1", FleetRole.Scout));
Add("initial-empty", "Initial", new { Role = (int)FleetRole.Military, ProfileId = "" },
    () => CombatProfileRegistry.CreateInitialState("", FleetRole.Military));
Add("initial-whitespace", "Initial", new { Role = (int)FleetRole.Science, ProfileId = " \t" },
    () => CombatProfileRegistry.CreateInitialState(" \t", FleetRole.Science));
Add("initial-unknown", "Initial", new { Role = (int)FleetRole.Military, ProfileId = "unknown" },
    () => CombatProfileRegistry.CreateInitialState("unknown", FleetRole.Military));

var fleet = Fleet(FleetRole.Military);
AddFleet("ensure-null", "Ensure", fleet, new { }, () => CombatProfileRegistry.EnsureState(fleet));
fleet = Fleet(); fleet.Combat = new FleetCombatState { ProfileId = "unknown", Hull = 4, TargetFleetId = 8 };
AddFleet("ensure-unknown-resets-whole-state", "Ensure", fleet, new { }, () => CombatProfileRegistry.EnsureState(fleet));
fleet = Fleet(FleetRole.Military); fleet.CurrentSystemId = 4;
fleet.Combat = new FleetCombatState { ProfileId = "patrol_corvette_mk1", Shields = 99, Armor = -1,
    Hull = 200, WeaponCooldownRemainingDays = -1, RetreatProgressDays = -1,
    Order = (MilitaryOrderType)99, TargetFleetId = 8, DefendSystemId = 4,
    RetreatStarted = true, IsDisengaged = true, DisengagedSystemId = 3 };
AddFleet("ensure-clamps-order-and-disengagement", "Ensure", fleet, new { }, () => CombatProfileRegistry.EnsureState(fleet));
fleet = Fleet(FleetRole.Military); fleet.CurrentSystemId = 4;
fleet.Combat = new FleetCombatState { ProfileId = "patrol_corvette_mk1", Shields = double.NaN,
    Armor = double.NaN, Hull = double.NaN, WeaponCooldownRemainingDays = double.NaN,
    RetreatProgressDays = double.NaN, IsDisengaged = true, DisengagedSystemId = 4 };
AddFleet("ensure-preserves-nan-and-current-disengagement", "Ensure", fleet, new { }, () => CombatProfileRegistry.EnsureState(fleet));

AddFleet("fleet-default-roundtrip", "FleetRoundtrip", Fleet(), new { }, () => Fleet());
var full = new FleetState {
    Id = int.MinValue, CivilizationId = int.MaxValue, Name = "Full", Role = (FleetRole)99,
    DesignId = "resource_outpost_ship", Position = new(-3.25f, 4.5f), CurrentSystemId = -4,
    DestinationSystemId = 9, TransitPhase = FleetTransitPhase.InterstellarWarp,
    TransitOriginSystemId = 4, TransitTargetSystemId = 6, TransitProgress = .75,
    LocalTransitStart = new(1, 2), LocalTransitPosition = new(3, 4), LocalTransitTarget = new(5, 6),
    PlannedRouteSystemIds = new() { 6, 9, -1 }, HoldRequested = true, ReturnToBaseRequested = true,
    ReturnToBaseFailureReason = "blocked", MissionOrderRevision = -8, DestinationPlanetaryBodyId = 44,
    PreventAutomaticSettlement = true, SettlementBodyId = 44, SettlementDaysCompleted = -2.5,
    ReconnaissanceSystemId = 7, ReconnaissanceDaysCompleted = double.NaN,
    FreightTargetOutpostId = 8, FreightHomeColonyId = 2, CargoMaterialCapacity = -3,
    CargoMaterials = double.PositiveInfinity, StrategicSpeed = -22, MaximumLegRangeLightYears = -360,
    FuelCapacityLightYears = -1000, FuelRemainingLightYears = double.NegativeInfinity,
    SensorRange = -135, IsActive = false, EmbarkedPopulationMillions = -7.5,
    EmbarkedPopulationSpeciesId = "species-x",
    Combat = new FleetCombatState { ProfileId = "custom", Shields = 1, Armor = 2, Hull = 3,
        WeaponCooldownRemainingDays = 4, Order = MilitaryOrderType.Retreat, TargetFleetId = 5,
        DefendSystemId = 6, RetreatProgressDays = 7, RetreatStarted = true,
        IsDisengaged = true, DisengagedSystemId = 8 },
    TacticalLoadout = new MassiveCombatLoadout { MassPerShip = 1, Acceleration = 2,
        MaximumSpeed = 3, ShieldPerShip = 4, ArmorPerShip = 5, HullPerShip = 6,
        ReactorOutputPerShip = 7, CoolingPerShip = 8, WarpStabilization = 9,
        WarpSpoolSeconds = 10, ModuleSlotCapacity = 11, MaximumModuleMass = 12,
        Weapons = new() { new() { Id = "weapon", Kind = MassiveWeaponKind.Missile,
            MountsPerShip = 2, DamagePerShot = 3, ShotsPerSecond = 4, Range = 5,
            Accuracy = 6, PowerPerSecond = 7, HeatPerSecond = 8 } },
        Modules = new() { new() { Id = "module", Kind = MassiveModuleKind.Sensor,
            InstalledCount = 2, MassEach = 3, PowerPerSecondEach = 4, HeatPerSecondEach = 5,
            Condition = .5f, Enabled = false, EffectiveRange = 6, FieldStrength = 7,
            DetectionSignature = 8, Slots = 9 } } },
    TacticalVessel = new MassiveVesselState { Id = 99, Name = "History", DesignId = "design",
        IsFlagship = true, IsCarrier = true, IsInterdictor = true, IsStoryShip = true,
        HullFraction = .1f, EngineFraction = .2f, SensorFraction = .3f, WarpDriveFraction = .4f,
        ReactorFraction = .5f, InterdictorFraction = .6f, BattlesFought = -4,
        ConfirmedKills = -2, Destroyed = true, Escaped = true }
};
AddFleet("fleet-full-roundtrip", "FleetRoundtrip", full, new { }, () => full);
AddFleet("ensure-preserves-unrelated-full-fleet", "Ensure", full, new { },
    () => CombatProfileRegistry.EnsureState(full));

foreach (var profileId in profileIds)
    Add("legacy-loadout-" + profileId, "LegacyLoadout", new { ProfileId = profileId },
        () => MassiveCombatLoadouts.FromLegacy(CombatProfileRegistry.Get(profileId)));
var customProfile = new CombatProfileDefinition("custom", 1, 2, 3, 4, .5, double.NaN);
Add("legacy-loadout-custom-nan-retreat", "LegacyLoadout", new { Profile = Clone(customProfile) },
    () => MassiveCombatLoadouts.FromLegacy(customProfile));
Add("interdictor-default", "Interdictor", new { Range = 900f, Strength = 72f },
    () => MassiveCombatLoadouts.WarpInterdictor());
Add("interdictor-custom", "Interdictor", new { Range = 2000f, Strength = 1f },
    () => MassiveCombatLoadouts.WarpInterdictor(2000, 1));

void Validate(string name, string target, object value, Action operation) =>
    Add(name, "Validation", new { Target = target, Value = Clone(value) }, () => { operation(); return null; });
var weapon = new MassiveWeaponGroup { Id = "weapon", Kind = MassiveWeaponKind.ElectronicWarfare,
    MountsPerShip = 0, DamagePerShot = 0, ShotsPerSecond = 0, Range = 0, Accuracy = 0,
    PowerPerSecond = 0, HeatPerSecond = 0 };
Validate("weapon-valid-zero-boundary", "Weapon", weapon, weapon.Validate);
weapon = new MassiveWeaponGroup { Id = " \t" }; Validate("weapon-whitespace-id", "Weapon", weapon, weapon.Validate);
weapon = new MassiveWeaponGroup { Id = "\u00a0" }; Validate("weapon-nbsp-id", "Weapon", weapon, weapon.Validate);
weapon = new MassiveWeaponGroup { Id = "x", MountsPerShip = -1 }; Validate("weapon-negative-mounts", "Weapon", weapon, weapon.Validate);
weapon = new MassiveWeaponGroup { Id = "x", Kind = (MassiveWeaponKind)99 }; Validate("weapon-invalid-kind", "Weapon", weapon, weapon.Validate);
weapon = new MassiveWeaponGroup { Id = "x", Accuracy = float.NaN }; Validate("weapon-nan-scalar", "Weapon", weapon, weapon.Validate);
weapon = new MassiveWeaponGroup { Id = "x", HeatPerSecond = -1 }; Validate("weapon-negative-scalar", "Weapon", weapon, weapon.Validate);

var module = new MassiveModuleState { Id = "module", Kind = MassiveModuleKind.WarpInterdictor,
    InstalledCount = 0, Slots = 0, Condition = 0, EffectiveRange = 2000 };
Validate("module-valid-boundaries", "Module", module, module.Validate);
module = new MassiveModuleState { Id = " " }; Validate("module-whitespace-id", "Module", module, module.Validate);
module = new MassiveModuleState { Id = "\u2003" }; Validate("module-em-space-id", "Module", module, module.Validate);
module = new MassiveModuleState { Id = "x", InstalledCount = -1 }; Validate("module-negative-count", "Module", module, module.Validate);
module = new MassiveModuleState { Id = "x", Slots = -1 }; Validate("module-negative-slots", "Module", module, module.Validate);
module = new MassiveModuleState { Id = "x", Kind = (MassiveModuleKind)99 }; Validate("module-invalid-kind", "Module", module, module.Validate);
module = new MassiveModuleState { Id = "x", Condition = float.NaN }; Validate("module-nan-condition", "Module", module, module.Validate);
module = new MassiveModuleState { Id = "x", Condition = 1.01f }; Validate("module-condition-high", "Module", module, module.Validate);
module = new MassiveModuleState { Id = "x", MassEach = float.PositiveInfinity }; Validate("module-infinite-scalar", "Module", module, module.Validate);
module = new MassiveModuleState { Id = "x", EffectiveRange = 2000.01f }; Validate("module-range-high", "Module", module, module.Validate);

var loadout = new MassiveCombatLoadout(); Validate("loadout-default-valid", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { Acceleration = float.NaN }; Validate("loadout-nan-scalar", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { MassPerShip = -1 }; Validate("loadout-negative-scalar", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { HullPerShip = 0 }; Validate("loadout-zero-hull", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { WarpSpoolSeconds = 0 }; Validate("loadout-zero-spool", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { ModuleSlotCapacity = -1 }; Validate("loadout-negative-capacity", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { MaximumModuleMass = -1 }; Validate("loadout-negative-mass-bound", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { Weapons = Enumerable.Range(0, 33).Select(i => new MassiveWeaponGroup { Id = "w" + i }).ToList() };
Validate("loadout-weapon-count", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { Modules = Enumerable.Range(0, 33).Select(i => new MassiveModuleState { Id = "m" + i }).ToList() };
Validate("loadout-module-count", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { Weapons = new() { new() { Id = " " } }, Modules = new() { new() { Id = " " } } };
Validate("loadout-child-weapon-precedes-module", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { ModuleSlotCapacity = 1, Modules = new() { new() { Id = "m", Slots = 1, InstalledCount = 2 } } };
Validate("loadout-slot-budget", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { MaximumModuleMass = 1, Modules = new() { new() { Id = "m", MassEach = 1, InstalledCount = 2 } } };
Validate("loadout-mass-budget", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { ModuleSlotCapacity = int.MaxValue, MaximumModuleMass = float.MaxValue,
    Modules = new() { new() { Id = "a", Slots = int.MaxValue }, new() { Id = "b", Slots = 1 } } };
Validate("loadout-checked-sum-overflow", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { ModuleSlotCapacity = 0,
    Modules = new() { new() { Id = "m", Slots = int.MaxValue, InstalledCount = 2 } } };
Validate("loadout-unchecked-product-wrap", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { ModuleSlotCapacity = int.MaxValue, MaximumModuleMass = float.MaxValue,
    Modules = new() { new() { Id = "a", Slots = int.MaxValue }, new() { Id = "b", Slots = 1 }, new() { Id = " " } } };
Validate("loadout-validates-all-modules-before-sum", "Loadout", loadout, loadout.Validate);
loadout = new MassiveCombatLoadout { MaximumModuleMass = 100_000_000,
    Modules = Enumerable.Range(0, 32).Select(i => new MassiveModuleState { Id = "m" + i, MassEach = i == 0 ? 100_000_000 : 1 }).ToList() };
Validate("loadout-mass-sum-double-accumulator", "Loadout", loadout, loadout.Validate);

var vessel = new MassiveVesselState { Id = 1, Name = "V", DesignId = "D", HullFraction = 0,
    EngineFraction = 1, SensorFraction = .5f, WarpDriveFraction = 1, ReactorFraction = 0,
    InterdictorFraction = 1, BattlesFought = -1, ConfirmedKills = -1 };
Validate("vessel-valid-condition-boundaries", "Vessel", vessel, vessel.Validate);
vessel = new MassiveVesselState { Id = 1, Name = " ", DesignId = "D" }; Validate("vessel-whitespace-name", "Vessel", vessel, vessel.Validate);
vessel = new MassiveVesselState { Id = 1, Name = "\u00a0", DesignId = "D" }; Validate("vessel-nbsp-name", "Vessel", vessel, vessel.Validate);
vessel = new MassiveVesselState { Id = 0, Name = "V", DesignId = "D" }; Validate("vessel-zero-id", "Vessel", vessel, vessel.Validate);
vessel = new MassiveVesselState { Id = 1, Name = "V", DesignId = "\t" }; Validate("vessel-whitespace-design", "Vessel", vessel, vessel.Validate);
vessel = new MassiveVesselState { Id = 1, Name = "V", DesignId = "\u2003" }; Validate("vessel-em-space-design", "Vessel", vessel, vessel.Validate);
vessel = new MassiveVesselState { Id = 1, Name = "V", DesignId = "D", SensorFraction = float.NaN };
Validate("vessel-nan-condition", "Vessel", vessel, vessel.Validate);
vessel = new MassiveVesselState { Id = 1, Name = "V", DesignId = "D", ReactorFraction = 1.01f };
Validate("vessel-condition-high", "Vessel", vessel, vessel.Validate);

var projectionFleets = new[] { Fleet(FleetRole.Science, 3, false), Fleet(FleetRole.Logistics, 2), Fleet((FleetRole)99, -1) };
Add("economic-projection-order-and-unknown-role", "Projection", new { Fleets = Clone(projectionFleets) },
    () => projectionFleets.Select(item => new { item.CivilizationId, Role = (int)item.Role, item.IsActive }).ToArray());

File.WriteAllText(args[0], JsonSerializer.Serialize(new {
    Format = "stellar-fleet-state-oracle-v2", Cases = cases
}, options) + Environment.NewLine);
