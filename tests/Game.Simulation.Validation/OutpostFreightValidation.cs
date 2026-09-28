using Game.Persistence;
using Game.Simulation.Economy;
using Game.Simulation.Construction;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.Validation;

internal static class OutpostFreightValidation
{
    public static void ValidateRepresentedCollectionAndDelivery()
    {
        var galaxy = new GalaxyGenerator().Generate(0x4652_4549_4748_544CL, new GalaxyGenerationSettings
        {
            SystemCount = 64,
            PreWarpCivilizationCount = 4,
            AncientCivilizationCount = 1,
            Radius = 560.0f,
        });
        var player = galaxy.Civilizations.First(candidate => candidate.Id == galaxy.PlayerCivilizationId);
        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        var homeColony = galaxy.Colonies.First(colony => colony.CivilizationId == player.Id && colony.SystemId == home.Id && colony.Kind == SettlementKind.Colony);
        var resourceBody = galaxy.PlanetaryBodies.First(body => body.SystemId != home.Id && body.HasRareResource && body.Environment.HasSolidSurface && !body.HasPreWarpCivilization);
        var outpost = new ColonyState
        {
            Id = galaxy.Colonies.Max(colony => colony.Id) + 9000,
            CivilizationId = player.Id,
            SystemId = resourceBody.SystemId,
            PlanetaryBodyId = resourceBody.Id,
            Name = "Freight Validation Outpost",
            Kind = SettlementKind.ResourceOutpost,
            PopulationSpeciesId = player.SpeciesId,
            PopulationMillions = 8.0,
            Infrastructure = 0.15,
            Stability = 0.85,
            StoredExtractedMaterials = 80.0,
        };
        var fabricator = SurfaceBuildingCatalog.Find("fabricator")!;
        outpost.SurfaceBuildings.Add(new SurfaceBuildingState
        {
            Id = 1,
            TypeId = fabricator.Id,
            X = 80,
            IndustryProgress = fabricator.IndustryCost,
            IsComplete = true,
        });
        galaxy.Colonies.Add(outpost);
        var design = ShipDesignRegistry.Get(ShipDesignRegistry.BulkFreighterId);
        var freighter = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 8000 : galaxy.Fleets.Max(fleet => fleet.Id) + 8000,
            CivilizationId = player.Id,
            Name = "Lifeline Validation",
            Role = FleetRole.Logistics,
            DesignId = design.Id,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = design.StrategicSpeed,
            MaximumLegRangeLightYears = 10000.0,
            FuelCapacityLightYears = 10000.0,
            FuelRemainingLightYears = 10000.0,
            SensorRange = design.SensorRange,
            CargoMaterialCapacity = design.CargoMaterialCapacity,
            IsActive = true,
        };
        galaxy.Fleets.Add(freighter);
        Require(FreightSimulation.GetEffectiveTransferRatePerDay(freighter, homeColony) ==
            FreightSimulation.BasicHubTransferCapacityPerDay,
            "undeveloped settlement did not use bounded basic-hub freight handling");
        AddCompleted(outpost, "power_generator", 2, -100, 100);
        AddCompleted(outpost, "cargo_terminal", 3, 100, 100);
        AddCompleted(homeColony, "cargo_terminal", 100, 100, 100);
        Require(FreightSimulation.GetEffectiveTransferRatePerDay(freighter, outpost) == 20.0 &&
            FreightSimulation.GetEffectiveTransferRatePerDay(freighter, homeColony) == 20.0,
            "powered cargo terminals did not raise port handling to the vessel limit");
        var outpostTerminal = outpost.SurfaceBuildings.Single(building => building.TypeId == "cargo_terminal");
        outpostTerminal.IsEnabled = false;
        Require(FreightSimulation.GetEffectiveTransferRatePerDay(freighter, outpost) ==
            FreightSimulation.BasicHubTransferCapacityPerDay,
            "shut-down cargo terminal continued providing port handling");
        outpostTerminal.IsEnabled = true;
        var freight = new FreightSimulation();
        var order = freight.IssueCollectionOrder(galaxy, player.Id, freighter.Id, outpost.Id);
        Require(order.Accepted && freighter.FreightHomeColonyId == homeColony.Id && freighter.FreightTargetOutpostId == outpost.Id,
            "freight collection order did not persist its origin and outpost destination");

        var outpostSystem = galaxy.Systems.First(system => system.Id == outpost.SystemId);
        freighter.Position = outpostSystem.Position;
        freighter.CurrentSystemId = outpostSystem.Id;
        FleetRouteOrders.Clear(freighter);
        var economy = galaxy.Economies.First(state => state.CivilizationId == player.Id);
        economy.LastBaseOperationsFundingFraction = 0.0;
        freight.Advance(galaxy, 1.0);
        Require(freighter.CargoMaterials == 0.0 && outpost.StoredExtractedMaterials == 80.0,
            "unfunded freight service transferred physical cargo");
        economy.LastBaseOperationsFundingFraction = 1.0;
        freight.Advance(galaxy, 0.5);
        Require(Math.Abs(freighter.CargoMaterials - 10.0) < 0.000001 &&
            Math.Abs(outpost.StoredExtractedMaterials - 70.0) < 0.000001 &&
            freighter.FreightTargetOutpostId == outpost.Id && freighter.DestinationSystemId is null,
            "freight loading did not respect its represented daily transfer rate");
        freight.Advance(galaxy, 3.5);
        Require(Math.Abs(freighter.CargoMaterials - 80.0) < 0.000001 && outpost.StoredExtractedMaterials == 0.0,
            "freighter did not transfer the exact local stockpile into bounded cargo");
        Require(freighter.DestinationSystemId == home.Id && freighter.FreightTargetOutpostId is null,
            "loaded freighter did not receive its represented return route");

        var directory = Path.Combine(Path.GetTempPath(), "stellar-freight-validation-" + Guid.NewGuid().ToString("N"));
        try
        {
            var path = Path.Combine(directory, "campaign.json");
            var saves = new CampaignSaveService();
            saves.Save(path, galaxy, 200.0);
            var restored = saves.Load(path).Galaxy;
            var restoredFreighter = restored.Fleets.Single(fleet => fleet.Id == freighter.Id);
            Require(restoredFreighter.CargoMaterials == 80.0 && restoredFreighter.FreightHomeColonyId == homeColony.Id,
                "mid-return save/load lost freight cargo or delivery identity");
            var restoredHome = restored.Systems.First(system => system.Id == home.Id);
            restoredFreighter.Position = restoredHome.Position;
            restoredFreighter.CurrentSystemId = restoredHome.Id;
            FleetRouteOrders.Clear(restoredFreighter);
            var restoredEconomy = restored.Economies.First(state => state.CivilizationId == player.Id);
            restoredEconomy.Industry = EconomySimulation.GetIndustryStorageCapacity(restored, player.Id) - 10.0;
            var industryBefore = restoredEconomy.Industry;
            freight.Advance(restored, 1.0);
            Require(Math.Abs(restoredEconomy.Industry - industryBefore - 10.0) < 0.000001 &&
                Math.Abs(restoredFreighter.CargoMaterials - 70.0) < 0.000001 &&
                restoredFreighter.FreightHomeColonyId == homeColony.Id,
                "home storage capacity did not bound freight unloading without discarding cargo");
            freight.Advance(restored, 1.0);
            Require(Math.Abs(restoredFreighter.CargoMaterials - 70.0) < 0.000001,
                "full home storage silently discarded or unloaded physical cargo");
            restoredEconomy.Industry -= 70.0;
            freight.Advance(restored, 3.5);
            Require(Math.Abs(restoredEconomy.Industry - EconomySimulation.GetIndustryStorageCapacity(restored, player.Id)) < 0.000001,
                "delivered freight did not become usable Industry at the bounded port rate");
            Require(restoredFreighter.CargoMaterials == 0.0 && restoredFreighter.FreightHomeColonyId is null,
                "completed freight run did not clear its cargo and mission state");
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    private static void AddCompleted(ColonyState colony, string typeId, int id, float x, float z)
    {
        var definition = SurfaceBuildingCatalog.Find(typeId)!;
        colony.SurfaceBuildings.Add(new SurfaceBuildingState
        {
            Id = id,
            TypeId = definition.Id,
            X = x,
            Z = z,
            IndustryProgress = definition.IndustryCost,
            IsComplete = true,
        });
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
