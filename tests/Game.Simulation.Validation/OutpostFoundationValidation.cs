using Game.Persistence;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Exploration;
using Game.Simulation.Generation;
using Game.Simulation.Models;

namespace Game.Simulation.Validation;

internal static class OutpostFoundationValidation
{
    public static void ValidateOutpostRulesAndPersistence()
    {
        var galaxy = new GalaxyGenerator().Generate(0x4F55_5450_4F53_54L, new GalaxyGenerationSettings
        {
            SystemCount = 32,
            PreWarpCivilizationCount = 4,
            AncientCivilizationCount = 1,
            Radius = 420.0f,
        });
        var playerId = galaxy.PlayerCivilizationId;
        var outpost = galaxy.Colonies.First(colony => colony.CivilizationId == playerId);
        foreach (var settlement in galaxy.Colonies.Where(colony =>
                     colony.CivilizationId == playerId && colony.SystemId == outpost.SystemId))
            settlement.Kind = SettlementKind.ResourceOutpost;
        outpost.PopulationMillions = 8.0;
        outpost.Infrastructure = 0.15;
        var populationBefore = outpost.PopulationMillions;

        var flow = EconomySimulation.GetCreditFlow(galaxy, playerId, includeResearchOperations: false);
        var expectedCivilianRevenue = galaxy.Colonies
            .Where(colony => colony.CivilizationId == playerId && colony.Kind == SettlementKind.Colony)
            .Sum(colony => Math.Max(0.01, colony.PopulationMillions / 1000.0) * 0.70 *
                Math.Clamp(colony.Infrastructure, 0.1, 5.0) * Math.Clamp(colony.Stability, 0.1, 1.2));
        Require(Math.Abs(flow.ColonyRevenuePerDay - expectedCivilianRevenue) < 0.000001,
            "staffed outpost generated ordinary civilian tax revenue");
        Require(flow.ColonyAdministrationPerDay >= EconomySimulation.OutpostAdministrationCreditsPerDay,
            "staffed outpost had no administration cost");
        new EconomySimulation().Advance(galaxy, 100.0);
        Require(Math.Abs(outpost.PopulationMillions - populationBefore) < 0.000001,
            "rotating outpost crew grew as a civilian population");

        var system = galaxy.Systems.First(candidate => candidate.Id == outpost.SystemId);
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 7000 : galaxy.Fleets.Max(candidate => candidate.Id) + 7000,
            CivilizationId = playerId,
            Name = "Outpost Service Validation Ship",
            Role = FleetRole.Military,
            Position = system.Position,
            CurrentSystemId = system.Id,
            FuelCapacityLightYears = 200.0,
            FuelRemainingLightYears = 10.0,
            IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        new ExplorationSimulation().Advance(galaxy, 0.1);
        Require(Math.Abs(fleet.FuelRemainingLightYears - 100.0) < 0.000001,
            "resource outpost did not provide exactly half-capacity refueling support");

        var resourceBody = galaxy.PlanetaryBodies.First(body => body.HasRareResource && body.Environment.HasSolidSurface && !body.HasPreWarpCivilization);
        var surveyedProfiles = galaxy.PlanetaryBodies.Where(body => body.HasRareResource)
            .Select(ResourceDepositProfile.ForBody).ToArray();
        Require(surveyedProfiles.All(profile => profile.GradeMultiplier is >= 0.70 and <= 1.40 &&
                profile.Accessibility is >= 0.45 and <= 1.0 && profile.ExtractionYieldMultiplier > 0.0) &&
                surveyedProfiles.Select(profile => profile.MaterialName).Distinct().Count() > 1,
            "generated deposits did not expose bounded varied material, grade and accessibility profiles");
        var extractionOutpost = new ColonyState
        {
            Id = galaxy.Colonies.Max(colony => colony.Id) + 1000,
            CivilizationId = playerId,
            SystemId = resourceBody.SystemId,
            PlanetaryBodyId = resourceBody.Id,
            Name = "Power-bound Extraction Validation",
            Kind = SettlementKind.ResourceOutpost,
            PopulationSpeciesId = galaxy.Civilizations.First(civilization => civilization.Id == playerId).SpeciesId,
            PopulationMillions = 8.0,
            Infrastructure = 0.15,
            Stability = 0.85,
        };
        var fabricator = SurfaceBuildingCatalog.Find("fabricator")!;
        extractionOutpost.SurfaceBuildings.Add(new SurfaceBuildingState
        {
            Id = 1,
            TypeId = fabricator.Id,
            X = 80,
            Z = 0,
            IndustryProgress = fabricator.IndustryCost,
            IsComplete = true,
        });
        galaxy.Colonies.Add(extractionOutpost);
        Require(SurfaceConstruction.GetBuildingCapacity(extractionOutpost) == 8,
            "sealed outpost did not expose its small hub module capacity");
        var rejectedTrade = SurfaceConstruction.Place(galaxy, playerId, extractionOutpost.Id, "trade_hub", 140, 0, 0);
        Require(!rejectedTrade.Accepted && rejectedTrade.Message.Contains("freighter", StringComparison.OrdinalIgnoreCase),
            "sealed outpost admitted a civilian trade hub instead of requiring represented freight");
        var operations = ResourceOutpostOperations.GetSnapshot(galaxy, extractionOutpost);
        var deposit = ResourceDepositProfile.ForBody(resourceBody);
        Require(Math.Abs(operations.ExtractionPerDay - deposit.ExtractionYieldMultiplier) < 0.000001 &&
                operations.StorageCapacity == 125.0 && operations.DepositMaterialName == deposit.MaterialName &&
                operations.DepositGrade == deposit.Grade && operations.DepositAccessibility is >= 0.45 and <= 1.0,
            "powered outpost extractor did not expose bounded production and storage");
        Require(operations.RemainingDepositMaterials == ResourceOutpostOperations.InitialDepositReserve(resourceBody) &&
                extractionOutpost.RemainingExtractableMaterials is null,
            "legacy outpost did not resolve a deterministic body-scaled deposit reserve");
        var economy = galaxy.Economies.First(state => state.CivilizationId == playerId);
        economy.LastBaseOperationsFundingFraction = 0.0;
        var unfundedOperations = ResourceOutpostOperations.GetSnapshot(galaxy, extractionOutpost);
        Require(unfundedOperations.ExtractionPerDay == 0.0 &&
                unfundedOperations.Status.Contains("0% operating funding", StringComparison.Ordinal),
            "unfunded outpost still advertised free extraction or used locale-dependent diagnostics");
        economy.LastBaseOperationsFundingFraction = 1.0;
        new EconomySimulation().Advance(galaxy, 200.0 / deposit.ExtractionYieldMultiplier);
        Require(Math.Abs(extractionOutpost.StoredExtractedMaterials - operations.StorageCapacity) < 0.000001,
            "outpost extraction did not stop at represented storage capacity");
        Require(Math.Abs(extractionOutpost.RemainingExtractableMaterials!.Value -
                         (operations.InitialDepositMaterials - operations.StorageCapacity)) < 0.000001,
            "outpost extraction did not consume its represented deposit");

        extractionOutpost.StoredExtractedMaterials = 0.0;
        extractionOutpost.RemainingExtractableMaterials = 10.0;
        new EconomySimulation().Advance(galaxy, 20.0);
        var depleted = ResourceOutpostOperations.GetSnapshot(galaxy, extractionOutpost);
        Require(Math.Abs(extractionOutpost.StoredExtractedMaterials - 10.0) < 0.000001 &&
                extractionOutpost.RemainingExtractableMaterials == 0.0 && depleted.ExtractionPerDay == 0.0 &&
                depleted.Status.Contains("depleted", StringComparison.OrdinalIgnoreCase),
            "finite outpost deposit did not deplete cleanly before storage filled");

        var directory = Path.Combine(Path.GetTempPath(), "stellar-outpost-validation-" + Guid.NewGuid().ToString("N"));
        try
        {
            var path = Path.Combine(directory, "campaign.json");
            var saves = new CampaignSaveService();
            saves.Save(path, galaxy, 100.0);
            var restored = saves.Load(path).Galaxy.Colonies.Single(colony => colony.Id == outpost.Id);
            Require(restored.Kind == SettlementKind.ResourceOutpost,
                "save/load changed a resource outpost into a civilian colony");
            var restoredExtraction = saves.Load(path).Galaxy.Colonies.Single(colony => colony.Id == extractionOutpost.Id);
            Require(Math.Abs(restoredExtraction.StoredExtractedMaterials - 10.0) < 0.000001 &&
                    restoredExtraction.RemainingExtractableMaterials == 0.0,
                "save/load lost the outpost's bounded extracted-material stockpile");
        }
        finally
        {
            if (Directory.Exists(directory)) Directory.Delete(directory, recursive: true);
        }
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
