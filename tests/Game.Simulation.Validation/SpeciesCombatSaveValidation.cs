using System.Numerics;
using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Persistence;
using Game.Simulation.Combat;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class SpeciesCombatSaveValidation
{
    public static void ValidateCombatStateWithinSharedV8AndV7Defaults()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x434F_4D42_5658L,
            new GalaxyGenerationSettings
            {
                SystemCount = 24,
                PreWarpCivilizationCount = 3,
                AncientCivilizationCount = 0,
                Radius = 320.0f,
            });

        galaxy.Fleets.Clear();
        var system = galaxy.Systems[0];
        var civilization = galaxy.Civilizations[0];
        var fleet = new FleetState
        {
            Id = 9100,
            CivilizationId = civilization.Id,
            Name = "V8 Combat Persistence Patrol",
            Role = FleetRole.Military,
            Position = system.Position,
            CurrentSystemId = system.Id,
            StrategicSpeed = 17.0,
            SensorRange = 95.0f,
            IsActive = true,
            Combat = CombatProfileRegistry.CreateInitialState(
                CombatProfileIds.PatrolCorvetteMk1,
                FleetRole.Military),
        };
        galaxy.Fleets.Add(fleet);

        var state = CombatProfileRegistry.EnsureState(fleet);
        state.Shields = 11.0;
        state.Armor = 22.0;
        state.Hull = 33.0;
        state.WeaponCooldownRemainingDays = 0.4;
        state.Order = MilitaryOrderType.Retreat;
        state.RetreatProgressDays = 0.6;
        state.RetreatStarted = true;

        WithTemporaryDirectory(directory =>
        {
            var service = new CampaignSaveService();
            var v8Path = Path.Combine(directory, "combat-v8.json");
            service.Save(v8Path, galaxy, 800.25);
            var loaded = service.Load(v8Path);
            var loadedFleet = loaded.Galaxy.Fleets.Single(candidate => candidate.Id == fleet.Id);
            var loadedState = CombatProfileRegistry.EnsureState(loadedFleet);

            Require(CampaignSaveService.CurrentFormatVersion == 16 && CampaignSaveService.SurfaceFormatVersion == 12 && CampaignSaveService.PresetFormatVersion == 10 && CampaignSaveService.LegacyFormatVersion == 8,
                "Combat must coexist with the shared species/body save-v8 schema.");
            Require(loadedState.ProfileId == state.ProfileId, "save/load changed combat profile identity");
            Require(Math.Abs(loadedState.Shields - 11.0) < 0.000001, "save/load changed shield damage state");
            Require(Math.Abs(loadedState.Armor - 22.0) < 0.000001, "save/load changed armor damage state");
            Require(Math.Abs(loadedState.Hull - 33.0) < 0.000001, "save/load changed hull damage state");
            Require(Math.Abs(loadedState.WeaponCooldownRemainingDays - 0.4) < 0.000001, "save/load changed weapon cooldown");
            Require(loadedState.Order == MilitaryOrderType.Retreat && loadedState.RetreatStarted,
                "save/load changed retreat order state");
            Require(Math.Abs(loadedState.RetreatProgressDays - 0.6) < 0.000001,
                "save/load changed retreat progress");

            var root = JsonNode.Parse(File.ReadAllText(v8Path))?.AsObject()
                ?? throw new InvalidOperationException("could not parse generated combat save");
            root["FormatVersion"] = 7;
            root["Galaxy"]!.AsObject().Remove("PlanetaryBodies");
            var fleets = root["Galaxy"]?["Fleets"]?.AsArray()
                ?? throw new InvalidOperationException("generated combat save had no fleets");
            foreach (var item in fleets)
            {
                var dto = item?.AsObject();
                dto?.Remove("Combat");
                dto?.Remove("EmbarkedPopulationSpeciesId");
                dto?.Remove("DestinationPlanetaryBodyId");
            }
            if (root["Galaxy"]?["Civilizations"] is JsonArray civilizations)
            {
                foreach (var item in civilizations)
                    item?.AsObject().Remove("SpeciesId");
            }
            if (root["Galaxy"]?["Colonies"] is JsonArray colonies)
            {
                foreach (var item in colonies)
                {
                    item?.AsObject().Remove("PopulationSpeciesId");
                    item?.AsObject().Remove("PlanetaryBodyId");
                }
            }
            if (root["Galaxy"]?["ShipyardStates"] is JsonArray shipyards)
            {
                foreach (var item in shipyards)
                {
                    var shipyard = item?.AsObject();
                    shipyard?.Remove("ReservedPopulationSpeciesId");
                    if (shipyard?["QueuedBuilds"] is JsonArray queue)
                    {
                        foreach (var build in queue)
                            build?.AsObject().Remove("ReservedPopulationSpeciesId");
                    }
                }
            }

            var v7Path = Path.Combine(directory, "combatless-v7.json");
            File.WriteAllText(v7Path, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
            var migrated = service.Load(v7Path);
            var migratedFleet = migrated.Galaxy.Fleets.Single(candidate => candidate.Id == fleet.Id);
            var migratedState = CombatProfileRegistry.EnsureState(migratedFleet);
            Require(migratedState.ProfileId == CombatProfileIds.PatrolCorvetteMk1,
                "combatless v7 military fleet did not receive role-based Combat defaults");
            Require(migratedState.Hull > 0.0, "combatless v7 fleet migrated as destroyed");
        });
    }

    private static void WithTemporaryDirectory(Action<string> action)
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            "stellar-continuum-species-combat-validation",
            Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            action(directory);
        }
        finally
        {
            if (Directory.Exists(directory))
                Directory.Delete(directory, recursive: true);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
