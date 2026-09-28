using System.Text.Json;
using System.Text.Json.Nodes;
using Game.Campaign;
using Game.Persistence;
using Game.Simulation;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.CoreRuntime.Validation;

internal static class SurfaceConstructionValidation
{
    public static void ValidateSurfaceFeedbackReadModel()
    {
        var galaxy = CreateGalaxy();
        var colony = Home(galaxy);
        colony.PopulationMillions = 100;
        var foodShort = new ColonySustenanceCapacitySnapshot(0, 0, 0, 0, 0, 0, 80, 120, 120, 80, .8, "food");
        colony.StoredFoodPopulationDaysMillions = 200;
        colony.StoredWaterPopulationDaysMillions = 0;
        var buffered = ColonySurfaceFeedbackReadModel.GetSustenance(galaxy, colony, foodShort);
        Require(buffered.IsBuffered && !buffered.IsDeclining && buffered.LimitingSupply == "food" && buffered.FoodDaysUntilDepletion > 9.9,
            "food capacity deficit with reserves was presented as an immediate decline");
        colony.StoredFoodPopulationDaysMillions = 0;
        var depleted = ColonySurfaceFeedbackReadModel.GetSustenance(galaxy, colony, foodShort);
        Require(depleted.IsDeclining && depleted.LimitingSupply == "food", "depleted food shortage did not identify actual decline");
        var waterShort = foodShort with { FoodCapacityMillions = 120, WaterCapacityMillions = 80, SupportedPopulationMillions = 80, LimitingSupply = "potable water" };
        colony.StoredWaterPopulationDaysMillions = 0;
        var water = ColonySurfaceFeedbackReadModel.GetSustenance(galaxy, colony, waterShort);
        Require(water.IsDeclining && water.LimitingSupply == "potable water", "water shortage did not win the effective limiter");
        var capacityWaterButFoodDepleted = foodShort with { WaterCapacityMillions = 70, SupportedPopulationMillions = 70, LimitingSupply = "potable water" };
        colony.StoredFoodPopulationDaysMillions = 0;
        colony.StoredWaterPopulationDaysMillions = 1_000;
        var actualFood = ColonySurfaceFeedbackReadModel.GetSustenance(galaxy, colony, capacityWaterButFoodDepleted);
        Require(actualFood.IsDeclining && actualFood.LimitingSupply == "food" && actualFood.RecoveryAction.Contains("agriculture"),
            "recovery followed capacity instead of the actually unsupported food supply");
        colony.StoredFoodPopulationDaysMillions = 10;
        var partialDay = ColonySurfaceFeedbackReadModel.GetSustenance(galaxy, colony, foodShort);
        Require(partialDay.IsDeclining && partialDay.FoodDaysUntilDepletion > .49 && partialDay.FoodDaysUntilDepletion < .51 &&
            colony.StoredFoodPopulationDaysMillions == 10, "next-day reserve preview missed partial-day exhaustion or mutated state");
        colony.Kind = SettlementKind.ResourceOutpost;
        var outpost = ColonySurfaceFeedbackReadModel.GetSustenance(galaxy, colony, foodShort);
        Require(!outpost.IsBuffered && !outpost.IsDeclining && outpost.Status.StartsWith("Outpost support limit"),
            "outpost feedback predicted ordinary-colony population behavior");
        colony.Kind = SettlementKind.Colony;

        Place(galaxy, "science_lab", 100, 100, 0);
        Place(galaxy, "fabricator", -100, -100, 0);
        var first = colony.SurfaceBuildings.First();
        var beforeProgress = first.IndustryProgress;
        var producingZeroStock = ColonySurfaceFeedbackReadModel.GetConstruction(first,
            ColonySurfaceFeedbackReadModel.GetConstructionContext(galaxy, galaxy.PlayerCivilizationId, 0, true));
        Require(producingZeroStock.Status.Contains("newly produced") && producingZeroStock.SharedSiteDemand > 0 &&
            first.IndustryProgress == beforeProgress, "zero stock with production was presented as a deadlock or mutated construction");
        var shared = ColonySurfaceFeedbackReadModel.GetConstruction(first,
            ColonySurfaceFeedbackReadModel.GetConstructionContext(galaxy, galaxy.PlayerCivilizationId, 30, false));
        Require(shared.Status.Contains("shared") && shared.MinimumDaysRemaining > 0,
            "competing authorized sites did not produce an observational contention explanation");
    }

    public static void ValidateHubCapacityAndUpgrade() => InTemporaryDirectory(directory =>
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var colony = Home(galaxy);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        Require(colony.SurfaceHubLevel == 2 && SurfaceConstruction.GetBuildingCapacity(colony) == 32,
            "new homeworld did not begin with a level-2 planetary hub");

        colony.SurfaceHubLevel = 1;
        economy.Credits = 500;
        economy.Industry = 1_000;
        var capabilities = new TestConstructionCapabilityView();
        Require(!SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities).Accepted,
            "level-1 command center bypassed its industrial development requirement");
        galaxy.ConstructionStates.Single(item => item.CivilizationId == player)
            .CompletedProjectIds.Add("industrial_automation");
        var first = SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities);
        Require(first.Accepted && colony.SurfaceHubLevel == 1 && colony.SurfaceHubUpgradeDaysRemaining > 0,
            "hub authorization granted capacity before construction");
        Require(!SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities).Accepted, "duplicate hub expansion charged twice");
        var pendingPath = Path.Combine(directory, "pending-hub.json");
        new CampaignSaveService().Save(pendingPath, galaxy, 0);
        var pending = new CampaignSaveService().Load(pendingPath).Galaxy;
        var restoredHub = pending.Colonies.Single(c => c.Id == colony.Id);
        Require(restoredHub.SurfaceHubLevel == 1 && restoredHub.SurfaceHubUpgradeDaysRemaining == colony.SurfaceHubUpgradeDaysRemaining,
            "save/load lost the pending hub upgrade or granted its capacity early");
        SurfaceConstruction.Advance(pending, player, 0, 0);
        Require(restoredHub.SurfaceHubLevel == 1, "paused pending hub granted capacity");
        SurfaceConstruction.Advance(galaxy, player, 0, colony.SurfaceHubUpgradeDaysRemaining);
        Require(first.Accepted && colony.SurfaceHubLevel == 2 && SurfaceConstruction.GetBuildingCapacity(colony) == 32,
            "level-1 command center did not expand to 32 modules");
        Near(economy.Credits, 440, "first hub upgrade charged the wrong currency amount");
        Near(economy.Industry, 750, "first hub upgrade consumed the wrong material amount");

        Require(!SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities).Accepted,
            "level-2 planetary hub bypassed Orbital Manufacturing research");
        capabilities.Grant("orbital_industry");
        var second = SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities);
        Require(second.Accepted && colony.SurfaceHubLevel == 2, "second expansion skipped construction time");
        SurfaceConstruction.Advance(galaxy, player, 0, colony.SurfaceHubUpgradeDaysRemaining);
        Require(second.Accepted && colony.SurfaceHubLevel == 3 && SurfaceConstruction.GetBuildingCapacity(colony) == 64,
            "level-2 hub did not expand to 64 modules");
        Near(economy.Credits, 300, "second hub upgrade charged the wrong currency amount");
        Near(economy.Industry, 150, "second hub upgrade consumed the wrong material amount");
        Require(!SurfaceConstruction.UpgradeHub(galaxy, player, colony.Id, capabilities).Accepted,
            "maximum-level hub accepted another upgrade");

        var luna = galaxy.Colonies.Single(item => item.CivilizationId == player &&
            item.PlanetaryBodyId == Game.Simulation.Generation.SolCatalogPreset.MoonBodyId);
        luna.SurfaceHubLevel = 2;
        Require(!SurfaceConstruction.UpgradeHub(galaxy, player, luna.Id, capabilities).Accepted &&
            luna.SurfaceHubLevel == 2, "small moon accepted a level-3 regional surface hub");
        Near(SurfaceConstruction.GetConstructionCostMultiplier(galaxy, colony), 1.0,
            "Earth did not retain the baseline surface authorization cost");
        Require(SurfaceConstruction.GetConstructionCostMultiplier(galaxy, luna) > 1.3,
            "airless low-gravity lunar construction did not carry an environment premium");
        var lunarHubCost = SurfaceConstruction.GetHubUpgradeCost(galaxy, luna)!.Value;
        Require(lunarHubCost.CreditCost > 140 && lunarHubCost.IndustryCost > 600,
            "lunar hub expansion did not apply its environment-adjusted funding and material cost");
        var lunarLab = SurfaceBuildingCatalog.Find("science_lab")!;
        var lunarAuthorization = SurfaceConstruction.GetAuthorizationCost(galaxy, luna, lunarLab);
        var beforeLunarOrder = economy.Credits;
        Require(SurfaceConstruction.Place(galaxy, player, luna.Id, lunarLab.Id, 100, 100, 0).Accepted,
            "valid lunar science complex was rejected");
        Near(economy.Credits, beforeLunarOrder - lunarAuthorization,
            "lunar surface authorization did not charge its exact environment-adjusted quote");

        var path = Path.Combine(directory, "hub-upgrade.json");
        var persistence = new CampaignSaveService();
        persistence.Save(path, galaxy, 8.0);
        var restored = Home(persistence.Load(path).Galaxy);
        Require(restored.SurfaceHubLevel == 3 && SurfaceConstruction.GetBuildingCapacity(restored) == 64,
            "hub level and module capacity did not survive save/load");

        var legacy = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        legacy["Galaxy"]!["Colonies"]!.AsArray()
            .Single(item => item!["Id"]!.GetValue<int>() == colony.Id)!.AsObject()
            .Remove("SurfaceHubLevel");
        var legacyPath = Path.Combine(directory, "pre-hub-level.json");
        File.WriteAllText(legacyPath, legacy.ToJsonString());
        var compatible = Home(persistence.Load(legacyPath).Galaxy);
        Require(compatible.SurfaceHubLevel == 3 && SurfaceConstruction.GetBuildingCapacity(compatible) == 64,
            "a pre-hub-level save lost its former 64-module capacity");
    });

    public static void ValidateOperatingShutdown()
    {
        var galaxy = CreateGalaxy();
        var colony = Home(galaxy);
        var playerId = galaxy.PlayerCivilizationId;
        Place(galaxy, "trade_hub", 120, 100, 0);
        var economy = galaxy.Economies.Single(value => value.CivilizationId == playerId);
        economy.Industry = 1_000;
        SurfaceConstruction.Advance(galaxy, playerId, 1_000, 100);
        var building = colony.SurfaceBuildings.Single();
        var operating = SurfaceConstruction.GetOutput(colony);
        Require(operating.CreditsPerDay > 0.0 && operating.UpkeepCreditsPerDay > 0.0,
            "completed trade hub had no operating economy");
        Require(SurfaceConstruction.SetOperatingPriority(galaxy, playerId, colony.Id, building.Id, true).Accepted,
            "completed surface building rejected an operating-priority order");

        var shutdown = SurfaceConstruction.SetEnabled(galaxy, playerId, colony.Id, building.Id, false);
        var stopped = SurfaceConstruction.GetOutput(colony);
        Require(shutdown.Accepted && !building.IsEnabled && stopped.CreditsPerDay == 0.0 &&
            stopped.UpkeepCreditsPerDay == 0.0 && stopped.WorkforceDemandMillions == 0.0,
            "shutdown building retained output, upkeep, or workers");
        var savePath = Path.Combine(Path.GetTempPath(), $"stellar-shutdown-{Guid.NewGuid():N}.json");
        try
        {
            var persistence = new CampaignSaveService();
            persistence.Save(savePath, galaxy, 5.0);
            var loaded = persistence.Load(savePath).Galaxy;
            var loadedBuilding = Home(loaded).SurfaceBuildings.Single();
            Require(!loadedBuilding.IsEnabled && loadedBuilding.OperatingPriority == 1 &&
                SurfaceConstruction.GetOutput(Home(loaded)).UpkeepCreditsPerDay == 0.0,
                "shutdown state, operating priority, or suspended upkeep did not survive save/load");
        }
        finally
        {
            if (File.Exists(savePath)) File.Delete(savePath);
        }
        var restart = SurfaceConstruction.SetEnabled(galaxy, playerId, colony.Id, building.Id, true);
        var restored = SurfaceConstruction.GetOutput(colony);
        Require(restart.Accepted && building.IsEnabled && restored.CreditsPerDay == operating.CreditsPerDay &&
            restored.UpkeepCreditsPerDay == operating.UpkeepCreditsPerDay,
            "restarted building did not restore its operating economy");
    }

    public static void ValidatePhysicalMaintenanceAndRepair() => InTemporaryDirectory(directory =>
    {
        var galaxy = CreateGalaxy();
        var colony = Home(galaxy);
        var player = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        Place(galaxy, "fabricator", 120, 100, 0);
        economy.Industry = 1_000;
        SurfaceConstruction.Advance(galaxy, player, 1_000, 100);
        var building = colony.SurfaceBuildings.Single();
        Near(SurfaceConstruction.GetOutput(colony).IndustryPerDay, 1.0,
            "newly completed infrastructure did not begin at full output");

        SurfaceConstruction.AdvanceCondition(galaxy, colony, 0.0, 100);
        Near(building.Condition, .8, "unfunded active infrastructure did not wear at the authored daily rate");
        Near(SurfaceConstruction.GetOutput(colony).IndustryPerDay, .9,
            "physical wear did not reduce effective building output");
        Require(SurfaceConstruction.GetRepairIndustryCost(building) == 23,
            "repair quote did not reflect the building's lost condition and construction scale");

        var mars = galaxy.Colonies.Single(item => item.CivilizationId == player && item.Name == "Mars");
        var marsBuilding = new SurfaceBuildingState
        {
            Id = 1, TypeId = "fabricator", X = 120, Z = 100,
            IndustryProgress = SurfaceBuildingCatalog.Find("fabricator")!.IndustryCost,
            IsComplete = true,
        };
        mars.SurfaceBuildings.Add(marsBuilding);
        var harshWear = SurfaceConstruction.GetEnvironmentalWearMultiplier(galaxy, mars);
        Require(harshWear > 1.0, "hostile Mars environment did not increase deferred-maintenance exposure");
        SurfaceConstruction.AdvanceCondition(galaxy, mars, 0.0, 100);
        Near(marsBuilding.Condition, 1.0 - .2 * harshWear,
            "hostile-world wear did not use the exact displayed environmental multiplier");
        Require(marsBuilding.Condition < building.Condition,
            "hostile-world infrastructure did not wear faster than the Earth fixture");
        var maintainedMarsCondition = marsBuilding.Condition;
        SurfaceConstruction.AdvanceCondition(galaxy, mars, 1.0, 100);
        Near(marsBuilding.Condition, maintainedMarsCondition,
            "fully funded hostile-world maintenance did not stabilize condition");

        Require(SurfaceConstruction.SetEnabled(galaxy, player, colony.Id, building.Id, false).Accepted,
            "maintenance fixture could not shut down its building");
        SurfaceConstruction.AdvanceCondition(galaxy, colony, 0.0, 100);
        Near(building.Condition, .8, "a safely shut-down building continued accumulating operating wear");
        Require(SurfaceConstruction.SetEnabled(galaxy, player, colony.Id, building.Id, true).Accepted,
            "maintenance fixture could not restart its building");
        SurfaceConstruction.AdvanceCondition(galaxy, colony, 0.0, 400);
        Near(building.Condition, 0.0, "prolonged unfunded operation did not exhaust building condition");
        var failed = SurfaceConstruction.GetOutput(colony);
        Require(failed.IndustryPerDay == 0.0 && !failed.StaffedBuildingIds.Contains(building.Id) &&
            !failed.PoweredBuildingIds.Contains(building.Id),
            "failed infrastructure retained output, workers, or power allocation");

        var fullRepairCost = SurfaceConstruction.GetRepairIndustryCost(building);
        Require(fullRepairCost == 113, "full repair material quote changed unexpectedly");
        economy.Industry = fullRepairCost - 1;
        Require(!SurfaceConstruction.Repair(galaxy, player, colony.Id, building.Id).Accepted && building.Condition == 0.0,
            "an unaffordable repair changed building condition");
        economy.Industry = fullRepairCost;
        var repaired = SurfaceConstruction.Repair(galaxy, player, colony.Id, building.Id);
        Require(repaired.Accepted && building.Condition == 1.0 && economy.Industry == 0.0 &&
            SurfaceConstruction.GetOutput(colony).IndustryPerDay == 1.0,
            "paid repair did not consume exact materials and restore full output");

        building.Condition = .61;
        var path = Path.Combine(directory, "building-condition.json");
        new CampaignSaveService().Save(path, galaxy, 3.0);
        var restored = Home(new CampaignSaveService().Load(path).Galaxy).SurfaceBuildings.Single();
        Near(restored.Condition, .61, "building condition did not survive save and load");

        var legacy = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        legacy["Galaxy"]!["Colonies"]!.AsArray()
            .Single(item => item!["Id"]!.GetValue<int>() == colony.Id)!["SurfaceBuildings"]![0]!.AsObject()
            .Remove("Condition");
        var legacyPath = Path.Combine(directory, "pre-condition.json");
        File.WriteAllText(legacyPath, legacy.ToJsonString());
        var migrated = Home(new CampaignSaveService().Load(legacyPath).Galaxy).SurfaceBuildings.Single();
        Near(migrated.Condition, 1.0, "a save created before physical condition support did not migrate safely");
    });

    public static void ValidateEssentialServicePriority()
    {
        var galaxy = CreateGalaxy();
        var colony = Home(galaxy);
        var player = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        Place(galaxy, "science_lab", 100, 100, 0);
        Place(galaxy, "water_reclamation", -100, 100, 0);
        economy.Industry = 1_000;
        SurfaceConstruction.Advance(galaxy, player, 1_000, 100);
        var lab = colony.SurfaceBuildings.Single(item => item.TypeId == "science_lab");
        var water = colony.SurfaceBuildings.Single(item => item.TypeId == "water_reclamation");
        var protectedOutput = SurfaceConstruction.GetOutput(colony);
        Require(protectedOutput.PoweredBuildingIds.SetEquals(new[] { water.Id }) &&
            protectedOutput.WaterCapacityMillions == 2000.0 && protectedOutput.SciencePerDay == 0.0,
            "automatic grid order did not protect potable-water service from discretionary research load");

        Require(SurfaceConstruction.SetOperatingPriority(galaxy, player, colony.Id, lab.Id, true).Accepted,
            "player could not override the automatic essential-service order");
        var overridden = SurfaceConstruction.GetOutput(colony);
        Require(overridden.PoweredBuildingIds.SetEquals(new[] { lab.Id }) &&
            overridden.SciencePerDay == 1.0 && overridden.WaterCapacityMillions == 0.0,
            "explicit player priority did not outrank the automatic essential-service order");
    }

    public static void ValidatePowerStorage() => InTemporaryDirectory(directory =>
    {
        var galaxy = CreateGalaxy();
        var colony = Home(galaxy);
        var player = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        Place(galaxy, "power_generator", 100, 100, 0);
        Place(galaxy, "grid_battery", -100, 100, 0);
        Place(galaxy, "science_lab", 100, -100, 0);
        Place(galaxy, "fabricator", -100, -100, 0);
        economy.Industry = 5_000;
        SurfaceConstruction.Advance(galaxy, player, 5_000, 100);
        var generator = colony.SurfaceBuildings.Single(item => item.TypeId == "power_generator");
        var battery = colony.SurfaceBuildings.Single(item => item.TypeId == "grid_battery");

        var charging = SurfaceConstruction.GetOutput(colony, 5.0);
        Require(charging.StorageChargePerDay == 2.0 && charging.StorageDischargePerDay == 0.0,
            "surplus generator output did not produce a bounded battery charge rate");
        SurfaceConstruction.AdvancePowerStorage(colony, charging, 5.0);
        Near(battery.StoredPowerDays, 9.0, "battery charging ignored elapsed energy or conversion loss");

        Require(SurfaceConstruction.SetEnabled(galaxy, player, colony.Id, battery.Id, false).Accepted,
            "storage fixture could not shut down the charged battery");
        var suspended = SurfaceConstruction.GetOutput(colony, 1.0);
        Near(suspended.StoredPowerDays, 9.0, "shut-down battery hid or discarded its stored energy");
        Near(suspended.PowerStorageCapacityDays, 12.0, "shut-down battery hid its physical capacity");
        Require(suspended.StorageChargePerDay == 0.0 && suspended.StorageDischargePerDay == 0.0,
            "shut-down battery continued participating in the power grid");
        Require(SurfaceConstruction.SetEnabled(galaxy, player, colony.Id, battery.Id, true).Accepted,
            "storage fixture could not restart the charged battery");

        Require(SurfaceConstruction.SetEnabled(galaxy, player, colony.Id, generator.Id, false).Accepted,
            "storage fixture could not shut down generation");
        var discharging = SurfaceConstruction.GetOutput(colony, 1.0);
        Require(discharging.PoweredBuildingIds.Contains(colony.SurfaceBuildings.Single(item => item.TypeId == "science_lab").Id) &&
            discharging.PoweredBuildingIds.Contains(colony.SurfaceBuildings.Single(item => item.TypeId == "fabricator").Id) &&
            discharging.StorageDischargePerDay == 2.0,
            "stored energy did not bridge the represented two-power generation shortfall");
        SurfaceConstruction.AdvancePowerStorage(colony, discharging, 1.0);
        Near(battery.StoredPowerDays, 9.0 - 2.0 / SurfaceConstruction.PowerStorageEfficiency,
            "battery discharge failed to consume stored energy with conversion loss");

        economy.Industry = 500;
        Place(galaxy, "trade_hub", 180, 0, 0);
        SurfaceConstruction.Advance(galaxy, player, 500, 100);
        Require(EconomySimulation.GetCreditFlow(galaxy, player).TradeRevenuePerDay == .08,
            "one-day treasury view did not recognize battery-supported trade");
        var longInterval = SurfaceConstruction.GetOutput(colony, 4.0);
        Require(longInterval.StorageDischargePerDay == 0.0 && longInterval.PoweredBuildingIds.Count == 2,
            "a large simulation interval created enough free battery energy for an unsustainable load");
        Require(EconomySimulation.GetCreditFlow(galaxy, player, powerIntervalDays: 4.0).TradeRevenuePerDay == 0.0,
            "long-step treasury flow overstated trade that stored energy could not sustain");

        var path = Path.Combine(directory, "grid-storage.json");
        new CampaignSaveService().Save(path, galaxy, 8.0);
        var restored = Home(new CampaignSaveService().Load(path).Galaxy).SurfaceBuildings
            .Single(item => item.TypeId == "grid_battery");
        Near(restored.StoredPowerDays, battery.StoredPowerDays, "stored grid energy did not survive save and load");

        var legacy = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        legacy["Galaxy"]!["Colonies"]!.AsArray()
            .Single(item => item!["Id"]!.GetValue<int>() == colony.Id)!["SurfaceBuildings"]!.AsArray()
            .Single(item => item!["TypeId"]!.GetValue<string>() == "grid_battery")!.AsObject()
            .Remove("StoredPowerDays");
        var legacyPath = Path.Combine(directory, "pre-storage.json");
        File.WriteAllText(legacyPath, legacy.ToJsonString());
        var migrated = Home(new CampaignSaveService().Load(legacyPath).Galaxy).SurfaceBuildings
            .Single(item => item.TypeId == "grid_battery");
        Near(migrated.StoredPowerDays, 0.0, "pre-storage save did not migrate a battery to a safe empty state");
    });

    public static void ValidateFreePlacementAndAuthority()
    {
        var galaxy = CreateGalaxy();
        var colony = Home(galaxy);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == galaxy.PlayerCivilizationId);
        var industry = economy.Industry;
        Place(galaxy, "science_lab", 100.125f, -80.375f, -37.5f);
        var placed = colony.SurfaceBuildings.Single();
        Require(placed.X == 100.125f && placed.Z == -80.375f && placed.RotationDegrees == 322.5f &&
            placed.IndustryProgress == 0 && !placed.IsComplete && economy.Industry == industry,
            "free placement snapped coordinates, completed instantly, or charged industry before construction");
        var stage = SurfaceConstruction.GetConstructionStage(placed);
        Require(stage.Id == "preparation" && stage.PhaseProgress == 0.0 && stage.OverallProgress == 0.0 &&
            stage.RemainingMaterials == 400.0,
            "new construction did not expose its physical preparation stage and remaining materials");
        var snapshot = JsonSerializer.Serialize(colony.SurfaceBuildings);
        foreach (var invalid in new (string Type, float X, float Z, float Yaw)[]
        {
            ("missing_building", -100, 100, 0), ("science_lab", 0, 0, 0),
            ("science_lab", 510, 0, 0), ("science_lab", float.NaN, 90, 0),
            ("science_lab", 90, float.PositiveInfinity, 0), ("science_lab", 90, 90, float.NaN),
            ("power_generator", 100.25f, -80.25f, 15),
        })
            Require(!SurfaceConstruction.Place(galaxy, galaxy.PlayerCivilizationId, colony.Id,
                invalid.Type, invalid.X, invalid.Z, invalid.Yaw).Accepted, "invalid placement was accepted");
        var foreign = galaxy.Colonies.First(item => item.CivilizationId != galaxy.PlayerCivilizationId);
        Require(!SurfaceConstruction.Place(galaxy, galaxy.PlayerCivilizationId, foreign.Id,
            "power_generator", 100, 100, 0).Accepted && foreign.SurfaceBuildings.Count == 0,
            "player placed construction into another faction's colony");
        Require(!SurfaceConstruction.Place(galaxy, galaxy.PlayerCivilizationId, int.MaxValue,
            "power_generator", 100, 100, 0).Accepted, "missing colony accepted construction");
        var earthId = colony.PlanetaryBodyId;
        colony.PlanetaryBodyId = galaxy.PlanetaryBodies.Single(body => body.SystemId == SolCatalogPreset.SystemId && body.Name == "Jupiter").Id;
        Require(!SurfaceConstruction.Place(galaxy, galaxy.PlayerCivilizationId, colony.Id,
            "power_generator", -100, 100, 0).Accepted, "gas giant accepted a ground building");
        colony.PlanetaryBodyId = earthId;
        Require(JsonSerializer.Serialize(colony.SurfaceBuildings) == snapshot && economy.Industry == industry,
            "rejected placement changed an existing site or spent industry");
        SurfaceConstruction.Validate(colony);
    }

    public static void ValidateRateBudgetAndPause()
    {
        var galaxy = CreateGalaxy();
        var colony = Home(galaxy);
        var player = galaxy.PlayerCivilizationId;
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        Place(galaxy, "power_generator", 100.125f, 80.75f, 0);
        Place(galaxy, "science_lab", -100.5f, 85.25f, 15);
        economy.Industry = 20;
        SurfaceConstruction.Advance(galaxy, player, 1000, 0.5);
        Near(economy.Industry, 0, "construction overspent its available stock");
        Near(colony.SurfaceBuildings.Sum(item => item.IndustryProgress), 20, "spent industry vanished or multiplied");
        Require(colony.SurfaceBuildings.All(item => item.IndustryProgress > 0 && item.IndustryProgress <= 15 && !item.IsComplete),
            "one site starved or exceeded its half-day build rate");
        var beforePause = JsonSerializer.Serialize(colony.SurfaceBuildings);
        economy.Industry = 100;
        SurfaceConstruction.Advance(galaxy, player, 100, 0);
        new GalaxySimulationStepCoordinator().Advance(galaxy, 0);
        Require(JsonSerializer.Serialize(colony.SurfaceBuildings) == beforePause && economy.Industry == 100,
            "paused surface construction progressed or spent industry");
        SurfaceConstruction.Advance(galaxy, player, 7.5, 1);
        Near(economy.Industry, 92.5, "construction ignored a constrained shared budget");
        Near(colony.SurfaceBuildings.Sum(item => item.IndustryProgress), 27.5, "budget did not reach the sites exactly once");
        var activeStages = colony.SurfaceBuildings.Select(SurfaceConstruction.GetConstructionStage).ToArray();
        Require(activeStages.All(item => item.Id == "preparation" && item.PhaseProgress > 0.0 &&
                item.RemainingMaterials > 0.0),
            "partial construction did not expose bounded stage progress and remaining material demand");
        foreach (var invalid in new[] { (double.NaN, 1.0), (-1.0, 1.0), (1.0, double.PositiveInfinity), (1.0, -1.0) })
        {
            var before = JsonSerializer.Serialize(colony.SurfaceBuildings);
            Throws<ArgumentOutOfRangeException>(() => SurfaceConstruction.Advance(galaxy, player, invalid.Item1, invalid.Item2));
            Require(JsonSerializer.Serialize(colony.SurfaceBuildings) == before && economy.Industry == 92.5,
                "invalid construction time or budget corrupted the economy");
        }
    }

    public static void ValidateRemovalAuthorityAndEffects()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var colony = Home(galaxy);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        var startingCredits = economy.Credits;

        Place(galaxy, "science_lab", 100, 100, 0);
        var cancelledId = colony.SurfaceBuildings.Single().Id;
        Require(!SurfaceConstruction.Remove(galaxy, player, int.MaxValue, cancelledId).Accepted,
            "a missing colony accepted surface removal");
        var foreign = galaxy.Colonies.First(item => item.CivilizationId != player);
        Require(!SurfaceConstruction.Remove(galaxy, player, foreign.Id, cancelledId).Accepted,
            "the player removed a building from a foreign colony");
        Require(!SurfaceConstruction.Remove(galaxy, player, colony.Id, int.MaxValue).Accepted,
            "a missing surface building accepted removal");
        var cancelled = SurfaceConstruction.Remove(galaxy, player, colony.Id, cancelledId);
        Require(cancelled.Accepted && colony.SurfaceBuildings.Count == 0,
            "an owned incomplete construction site could not be cancelled");
        Near(economy.Credits, startingCredits - 20,
            "cancelling a science lab did not retain half its authorization cost");

        Place(galaxy, "power_generator", -100, 100, 0);
        var generator = colony.SurfaceBuildings.Single();
        economy.Industry = 1000;
        SurfaceConstruction.Advance(galaxy, player, 1000, 100);
        Require(generator.IsComplete && SurfaceConstruction.GetOutput(colony).Supply == 6,
            "demolition fixture did not complete and power the generator");
        var creditsBeforeDemolition = economy.Credits;
        var demolished = SurfaceConstruction.Remove(galaxy, player, colony.Id, generator.Id);
        Require(demolished.Accepted && colony.SurfaceBuildings.Count == 0 && SurfaceConstruction.GetOutput(colony).Supply == 2,
            "demolishing a completed generator did not stop its output");
        Near(economy.Credits, creditsBeforeDemolition,
            "demolishing a completed building incorrectly refunded its authorization cost");
    }

    public static void ValidateUpgradeAuthorityAndEffects() => InTemporaryDirectory(directory =>
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var colony = Home(galaxy);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        Require(!SurfaceConstruction.Place(galaxy, player, colony.Id, "advanced_science_lab", 150, 150, 0).Accepted &&
            colony.SurfaceBuildings.Count == 0, "an upgrade-only building was accepted as direct construction");

        Place(galaxy, "science_lab", 100, 100, 0);
        var lab = colony.SurfaceBuildings.Single();
        var beforeIncomplete = JsonSerializer.Serialize((economy.Credits, economy.Industry, lab));
        Require(!SurfaceConstruction.Upgrade(galaxy, player, colony.Id, lab.Id, AllSurfaceUpgradeCapabilities).Accepted &&
            JsonSerializer.Serialize((economy.Credits, economy.Industry, lab)) == beforeIncomplete,
            "an incomplete building upgrade was accepted or mutated resources");
        Place(galaxy, "power_generator", -100, 100, 0);
        economy.Industry = 1000;
        SurfaceConstruction.Advance(galaxy, player, 1000, 100);
        Require(colony.SurfaceBuildings.All(item => item.IsComplete), "upgrade fixtures did not complete");
        var generator = colony.SurfaceBuildings.Single(item => item.TypeId == "power_generator");
        var beforeResearchLock = JsonSerializer.Serialize((economy.Credits, economy.Industry, generator));
        Require(!SurfaceConstruction.Upgrade(galaxy, player, colony.Id, generator.Id,
                new TestConstructionCapabilityView()).Accepted &&
            JsonSerializer.Serialize((economy.Credits, economy.Industry, generator)) == beforeResearchLock,
            "fusion complex bypassed its Adaptive Research requirement or mutated state on rejection");

        economy.Credits = 49;
        economy.Industry = 500;
        Require(!SurfaceConstruction.Upgrade(galaxy, player, colony.Id, lab.Id, AllSurfaceUpgradeCapabilities).Accepted,
            "upgrade ignored insufficient credits");
        economy.Credits = 500;
        economy.Industry = 319;
        Require(!SurfaceConstruction.Upgrade(galaxy, player, colony.Id, lab.Id, AllSurfaceUpgradeCapabilities).Accepted,
            "upgrade ignored insufficient industry");
        var foreign = galaxy.Colonies.First(item => item.CivilizationId != player);
        Require(!SurfaceConstruction.Upgrade(galaxy, player, foreign.Id, lab.Id, AllSurfaceUpgradeCapabilities).Accepted &&
            !SurfaceConstruction.Upgrade(galaxy, player, int.MaxValue, lab.Id, AllSurfaceUpgradeCapabilities).Accepted,
            "upgrade authority accepted a foreign or missing colony");

        economy.Credits = 500;
        economy.Industry = 500;
        var upgraded = SurfaceConstruction.Upgrade(galaxy, player, colony.Id, lab.Id, AllSurfaceUpgradeCapabilities);
        Require(upgraded.Accepted && lab.TypeId == "science_lab" && lab.UpgradeDaysRemaining > 0,
            "upgrade replaced the old building before completion");
        var pendingPath = Path.Combine(directory, "pending-upgrade.json");
        new CampaignSaveService().Save(pendingPath, galaxy, 0);
        var pending = new CampaignSaveService().Load(pendingPath).Galaxy;
        var pendingLab = Home(pending).SurfaceBuildings.Single(b => b.Id == lab.Id);
        Require(pendingLab.PendingUpgradeTypeId == "advanced_science_lab" && pendingLab.UpgradeDaysRemaining == lab.UpgradeDaysRemaining,
            "save/reload lost the pending upgrade and its timer");
        SurfaceConstruction.Advance(galaxy, player, 0, 0);
        Require(lab.TypeId == "science_lab", "paused upgrade completed");
        SurfaceConstruction.Advance(galaxy, player, 0, lab.UpgradeDaysRemaining);
        Require(upgraded.Accepted && lab.TypeId == "advanced_science_lab" && lab.IsComplete && lab.IndustryProgress == 400,
            "owned completed lab did not become an operational advanced campus");
        Near(economy.Credits, 450, "upgrade charged the wrong credit amount");
        Near(economy.Industry, 180, "upgrade charged the wrong industry amount");
        var output = SurfaceConstruction.GetOutput(colony);
        Require(output.Supply == 6 && output.Demand == 3 && output.SciencePerDay == 2.5 &&
            output.UpkeepCreditsPerDay == .10 && output.PoweredBuildingIds.SetEquals(new[] { 1, 2 }),
            "advanced campus output or power demand did not replace the base lab values");
        var afterUpgrade = JsonSerializer.Serialize((economy.Credits, economy.Industry, lab));
        Require(!SurfaceConstruction.Upgrade(galaxy, player, colony.Id, lab.Id, AllSurfaceUpgradeCapabilities).Accepted &&
            JsonSerializer.Serialize((economy.Credits, economy.Industry, lab)) == afterUpgrade,
            "a terminal upgrade was repeated or mutated resources");

        var path = Path.Combine(directory, "upgraded-surface.json");
        var persistence = new CampaignSaveService();
        persistence.Save(path, galaxy, 12.5);
        var loaded = persistence.Load(path).Galaxy;
        var restored = Home(loaded).SurfaceBuildings.Single(item => item.Id == lab.Id);
        Require(restored.TypeId == "advanced_science_lab" && restored.IsComplete && restored.IndustryProgress == 400 &&
            SurfaceConstruction.GetOutput(Home(loaded)).SciencePerDay == 2.5,
            "advanced building identity or output was lost across save and reload");
    });

    public static void ValidateDerivedSpecialization()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var colony = Home(galaxy);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        Require(SurfaceConstruction.GetSpecialization(colony).Id == "general",
            "an empty colony invented a surface specialization");
        Place(galaxy, "science_lab", 100, 100, 0);
        Place(galaxy, "science_lab", -100, 100, 0);
        Place(galaxy, "science_lab", 100, -100, 0);
        Place(galaxy, "power_generator", -100, -100, 0);
        Require(SurfaceConstruction.GetSpecialization(colony).Id == "general",
            "unfinished construction activated a district");
        economy.Industry = 2000;
        SurfaceConstruction.Advance(galaxy, player, 2000, 100);
        var specialization = SurfaceConstruction.GetSpecialization(colony);
        var output = SurfaceConstruction.GetOutput(colony);
        Require(specialization.Id == "science_lab" && specialization.Active && specialization.CompletedComplexes == 3 &&
            output.Supply == 6 && output.Demand == 6 && output.SciencePerDay == 3.75,
            "three powered labs did not create the exact research-district bonus");

        var removed = SurfaceConstruction.Remove(galaxy, player, colony.Id,
            colony.SurfaceBuildings.First(item => item.TypeId == "science_lab").Id);
        specialization = SurfaceConstruction.GetSpecialization(colony);
        output = SurfaceConstruction.GetOutput(colony);
        Require(removed.Accepted && specialization.Id == "science_lab" && !specialization.Active &&
            specialization.CompletedComplexes == 2 && output.SciencePerDay == 2,
            "demolishing below the threshold did not remove the specialization bonus");
    }

    public static void ValidateHabitatSupportInfrastructure()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var mars = galaxy.Colonies.Single(colony => colony.CivilizationId == player && colony.PlanetaryBodyId == 4);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        var burden = new Game.Simulation.Species.CurrentColonyHabitatSupportBurdenView().Build(galaxy, mars.Id);
        var rawCost = EconomySimulation.GetHabitatSupportCost(burden);
        var before = EconomySimulation.GetCreditFlow(galaxy, player);
        Require(rawCost > 0 && before.HabitatSupportPerDay >= rawCost,
            "Mars did not begin with an explicit life-support burden");
        Require(SurfaceConstruction.Place(galaxy, player, mars.Id, "habitat_complex", 100, 100, 0).Accepted,
            "Mars rejected an affordable habitat complex");
        economy.Industry = 1000;
        SurfaceConstruction.Advance(galaxy, player, 1000, 100);
        var habitat = mars.SurfaceBuildings.Single();
        var output = SurfaceConstruction.GetOutput(mars);
        Require(output.HabitatSupportReduction == .20 && output.PoweredBuildingIds.Contains(habitat.Id),
            "powered habitat complex did not reduce local support burden");
        var after = EconomySimulation.GetCreditFlow(galaxy, player);
        Near(after.HabitatSupportPerDay, before.HabitatSupportPerDay - rawCost * .20,
            "habitat reduction was not applied to the exact occupied-world cost");
        economy.Credits = 500;
        economy.Industry = 500;
        Require(SurfaceConstruction.Upgrade(galaxy, player, mars.Id, habitat.Id, AllSurfaceUpgradeCapabilities).Accepted,
            "completed habitat could not upgrade to a closed-loop arcology");
        SurfaceConstruction.Advance(galaxy, player, 0, habitat.UpgradeDaysRemaining);
        output = SurfaceConstruction.GetOutput(mars);
        Require(output.HabitatSupportReduction == 0 && output.Demand == 3,
            "unpowered advanced habitat incorrectly reduced life-support cost");
        Require(SurfaceConstruction.Place(galaxy, player, mars.Id, "power_generator", -100, 100, 0).Accepted,
            "Mars rejected habitat-supporting generation");
        economy.Industry = 1000;
        SurfaceConstruction.Advance(galaxy, player, 1000, 100);
        Require(SurfaceConstruction.GetOutput(mars).HabitatSupportReduction == .40,
            "powered closed-loop habitat did not provide its advanced reduction");
    }

    public static void ValidateSharedConstructionBudget()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var colony = Home(galaxy);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        var construction = new ConstructionSimulation();
        Require(construction.StartProject(galaxy, player, "research_network").Accepted, "normal opening project unavailable");
        Place(galaxy, "science_lab", 100.25f, 100.5f, 0);
        Place(galaxy, "fabricator", -100.25f, 100.5f, 25);
        economy.Industry = 100;
        construction.Advance(galaxy, new Dictionary<int, double> { [player] = 75 }, 1);
        var project = galaxy.ConstructionStates.Single(item => item.CivilizationId == player);
        var surfaceProgress = colony.SurfaceBuildings.Sum(item => item.IndustryProgress);
        Near(economy.Industry, 25, "project and surface sites spent outside their shared allocation");
        Near(project.ActiveProjectProgress + surfaceProgress, 75, "project/site progress does not conserve industry");
        Require(project.ActiveProjectProgress > 0 && colony.SurfaceBuildings.All(item => item.IndustryProgress is > 0 and <= 30),
            "regular project or surface sites were starved, or a site exceeded its daily rate");
        Require(galaxy.ShipyardStates.Single(item => item.CivilizationId == player).ActiveDesignId is null,
            "ground construction bypassed shipyard prerequisites");
    }

    public static void ValidateWorkforceLimitsOutput()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var colony = Home(galaxy);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        Place(galaxy, "power_generator", 100, 100, 0);
        Place(galaxy, "science_lab", -100, 100, 0);
        economy.Industry = 1000;
        SurfaceConstruction.Advance(galaxy, player, 1000, 100);
        colony.PopulationMillions = .10;

        var constrained = SurfaceConstruction.GetOutput(colony);
        Require(Math.Abs(constrained.WorkforceAvailableMillions - .045) < .000001 &&
            Math.Abs(constrained.WorkforceDemandMillions - .070) < .000001 &&
            constrained.StaffedBuildingIds.SetEquals(new[] { 1 }) &&
            constrained.PoweredBuildingIds.SetEquals(new[] { 1 }) && constrained.SciencePerDay == 0,
            $"insufficient population did not shut the later completed complex down deterministically: " +
            $"available={constrained.WorkforceAvailableMillions} demand={constrained.WorkforceDemandMillions} " +
            $"staffed={string.Join(',', constrained.StaffedBuildingIds)} powered={string.Join(',', constrained.PoweredBuildingIds)} science={constrained.SciencePerDay}");

        var lab = colony.SurfaceBuildings.Single(item => item.TypeId == "science_lab");
        Require(SurfaceConstruction.SetOperatingPriority(galaxy, player, colony.Id, lab.Id, true).Accepted,
            "completed lab could not receive operating priority");
        colony.PopulationMillions = .12;
        var prioritized = SurfaceConstruction.GetOutput(colony);
        Require(prioritized.StaffedBuildingIds.SetEquals(new[] { lab.Id }) &&
            prioritized.PoweredBuildingIds.SetEquals(new[] { lab.Id }) && prioritized.SciencePerDay == 1,
            "operating priority did not redirect scarce workers and hub power to the chosen lab");

        colony.PopulationMillions = .20;
        var supported = SurfaceConstruction.GetOutput(colony);
        Require(supported.StaffedBuildingIds.SetEquals(new[] { 1, 2 }) &&
            supported.PoweredBuildingIds.SetEquals(new[] { 1, 2 }) && supported.SciencePerDay == 1,
            "restored workforce did not return the completed complex to operation");
    }

    public static void ValidatePowerAndEconomy()
    {
        var galaxy = CreateGalaxy();
        var baseline = CreateGalaxy();
        var player = galaxy.PlayerCivilizationId;
        var colony = Home(galaxy);
        var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
        var originalEconomy = baseline.Economies.Single(item => item.CivilizationId == player);
        Place(galaxy, "science_lab", 100, 100, 0);
        Place(galaxy, "fabricator", -100, 100, 0);
        Require(SurfaceConstruction.GetOutput(colony).SciencePerDay == 0 && SurfaceConstruction.GetOutput(colony).IndustryPerDay == 0,
            "unfinished sites produced resources");
        economy.Industry = 5000;
        SurfaceConstruction.Advance(galaxy, player, 5000, 100);
        var unpowered = SurfaceConstruction.GetOutput(colony);
        Require(unpowered.Supply == 2 && unpowered.Demand == 4 && unpowered.SciencePerDay == 1 &&
            unpowered.IndustryPerDay == 0 && unpowered.PoweredBuildingIds.SetEquals(new[] { 1 }),
            "limited hub power did not select completed consumers deterministically");
        Place(galaxy, "power_generator", 100, -100, 0);
        Require(SurfaceConstruction.GetOutput(colony).Supply == 2, "unfinished generator supplied power");
        SurfaceConstruction.Advance(galaxy, player, 5000, 100);
        var powered = SurfaceConstruction.GetOutput(colony);
        Require(powered.Supply == 6 && powered.Demand == 4 && powered.SciencePerDay == 1 && powered.IndustryPerDay == 1 &&
            powered.PoweredBuildingIds.SetEquals(new[] { 1, 2, 3 }), "generator did not power ordinary completed buildings");
        Place(galaxy, "power_generator", -100, -100, 0);
        Place(galaxy, "trade_hub", 180, 0, 0);
        SurfaceConstruction.Advance(galaxy, player, 5000, 100);
        powered = SurfaceConstruction.GetOutput(colony);
        Require(powered.Supply == 10 && powered.Demand == 6 && powered.CreditsPerDay == .08 &&
            powered.UpkeepCreditsPerDay == .16 &&
            powered.PoweredBuildingIds.SetEquals(new[] { 1, 2, 3, 4, 5 }), "trade hub did not join the powered colony economy");
        var creditFlow = EconomySimulation.GetCreditFlow(galaxy, player);
        var baselineCreditFlow = EconomySimulation.GetCreditFlow(baseline, player);
        Near(creditFlow.TradeRevenuePerDay, .08, "cash-flow breakdown omitted powered surface trade");
        Near(creditFlow.SurfaceMaintenancePerDay, .16, "cash-flow breakdown omitted completed surface upkeep");
        Near(creditFlow.NetCreditsPerDay, creditFlow.GrossIncomePerDay - creditFlow.OperatingCostsPerDay,
            "cash-flow breakdown did not reconcile to its displayed net");
        var science = economy.Science;
        var industry = economy.Industry;
        var credits = economy.Credits;
        var originalScience = originalEconomy.Science;
        var originalIndustry = originalEconomy.Industry;
        var originalCredits = originalEconomy.Credits;
        new EconomySimulation().Advance(galaxy, 1);
        new EconomySimulation().Advance(baseline, 1);
        Near(economy.LastCreditsPerSecond, creditFlow.NetCreditsPerDay,
            "displayed cash-flow snapshot disagreed with authoritative economy output");
        Near((economy.Science - science) - (originalEconomy.Science - originalScience), 1,
            "completed powered lab failed to contribute through the authoritative economy");
        Near((economy.Industry - industry) - (originalEconomy.Industry - originalIndustry), 1,
            "completed powered fabricator failed to contribute through the authoritative economy");
        Near((economy.Credits - credits) - (originalEconomy.Credits - originalCredits),
            creditFlow.NetCreditsPerDay - baselineCreditFlow.NetCreditsPerDay,
            "surface jobs, trade and maintenance failed to contribute through the authoritative economy");
    }

    public static void ValidateFramePartitionIndependence()
    {
        (double Project, double Surface) AdvanceDay(int steps)
        {
            var galaxy = CreateGalaxy();
            var player = galaxy.PlayerCivilizationId;
            var economy = galaxy.Economies.Single(item => item.CivilizationId == player);
            var simulation = new ConstructionSimulation();
            Require(simulation.StartProject(galaxy, player, "research_network").Accepted, "opening project unavailable");
            Place(galaxy, "science_lab", 100, 100, 0);
            economy.Industry = 0;
            for (var step = 0; step < steps; step++)
            {
                economy.Industry += 10.0 / steps;
                simulation.Advance(galaxy, new Dictionary<int, double> { [player] = 10.0 / steps }, 1.0 / steps);
            }
            var project = galaxy.ConstructionStates.Single(item => item.CivilizationId == player).ActiveProjectProgress;
            var surface = Home(galaxy).SurfaceBuildings.Single().IndustryProgress;
            Near(project + surface + economy.Industry, 10, "partitioned construction lost its daily industry");
            return (project, surface);
        }
        var reference = AdvanceDay(1);
        Require(reference.Project > 0 && reference.Surface > 0, "daily construction starved a competing project");
        foreach (var steps in new[] { 4, 60 })
        {
            var partitioned = AdvanceDay(steps);
            Near(partitioned.Project, reference.Project, $"ordinary project progress changed with {steps} frames/day");
            Near(partitioned.Surface, reference.Surface, $"surface progress changed with {steps} frames/day");
        }
    }

    public static void ValidateSaveContinuity() => InTemporaryDirectory(directory =>
    {
        var sessions = new CampaignSessionService();
        var session = sessions.CreateNew(2026090817);
        var galaxy = session.Galaxy;
        var plain = new CampaignSaveService();
        var oldPath = Path.Combine(directory, "empty-sol.json");
        plain.Save(oldPath, galaxy, 17);
        Require(Version(oldPath) == 16, "new save omitted its authoritative planetary catalog format");
        var oldCampaign = Path.Combine(directory, "empty-sol-campaign.json");
        sessions.Save(oldCampaign, galaxy, session.Diplomacy, 17);
        Require(Version(oldCampaign) == 17, "current campaign omitted its authoritative catalog wrapper");
        Place(galaxy, "science_lab", 113.125f, -87.375f, 32.5f);
        SurfaceConstruction.Advance(galaxy, galaxy.PlayerCivilizationId, 7.25, 0.5);
        var expected = JsonSerializer.Serialize(Home(galaxy).SurfaceBuildings);
        var surfacePath = Path.Combine(directory, "surface.json");
        plain.Save(surfacePath, galaxy, 17.5);
        var restored = plain.Load(surfacePath);
        Require(Version(surfacePath) == 16 && restored.SimulationDays == 17.5 &&
            JsonSerializer.Serialize(Home(restored.Galaxy).SurfaceBuildings) == expected,
            "standalone save lost exact placement, yaw, partial progress, or clock");
        var campaignPath = Path.Combine(directory, "surface-campaign.json");
        sessions.Save(campaignPath, galaxy, session.Diplomacy, 17.5);
        var campaign = new CampaignStatePersistenceService().Load(campaignPath);
        Require(Version(campaignPath) == 17 && campaign.SimulationDays == 17.5 &&
            JsonSerializer.Serialize(Home(campaign.Galaxy).SurfaceBuildings) == expected &&
            campaign.Galaxy.PlanetaryBodies.SequenceEqual(galaxy.PlanetaryBodies),
            "campaign wrapper lost surface placement or reconstructed a different body catalog");
        SurfaceConstruction.Advance(campaign.Galaxy, galaxy.PlayerCivilizationId, 10, 0.5);
        Near(Home(campaign.Galaxy).SurfaceBuildings.Single().IndustryProgress, 17.25,
            "loaded incomplete construction failed to resume from its saved progress");
    });

    public static void ValidateInvalidSurfaceSaves() => InTemporaryDirectory(directory =>
    {
        var galaxy = CreateGalaxy();
        Place(galaxy, "science_lab", 113.125f, -87.375f, 32.5f);
        SurfaceConstruction.Advance(galaxy, galaxy.PlayerCivilizationId, 7.25, 0.5);
        var path = Path.Combine(directory, "valid.json");
        var persistence = new CampaignSaveService();
        persistence.Save(path, galaxy, 1);
        var valid = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        JsonObject Colony(JsonObject root) => root["Galaxy"]!["Colonies"]!.AsArray()
            .Single(item => item!["Id"]!.GetValue<int>() == Home(galaxy).Id)!.AsObject();
        JsonObject Site(JsonObject root) => Colony(root)["SurfaceBuildings"]![0]!.AsObject();
        var corruptions = new (string Name, Action<JsonObject> Change)[]
        {
            ("missing-collection", root => Colony(root)["SurfaceBuildings"] = null),
            ("missing-x", root => Site(root).Remove("X")),
            ("missing-rotation", root => Site(root).Remove("RotationDegrees")),
            ("missing-progress", root => Site(root).Remove("IndustryProgress")),
            ("unknown-type", root => Site(root)["TypeId"] = "unknown"),
            ("outside-colony", root => Site(root)["X"] = 512),
            ("negative-progress", root => Site(root)["IndustryProgress"] = -1),
            ("overspent-progress", root => Site(root)["IndustryProgress"] = 401),
            ("false-completion", root => Site(root)["IsComplete"] = true),
            ("duplicate-id", root => Colony(root)["SurfaceBuildings"]!.AsArray().Add(Site(root).DeepClone())),
            ("overlapping-sites", root =>
            {
                var duplicate = (JsonObject)Site(root).DeepClone();
                duplicate["Id"] = 99;
                Colony(root)["SurfaceBuildings"]!.AsArray().Add(duplicate);
            }),
            ("invalid-hub-level", root => Colony(root)["SurfaceHubLevel"] = 4),
            ("invalid-operating-priority", root => Site(root)["OperatingPriority"] = 2),
            ("invalid-building-condition", root => Site(root)["Condition"] = 1.1),
            ("invalid-stored-power", root => Site(root)["StoredPowerDays"] = 1.0),
            ("missing-economies", root => root["Galaxy"]!.AsObject().Remove("Economies")),
            ("empty-economies", root => root["Galaxy"]!["Economies"] = new JsonArray()),
            ("downgraded-format", root => root["FormatVersion"] = 10),
        };
        var acceptedCorruptions = new List<string>();
        foreach (var corruption in corruptions)
        {
            var changed = (JsonObject)valid.DeepClone();
            corruption.Change(changed);
            var corruptPath = Path.Combine(directory, corruption.Name + ".json");
            File.WriteAllText(corruptPath, changed.ToJsonString());
            try { persistence.Load(corruptPath); acceptedCorruptions.Add(corruption.Name); }
            catch (InvalidDataException) { }
            catch (JsonException) { } // Required-field decoding is rejected before model validation.
        }
        Require(acceptedCorruptions.Count == 0, "Accepted malformed surface saves: " + string.Join(", ", acceptedCorruptions));
        Require(Home(persistence.Load(path).Galaxy).SurfaceBuildings.Count == 1,
            "negative validation damaged the valid checkpoint");
    });

    private static GalaxyState CreateGalaxy() => new GalaxyGenerator().Generate(2026090817,
        new GalaxyGenerationSettings { SystemCount = 48, PreWarpCivilizationCount = 4, AncientCivilizationCount = 1, Radius = 600 });
    private static ColonyState Home(GalaxyState galaxy) => galaxy.Colonies
        .Where(item => item.CivilizationId == galaxy.PlayerCivilizationId).MaxBy(item => item.PopulationMillions)!;
    private static void Place(GalaxyState galaxy, string type, float x, float z, float rotation) =>
        Require(SurfaceConstruction.Place(galaxy, galaxy.PlayerCivilizationId, Home(galaxy).Id, type, x, z, rotation).Accepted,
            $"valid free placement failed: {type} at {x},{z}");
    private static int Version(string path) => JsonNode.Parse(File.ReadAllText(path))!["FormatVersion"]!.GetValue<int>();
    private static void Near(double actual, double expected, string message) =>
        Require(Math.Abs(actual - expected) < 0.000001, $"{message}; expected={expected:R}, actual={actual:R}");
    private static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new InvalidOperationException($"Expected rejection with {typeof(T).Name}.");
    }
    private static void InTemporaryDirectory(Action<string> verify)
    {
        var directory = Path.Combine(Path.GetTempPath(), "stellar-surface-validation-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try { verify(directory); }
        finally
        {
            var expectedParent = Path.GetFullPath(Path.GetTempPath()).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            Require(string.Equals(Path.GetDirectoryName(Path.GetFullPath(directory)), expectedParent, StringComparison.OrdinalIgnoreCase),
                "refusing test cleanup outside the exact temporary directory");
            Directory.Delete(directory, recursive: true);
        }
    }

    private static readonly TestConstructionCapabilityView AllSurfaceUpgradeCapabilities = new(
        "fusion_power", "additive_manufacturing", "interplanetary_trade_standards", "closed_loop_recycling");

    private sealed class TestConstructionCapabilityView : IConstructionCapabilityView
    {
        private readonly HashSet<string> _capabilities = new(StringComparer.Ordinal);
        public TestConstructionCapabilityView(params string[] capabilityIds) =>
            _capabilities.UnionWith(capabilityIds);
        public void Grant(string capabilityId) => _capabilities.Add(capabilityId);
        public bool HasCivilizationCapability(GalaxyState galaxy, int civilizationId, string capabilityId) =>
            _capabilities.Contains(capabilityId);
    }
}
