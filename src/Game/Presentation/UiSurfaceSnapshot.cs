using System.Collections.Generic;
using Game.Simulation.Economy;

namespace Game.Presentation;

public sealed record UiSurfaceSnapshot(int ColonyId, int BodyId, string PlanetName, string ColonyName,
    SovereignCurrencyDefinition Currency, double Credits, double Industry, double PowerSupply, double PowerDemand,
    double StoredPowerDays, double PowerStorageCapacityDays, double StorageChargePerDay, double StorageDischargePerDay,
    double CargoTransferCapacityPerDay,
    IReadOnlyList<UiSurfaceBuilding> Buildings,
    IReadOnlyList<UiSurfaceBuildOption> BuildOptions, double CreditsPerDay, double UpkeepCreditsPerDay,
    double BaseOperationsFundingFraction,
    double IndustryPerDay, double SciencePerDay, string SpecializationName, string SpecializationDescription,
    int SpecializationComplexes, bool SpecializationActive, string SurfaceVisualClass,
    double PopulationMillions, int RequiredHabitatSystems, double HabitatSupportReduction, int BuildingCapacity,
    string HubName, int HubLevel, bool IsCapitalHub, bool CanUpgradeHub,
    double HubUpgradeCreditCost, double HubUpgradeIndustryCost, bool CanAffordHubUpgrade,
    string? HubUpgradeLockReason,
    double EnvironmentConstructionCostMultiplier,
    double EnvironmentalWearMultiplier,
    bool IsResourceOutpost, double ExtractionPerDay, double StoredExtractedMaterials,
    double ExtractedMaterialCapacity, double RemainingDepositMaterials,
    double InitialDepositMaterials, string DepositMaterialName, string DepositGrade,
    double DepositAccessibility, double ExtractionYieldMultiplier, string OutpostOperationsStatus,
    double FoodCapacityMillions, double WaterCapacityMillions, double HousingCapacityMillions, double SupportedPopulationMillions,
    double SustenanceSupportRatio, string LimitingSustenanceSupply,
    double EffectiveSustenanceSupportRatio, string EffectiveLimitingSustenanceSupply, bool SustenanceBuffered,
    bool SustenanceDeclining, double FoodDaysUntilDepletion, double WaterDaysUntilDepletion,
    string SustenanceStatus, string SustenanceRecoveryAction,
    double WorkforceAvailableMillions, double WorkforceDemandMillions,
    double WorkingAgePopulationMillions, double EmployedPopulationMillions, double EmploymentRate,
    double FoodReserveDays, double WaterReserveDays, double HubUpgradeDaysRemaining = 0);
public sealed record UiSurfaceBuilding(int Id, string TypeId, string Name, float X, float Z,
    float RotationDegrees, double Progress, double Cost, bool Complete, bool Powered,
    bool CanUpgrade = false, string? UpgradeName = null, double UpgradeCreditCost = 0,
    double UpgradeIndustryCost = 0, bool CanAffordUpgrade = false, bool Staffed = true,
    bool Enabled = true, string? UpgradeLockReason = null, bool Prioritized = false,
    double Condition = 1.0, double Efficiency = 1.0, double RepairIndustryCost = 0.0,
    bool CanAffordRepair = false, string ConstructionStage = "Operational",
    double ConstructionStageProgress = 1.0, double RemainingConstructionMaterials = 0.0,
    bool EssentialService = false, double UpgradeDaysRemaining = 0,
    double StoredMaterials = 0, double SharedConstructionDemand = 0, double MinimumConstructionDays = 0,
    string ConstructionStatus = "Operational", string ConstructionRecoveryAction = "");
public sealed record UiSurfaceBuildOption(string Id, string Name, string Description, double IndustryCost,
    double CreditCost, float FootprintRadius, bool CanAfford, double StoredMaterials = 0,
    double PendingConstructionDemand = 0);
public sealed record UiSurfaceOrderResult(bool Accepted, string Message);
