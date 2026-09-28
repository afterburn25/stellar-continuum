using System;
using System.Linq;
using Game.Simulation.Construction;
using Game.Simulation.Models;

namespace Game.Simulation.Economy;

public sealed record ResourceOutpostOperationsSnapshot(
    bool IsResourceOutpost,
    bool HasConfirmedDeposit,
    double ExtractionPerDay,
    double StoredMaterials,
    double StorageCapacity,
    double RemainingDepositMaterials,
    double InitialDepositMaterials,
    string DepositMaterialName,
    string DepositGrade,
    double DepositAccessibility,
    double ExtractionYieldMultiplier,
    string Status);

public static class ResourceOutpostOperations
{
    public const double SealedHubStorageCapacity = 25.0;
    public const double StorageCapacityPerFabricationComplex = 100.0;
    public const double MinimumDepositReserve = 3_000.0;
    public const double MaximumDepositReserve = 20_000.0;

    public static double InitialDepositReserve(PlanetaryBodyState body)
    {
        ArgumentNullException.ThrowIfNull(body);
        if (!body.HasRareResource) return 0.0;
        return Math.Clamp(
            MinimumDepositReserve + body.RadiusEarth * 2_500.0 + Math.Sqrt(body.MassEarth) * 1_000.0,
            MinimumDepositReserve,
            MaximumDepositReserve);
    }

    public static ResourceOutpostOperationsSnapshot GetSnapshot(
        GalaxyState galaxy,
        ColonyState settlement,
        double? operatingFundingFraction = null)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(settlement);
        if (settlement.Kind != SettlementKind.ResourceOutpost)
            return new(false, false, 0.0, 0.0, 0.0, 0.0, 0.0,
                "No confirmed deposit", "None", 0.0, 0.0, "Ordinary colony");

        var body = settlement.PlanetaryBodyId is int bodyId
            ? galaxy.PlanetaryBodies.FirstOrDefault(candidate => candidate.Id == bodyId && candidate.SystemId == settlement.SystemId)
            : null;
        var hasDeposit = body?.HasRareResource == true;
        var deposit = body is null
            ? new ResourceDepositProfile("No confirmed deposit", "None", 0.0, 0.0, 0.0, 0.0)
            : ResourceDepositProfile.ForBody(body);
        var initialDeposit = body is null ? 0.0 : InitialDepositReserve(body);
        var remainingDeposit = hasDeposit
            ? settlement.RemainingExtractableMaterials ?? Math.Max(0.0, initialDeposit - settlement.StoredExtractedMaterials)
            : 0.0;
        if (!double.IsFinite(remainingDeposit) || remainingDeposit < 0.0)
            throw new InvalidOperationException($"Resource outpost {settlement.Id} has an invalid remaining deposit reserve.");
        var surface = SurfaceConstruction.GetOutput(settlement);
        var completedFabricators = settlement.SurfaceBuildings.Count(building => building.IsComplete &&
            SurfaceBuildingCatalog.FunctionalFamily(building.TypeId) == "fabricator");
        var capacity = SealedHubStorageCapacity + completedFabricators * StorageCapacityPerFabricationComplex;
        var funding = operatingFundingFraction ?? galaxy.Economies
            .FirstOrDefault(economy => economy.CivilizationId == settlement.CivilizationId)
            ?.LastBaseOperationsFundingFraction ?? 1.0;
        if (!double.IsFinite(funding) || funding is < 0.0 or > 1.0)
            throw new ArgumentOutOfRangeException(nameof(operatingFundingFraction),
                "Operating funding fraction must be finite and between zero and one.");
        var extraction = hasDeposit && remainingDeposit > 0.000001
            ? surface.IndustryPerDay * funding * deposit.ExtractionYieldMultiplier
            : 0.0;
        var status = !hasDeposit
            ? "No confirmed extractable deposit"
            : remainingDeposit <= 0.000001
                ? "Deposit depleted: no extractable material remains"
            : completedFabricators == 0
                ? "Build a fabrication complex to begin extraction"
                : surface.IndustryPerDay > 0.0 && funding < 0.999999
                    ? FormattableString.Invariant($"Extraction running at {funding * 100.0:0}% operating funding")
                : extraction <= 0.0
                    ? "Extraction offline: processing complex lacks power"
                : settlement.StoredExtractedMaterials + 0.0001 >= capacity
                        ? "Storage full: freight service required"
                        : "Extracting to local storage; freight service not yet established";
        return new(true, hasDeposit, extraction, settlement.StoredExtractedMaterials, capacity,
            remainingDeposit, initialDeposit, deposit.MaterialName, deposit.Grade,
            deposit.Accessibility, deposit.ExtractionYieldMultiplier, status);
    }

    public static void Advance(
        GalaxyState galaxy,
        ColonyState settlement,
        double simulationDays,
        double operatingFundingFraction = 1.0)
    {
        if (settlement.Kind != SettlementKind.ResourceOutpost || simulationDays <= 0.0)
            return;
        if (!double.IsFinite(operatingFundingFraction) || operatingFundingFraction is < 0.0 or > 1.0)
            throw new ArgumentOutOfRangeException(nameof(operatingFundingFraction),
                "Operating funding fraction must be finite and between zero and one.");
        var snapshot = GetSnapshot(galaxy, settlement, operatingFundingFraction);
        if (!snapshot.HasConfirmedDeposit) return;
        var freeStorage = Math.Max(0.0, snapshot.StorageCapacity - settlement.StoredExtractedMaterials);
        var extracted = Math.Min(snapshot.RemainingDepositMaterials,
            Math.Min(freeStorage, snapshot.ExtractionPerDay * simulationDays));
        settlement.StoredExtractedMaterials += extracted;
        settlement.RemainingExtractableMaterials = Math.Max(0.0, snapshot.RemainingDepositMaterials - extracted);
    }
}
