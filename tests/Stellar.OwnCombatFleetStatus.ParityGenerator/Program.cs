using System.Globalization;
using System.Numerics;
using System.Text.Json;
using System.Text.Json.Serialization;
using Game.Simulation.AI;
using Game.Simulation.Combat;
using Game.Simulation.Combat.Massive;
using Game.Simulation.Models;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
if (args.Length != 1) throw new ArgumentException("Expected output fixture path.");
var json = new JsonSerializerOptions { WriteIndented = false, IncludeFields = true, NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
JsonElement Freeze(object? value) => JsonSerializer.SerializeToElement(value, json);
CivilizationState Civ(int id = 1) => new(id, $"Civ {id}", 10, CivilizationArchetype.Adaptive, CivilizationTraits.Balanced, id == 1, CivilizationDevelopmentStage.WarpCapable);
FleetCombatState Combat(string profile = "patrol_corvette_mk1") => new() { ProfileId = profile, Shields = 20, Armor = 30, Hull = 70, WeaponCooldownRemainingDays = .5, Order = MilitaryOrderType.Hold, RetreatProgressDays = .25 };
FleetState Fleet(int id = 1, int civ = 1, FleetRole role = FleetRole.Military, string name = "Alpha", int? system = 10) => new() { Id = id, CivilizationId = civ, Name = name, Role = role, DesignId = "design", Position = new(1, 2), CurrentSystemId = system, DestinationSystemId = 20, TransitPhase = FleetTransitPhase.InterstellarWarp, TransitOriginSystemId = 9, TransitTargetSystemId = 10, TransitProgress = .2, LocalTransitStart = new(.1f, .2f), LocalTransitPosition = new(.3f, .4f), LocalTransitTarget = new(.5f, .6f), PlannedRouteSystemIds = new() { 20 }, HoldRequested = true, ReturnToBaseRequested = true, ReturnToBaseFailureReason = "history", MissionOrderRevision = 7, DestinationPlanetaryBodyId = 77, PreventAutomaticSettlement = true, SettlementBodyId = 78, SettlementDaysCompleted = 2, ReconnaissanceSystemId = 10, ReconnaissanceDaysCompleted = 3, FreightTargetOutpostId = 30, FreightHomeColonyId = 20, CargoMaterialCapacity = 100, CargoMaterials = 5, StrategicSpeed = 22, MaximumLegRangeLightYears = 50, FuelCapacityLightYears = 100, FuelRemainingLightYears = 80, SensorRange = 90, IsActive = true, EmbarkedPopulationMillions = 4.125, EmbarkedPopulationSpeciesId = "terran_baseline", Combat = Combat(), TacticalVessel = new MassiveVesselState { Id = id, Name = name + " vessel", DesignId = "history-design", IsFlagship = true, IsCarrier = true, IsInterdictor = true, IsStoryShip = true, HullFraction = .8f, EngineFraction = .7f, SensorFraction = .6f, WarpDriveFraction = .5f, ReactorFraction = .4f, InterdictorFraction = .3f, BattlesFought = 9, ConfirmedKills = 8, Destroyed = false, Escaped = true } };
GalaxyState World() => new() { Seed = 39, Systems = Array.Empty<StarSystemState>(), PlanetaryBodies = Array.Empty<PlanetaryBodyState>(), Civilizations = new List<CivilizationState> { Civ(), Civ(2) }, Fleets = new List<FleetState> { Fleet() }, Colonies = new List<ColonyState>(), Economies = Array.Empty<CivilizationEconomyState>(), Technologies = new List<Game.Simulation.Research.TechnologyState>(), ConstructionStates = new List<Game.Simulation.Construction.ConstructionState>(), ShipyardStates = new List<Game.Simulation.Shipbuilding.ShipyardState>(), PlayerCivilizationId = 1, Knowledge = new Game.Simulation.Knowledge.CivilizationKnowledgeState() };
object Snapshot(GalaxyState world) => new { world.Civilizations, world.Fleets };
var cases = new List<object>();
void Add(string name, Action<GalaxyState> arrange, int civilizationId = 1)
{
    var world = World(); arrange(world); var arguments = Freeze(new { World = Snapshot(world), CivilizationId = civilizationId }); var before = Freeze(Snapshot(world));
    OwnCombatFleetStatusView? result = null; Exception? caught = null;
    try { result = OwnCombatFleetStatusBuilder.Build(world, civilizationId); }
    catch (Exception error) { caught = error; }
    cases.Add(new { Name = name, Arguments = arguments, Before = before, Result = result is null ? (JsonElement?)null : Freeze(result), Error = caught is null ? (JsonElement?)null : Freeze(new { Type = caught.GetType().Name, caught.Message }), After = Freeze(Snapshot(world)) });
}

Add("basic-military", _ => { });
Add("no-owned-fleets", g => g.Fleets.Clear());
Add("unknown-civilization", _ => { }, 99);
Add("inactive-filtered", g => g.Fleets[0].IsActive = false);
Add("foreign-filtered", g => g.Fleets[0] = Fleet(civ: 2));
Add("stable-sorted-duplicates", g => { g.Fleets.Clear(); g.Fleets.Add(Fleet(5, name: "first-five")); g.Fleets.Add(Fleet(1, name: "first-one")); g.Fleets.Add(Fleet(5, name: "second-five")); g.Fleets.Add(Fleet(1, name: "second-one")); });
foreach (var role in Enum.GetValues<FleetRole>()) Add("null-combat-role-" + role, g => { g.Fleets[0] = Fleet(role: role); g.Fleets[0].Combat = null; });
foreach (var profile in new[] { "", "   ", "missing-profile" }) Add("fallback-profile-" + (profile.Length == 0 ? "empty" : profile.Trim().Length == 0 ? "blank" : "unknown"), g => g.Fleets[0].Combat = Combat(profile));
foreach (var order in Enum.GetValues<MilitaryOrderType>()) Add("known-order-" + order, g => { g.Fleets[0].Combat!.Order = order; g.Fleets[0].Combat.TargetFleetId = 8; g.Fleets[0].Combat.DefendSystemId = 10; });
Add("unknown-order-normalizes-hold", g => { g.Fleets[0].Combat!.Order = (MilitaryOrderType)99; g.Fleets[0].Combat.TargetFleetId = 8; g.Fleets[0].Combat.DefendSystemId = 10; });
Add("attack-without-target", g => { g.Fleets[0].Combat!.Order = MilitaryOrderType.Attack; g.Fleets[0].Combat.TargetFleetId = null; });
Add("defend-without-system", g => { g.Fleets[0].Combat!.Order = MilitaryOrderType.Defend; g.Fleets[0].Combat.DefendSystemId = null; });
Add("clamp-negative", g => { var s = g.Fleets[0].Combat!; s.Shields = -1; s.Armor = -2; s.Hull = -3; s.WeaponCooldownRemainingDays = -4; s.RetreatProgressDays = -5; });
Add("clamp-over-maximum", g => { var s = g.Fleets[0].Combat!; s.Shields = 999; s.Armor = 999; s.Hull = 999; s.RetreatProgressDays = 999; });
Add("clamp-nan", g => { var s = g.Fleets[0].Combat!; s.Shields = double.NaN; s.Armor = double.NaN; s.Hull = double.NaN; s.WeaponCooldownRemainingDays = double.NaN; s.RetreatProgressDays = double.NaN; });
Add("clamp-positive-infinity", g => { var s = g.Fleets[0].Combat!; s.Shields = double.PositiveInfinity; s.Armor = double.PositiveInfinity; s.Hull = double.PositiveInfinity; s.WeaponCooldownRemainingDays = double.PositiveInfinity; s.RetreatProgressDays = double.PositiveInfinity; });
Add("clamp-negative-infinity", g => { var s = g.Fleets[0].Combat!; s.Shields = double.NegativeInfinity; s.Armor = double.NegativeInfinity; s.Hull = double.NegativeInfinity; s.WeaponCooldownRemainingDays = double.NegativeInfinity; s.RetreatProgressDays = double.NegativeInfinity; });
foreach (var cooldown in new[] { 0d, .00000005, .0000001, .00000011 }) Add("fire-threshold-" + cooldown.ToString("R", CultureInfo.InvariantCulture), g => g.Fleets[0].Combat!.WeaponCooldownRemainingDays = cooldown);
foreach (var hull in new[] { 0d, .00000005, .0000001, .00000011, 94.9999998, 94.9999999, 95d }) Add("hull-threshold-" + hull.ToString("R", CultureInfo.InvariantCulture), g => g.Fleets[0].Combat!.Hull = hull);
Add("retreat-progress-negative", g => { g.Fleets[0].Combat!.Order = MilitaryOrderType.Retreat; g.Fleets[0].Combat.RetreatProgressDays = -1; });
Add("retreat-progress-mid", g => { g.Fleets[0].Combat!.Order = MilitaryOrderType.Retreat; g.Fleets[0].Combat.RetreatProgressDays = .375; });
Add("retreat-progress-over", g => { g.Fleets[0].Combat!.Order = MilitaryOrderType.Retreat; g.Fleets[0].Combat.RetreatProgressDays = 99; });
Add("disengaged-matching-system", g => { g.Fleets[0].Combat!.IsDisengaged = true; g.Fleets[0].Combat.DisengagedSystemId = 10; });
Add("disengaged-mismatched-system", g => { g.Fleets[0].Combat!.IsDisengaged = true; g.Fleets[0].Combat.DisengagedSystemId = 11; });
Add("disengaged-both-null-system", g => { g.Fleets[0].CurrentSystemId = null; g.Fleets[0].Combat!.IsDisengaged = true; g.Fleets[0].Combat.DisengagedSystemId = null; });
Add("disengaged-flag-false", g => { g.Fleets[0].Combat!.IsDisengaged = false; g.Fleets[0].Combat.DisengagedSystemId = 10; });
Add("unarmed-civilian", g => g.Fleets[0].Combat = Combat("civilian_light_v1"));
Add("aggregate-mixed", g => { g.Fleets.Add(Fleet(2, role: FleetRole.Scout, name: "Scout")); g.Fleets[1].Combat = null; g.Fleets.Add(Fleet(3, role: FleetRole.Military, name: "Retreat")); g.Fleets[2].Combat!.Order = MilitaryOrderType.Retreat; g.Fleets.Add(Fleet(4, civ: 2, name: "Foreign")); var inactive = Fleet(5, name: "Inactive"); inactive.IsActive = false; g.Fleets.Add(inactive); });

Exception? nullError = null; try { _ = OwnCombatFleetStatusBuilder.Build(null!, 1); } catch (Exception error) { nullError = error; }
File.WriteAllText(args[0], JsonSerializer.Serialize(new { Schema = "stellar-own-combat-status-oracle-v1", Cases = cases, SourceOnlyObservations = new[] { new { Name = "null-galaxy", Error = nullError is null ? (JsonElement?)null : Freeze(new { Type = nullError.GetType().Name, nullError.Message }) } } }, json) + Environment.NewLine);
