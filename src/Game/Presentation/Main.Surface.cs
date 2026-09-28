using System;
using System.Linq;
using Game.Simulation.Construction;
using Game.Simulation.Economy;
using Game.Simulation.Models;
using Game.Simulation.Species;
using Game.Simulation.Shipbuilding;
using Game.Presentation.Spatial;
using Godot;

namespace Game.Presentation;

public sealed record UiOwnedColonySnapshot(int ColonyId, int BodyId, string ColonyName, string PlanetName,
    string SystemName, double PopulationMillions, int BuildingCount, bool CanLand,
    string SpecializationName, string SpecializationDescription, string SettlementScale,
    double AdministrationCreditsPerDay, double HabitatSupportCreditsPerDay, double GrossHabitatSupportCreditsPerDay,
    double HabitatSupportReduction, double SurfacePowerSupply, double SurfacePowerDemand, string HabitatNeeds,
    double EnvironmentalWearMultiplier,
    double ExtractionPerDay, double StoredExtractedMaterials, double ExtractedMaterialCapacity,
    double RemainingDepositMaterials, double InitialDepositMaterials, string DepositMaterialName,
    string DepositGrade, double DepositAccessibility, double ExtractionYieldMultiplier, string OutpostOperationsStatus,
    bool CanRequestFreight, string FreightActionReason,
    double FoodCapacityMillions, double WaterCapacityMillions, double HousingCapacityMillions, double SupportedPopulationMillions,
    double SustenanceSupportRatio, string LimitingSustenanceSupply,
    double WorkforceAvailableMillions, double WorkforceDemandMillions,
    double WorkingAgePopulationMillions, double EmployedPopulationMillions, double EmploymentRate,
    double FoodReserveDays, double WaterReserveDays,
    double AverageBuildingCondition, int DamagedBuildingCount, int FailedBuildingCount,
    double StoredPowerDays, double PowerStorageCapacityDays, double StorageChargePerDay, double StorageDischargePerDay,
    double CargoTransferCapacityPerDay);

public partial class Main
{
    private PlanetSurfaceView? _planetSurfaceView;
    private GalaxyState? _surfaceGalaxy;
    private int? _surfaceColonyId;
    private int? _surfaceBodyId;
    public bool UiIsSurfaceOpen => _planetSurfaceView?.IsOpen == true;
    public UiSurfaceSnapshot? UiCurrentSurface => BuildSurfaceSnapshot();
    public UiOwnedColonySnapshot[] UiOwnedColonies => _galaxy is null
        ? Array.Empty<UiOwnedColonySnapshot>()
        : _galaxy.Colonies
            .Where(colony => colony.CivilizationId == _galaxy.PlayerCivilizationId)
            .OrderBy(colony => colony.Id)
            .Select(colony =>
            {
                var body = _galaxy.PlanetaryBodies.FirstOrDefault(item => item.Id == colony.PlanetaryBodyId);
                var system = _galaxy.Systems.First(item => item.Id == colony.SystemId);
                var specialization = SurfaceConstruction.GetSpecialization(colony);
                var surface = SurfaceConstruction.GetOutput(colony);
                var support = new CurrentColonyHabitatSupportBurdenView().Build(_galaxy, colony.Id);
                var needs = support.Environment is not { } environment ? "Environment unresolved" :
                    environment.RequiredMitigationCategories == 0 ? "Natural environment" :
                    $"{environment.RequiredMitigationCategories} habitat systems required";
                var grossSupport = EconomySimulation.GetHabitatSupportCost(support);
                var outpost = ResourceOutpostOperations.GetSnapshot(_galaxy, colony);
                var sustenance = ColonySustenanceCapacity.GetSnapshot(_galaxy, colony);
                var labor = ColonyLaborEconomy.GetSnapshot(colony,
                    _galaxy.ConstructionStates.First(state => state.CivilizationId == colony.CivilizationId)
                        .CompletedProjectIds.Contains("industrial_automation"),
                    Math.Min(surface.WorkforceAvailableMillions, surface.WorkforceDemandMillions));
                var freight = FindAvailableFreighter();
                var completedBuildings = colony.SurfaceBuildings.Where(item => item.IsComplete).ToArray();
                var averageCondition = completedBuildings.Length == 0 ? 1.0 : completedBuildings.Average(item => item.Condition);
                var damagedBuildings = completedBuildings.Count(item => item.Condition < 1.0 - .0000001);
                var failedBuildings = completedBuildings.Count(item => item.Condition <= SurfaceConstruction.MinimumOperationalCondition);
                var canRequestFreight = outpost.IsResourceOutpost && freight is not null &&
                    (outpost.StoredMaterials > 0.0 || outpost.ExtractionPerDay > 0.0);
                var freightReason = !outpost.IsResourceOutpost ? string.Empty
                    : freight is null ? "Build an Interstellar Bulk Freighter and station it at a developed colony."
                    : outpost.StoredMaterials <= 0.0 && outpost.ExtractionPerDay <= 0.0 ? outpost.Status
                    : $"Dispatch {freight.Name} to collect up to {freight.CargoMaterialCapacity:0.#} material units.";
                return new UiOwnedColonySnapshot(colony.Id, colony.PlanetaryBodyId ?? -1, colony.Name,
                    body?.Name ?? "Orbital habitat", system.Name, colony.PopulationMillions,
                    colony.SurfaceBuildings.Count, body?.Environment.HasSolidSurface == true,
                    specialization.Name, specialization.Description,
                    colony.Kind == SettlementKind.ResourceOutpost ? "Staffed resource outpost" :
                        colony.PopulationMillions < 250 ? "Growing settlement" : "Colony",
                    EconomySimulation.GetAdministrationCost(colony.PopulationMillions),
                    grossSupport * (1 - surface.HabitatSupportReduction), grossSupport,
                    surface.HabitatSupportReduction, surface.Supply, surface.Demand, needs,
                    SurfaceConstruction.GetEnvironmentalWearMultiplier(_galaxy, colony),
                    outpost.ExtractionPerDay, outpost.StoredMaterials, outpost.StorageCapacity,
                    outpost.RemainingDepositMaterials, outpost.InitialDepositMaterials, outpost.DepositMaterialName,
                    outpost.DepositGrade, outpost.DepositAccessibility, outpost.ExtractionYieldMultiplier, outpost.Status,
                    canRequestFreight, freightReason, sustenance.FoodCapacityMillions,
                    sustenance.WaterCapacityMillions, sustenance.HousingCapacityMillions, sustenance.SupportedPopulationMillions,
                    sustenance.SupportRatio, sustenance.LimitingSupply,
                    surface.WorkforceAvailableMillions, surface.WorkforceDemandMillions,
                    labor.WorkingAgePopulationMillions, labor.EmployedPopulationMillions, labor.EmploymentRate,
                    colony.StoredFoodPopulationDaysMillions / Math.Max(.001, colony.PopulationMillions),
                    colony.StoredWaterPopulationDaysMillions / Math.Max(.001, colony.PopulationMillions),
                    averageCondition, damagedBuildings, failedBuildings,
                    surface.StoredPowerDays, surface.PowerStorageCapacityDays,
                    surface.StorageChargePerDay, surface.StorageDischargePerDay,
                    FreightSimulation.GetPortTransferCapacityPerDay(colony));
            }).ToArray();

    public string UiRequestOutpostFreight(int outpostId)
    {
        if (_galaxy is null) return "Freight control is unavailable while the campaign initializes.";
        var fleet = FindAvailableFreighter();
        if (fleet is null) return "No idle Interstellar Bulk Freighter is stationed at one of your developed colonies.";
        return _coreSimulation.IssueFreightCollectionOrder(
            _galaxy, _galaxy.PlayerCivilizationId, fleet.Id, outpostId).Message;
    }

    private FleetState? FindAvailableFreighter()
    {
        if (_galaxy is null) return null;
        var developedSystems = _galaxy.Colonies.Where(colony => colony.CivilizationId == _galaxy.PlayerCivilizationId &&
            colony.Kind == SettlementKind.Colony).Select(colony => colony.SystemId).ToHashSet();
        return _galaxy.Fleets.Where(fleet => fleet.IsActive && fleet.CivilizationId == _galaxy.PlayerCivilizationId &&
                fleet.Role == FleetRole.Logistics && fleet.DesignId == ShipDesignRegistry.BulkFreighterId &&
                fleet.DestinationSystemId is null && fleet.FreightHomeColonyId is null && fleet.FreightTargetOutpostId is null &&
                fleet.CargoMaterials <= 0.0 && fleet.CurrentSystemId is int systemId && developedSystems.Contains(systemId))
            .OrderBy(fleet => fleet.Id).FirstOrDefault();
    }

    protected void InitializeSurfacePresentation()
    {
        if (_planetSurfaceView is not null) return;
        var layer = new CanvasLayer { Name = "PlanetSurfaceLayer", Layer = 20 };
        _planetSurfaceView = new PlanetSurfaceView { Name = "PlanetSurfaceView" };
        _planetSurfaceView.Configure(BuildSurfaceSnapshot, UiPlaceSurfaceBuilding, UiRemoveSurfaceBuilding,
            UiUpgradeSurfaceBuilding, UiRepairSurfaceBuilding, UiSetSurfaceBuildingEnabled,
            UiSetSurfaceBuildingPriority, UiUpgradeSurfaceHub);
        _planetSurfaceView.ReadSkyCompanions = () => _systemSpatialCanvas?.VisibleBodies
            .Where(body => body.Kind == PlanetaryBodyKind.Moon && body.ParentBodyId == _surfaceBodyId).ToArray()
            ?? Array.Empty<SystemSpatialBodyMarker>();
        _planetSurfaceView.IsInputBlocked = () => (UiIsMenuOpen || UiIsDeveloperToolsOpen);
        _planetSurfaceView.SaveRequested += UiSave;
        _planetSurfaceView.PlaybackCycleRequested += UiCyclePlayback;
        _planetSurfaceView.PlaybackPauseRequested += UiTogglePause;
        _planetSurfaceView.ReadTimeLabel = () => UiModeLabel + " · " + (UiDeveloperToolsUsed ? "Tools used · " : "") + UiSpeedLabel;
        _planetSurfaceView.ReadPlaybackState = () => new PlaybackState(UiIsPaused, UiCurrentSpeed, UiResumeSpeed, UiIsDeveloperMode);
        _planetSurfaceView.ReturnToOrbit += UiReturnToOrbit;
        AddChild(layer);
        layer.AddChild(_planetSurfaceView);
        PlanetSurfaceAvailable = CanOpenPlanetSurface;
        PlanetSurfaceRequested += UiOpenPlanetSurface;
    }

    protected void RefreshSurfacePresentation()
    {
        if (_planetSurfaceView is not null) _planetSurfaceView.VisualStyle = UiVisualStyle;
        if (UiIsSurfaceOpen && BuildSurfaceSnapshot() is null) UiReturnToOrbit();
    }

    private bool CanOpenPlanetSurface(int bodyId) => _galaxy is not null &&
        _galaxy.Colonies.Any(colony => colony.CivilizationId == _galaxy.PlayerCivilizationId &&
            colony.PlanetaryBodyId == bodyId && colony.SystemId == _selectedSystemId) &&
        _galaxy.PlanetaryBodies.Any(body => body.Id == bodyId && body.SystemId == _selectedSystemId && body.Environment.HasSolidSurface);

    public void UiOpenPlanetSurface(int bodyId)
    {
        if ((UiIsMenuOpen || UiIsDeveloperToolsOpen) || UiFocusedPlanetBodyId != bodyId || !CanOpenPlanetSurface(bodyId))
        {
            SetStatus("Focus a planet with one of your surface colonies to land.", 5);
            return;
        }
        InitializeSurfacePresentation();
        var colony = _galaxy.Colonies.First(item => item.CivilizationId == _galaxy.PlayerCivilizationId && item.PlanetaryBodyId == bodyId);
        _surfaceGalaxy = _galaxy;
        _surfaceColonyId = colony.Id;
        _surfaceBodyId = bodyId;
        _panning = false;
        _planetSurfaceView!.Open();
    }

    public void UiReturnToOrbit()
    {
        _systemSpatialCanvas?.PrepareSurfaceReturn();
        _planetSurfaceView?.Close();
        _surfaceGalaxy = null;
        _surfaceColonyId = null;
        _surfaceBodyId = null;
        _panning = false;
    }

    private void UiBeginPlanetDescent(int bodyId)
    {
        var scene = _systemSpatialCanvas?.Scene;
        var marker = _systemSpatialCanvas?.GetBodyMarker(bodyId);
        if (scene is null || marker is null || !CanOpenPlanetSurface(bodyId)) return;
        var basis = scene.GetBodyBasis(bodyId) ?? Basis.Identity;
        var altitude = scene.FocusAltitudeRatio;
        UiOpenPlanetSurface(bodyId);
        if (UiIsSurfaceOpen) _planetSurfaceView!.OpenFromOrbit(marker, scene.CameraBasis, basis, altitude);
    }

    public void UiOpenOwnedColony(int colonyId, bool land)
    {
        if (_galaxy is null || UiIsMenuOpen || UiIsDeveloperToolsOpen) return;
        var colony = _galaxy.Colonies.FirstOrDefault(item => item.Id == colonyId &&
            item.CivilizationId == _galaxy.PlayerCivilizationId);
        if (colony?.PlanetaryBodyId is not int bodyId)
        {
            SetStatus("This colony has no surface destination.", 5);
            return;
        }

        GetNode<CampaignSidebar>("CampaignSidebar").CloseDrawer();
        UiReturnToOrbit();
        if (UiIsSystemSpatialView) ReturnToStellarView(announce: false);
        _selectedSystemId = colony.SystemId;
        EnterSelectedSystemView();
        if (_systemSpatialCanvas?.FocusBody(bodyId) != true)
        {
            SetStatus("The colony world is not available in the current orbital survey.", 6);
            return;
        }
        if (land) UiOpenPlanetSurface(bodyId);
    }

    private UiSurfaceSnapshot? BuildSurfaceSnapshot()
    {
        if (_galaxy is null || !ReferenceEquals(_surfaceGalaxy, _galaxy) || _surfaceBodyId is not int bodyId ||
            UiFocusedPlanetBodyId != bodyId || !CanOpenPlanetSurface(bodyId)) return null;
        var colony = _galaxy.Colonies.FirstOrDefault(item => item.Id == _surfaceColonyId && item.CivilizationId == _galaxy.PlayerCivilizationId &&
            item.PlanetaryBodyId == bodyId && item.SystemId == _selectedSystemId);
        if (colony is null) return null;
        var output = SurfaceConstruction.GetOutput(colony);
        var specialization = SurfaceConstruction.GetSpecialization(colony);
        var body = _galaxy.PlanetaryBodies.First(item => item.Id == bodyId);
        var habitat = new CurrentColonyHabitatSupportBurdenView().Build(_galaxy, colony.Id);
        var outpost = ResourceOutpostOperations.GetSnapshot(_galaxy, colony);
        var sustenance = ColonySustenanceCapacity.GetSnapshot(_galaxy, colony);
        var sustenanceFeedback = ColonySurfaceFeedbackReadModel.GetSustenance(_galaxy, colony, sustenance);
        var constructionFeedback = ColonySurfaceFeedbackReadModel.GetConstructionContext(_galaxy, colony.CivilizationId,
            PlayerEconomy.Industry, PlayerEconomy.LastIndustryPerSecond > .0000001);
        var labor = ColonyLaborEconomy.GetSnapshot(colony,
            _galaxy.ConstructionStates.First(state => state.CivilizationId == colony.CivilizationId)
                .CompletedProjectIds.Contains("industrial_automation"),
            Math.Min(output.WorkforceAvailableMillions, output.WorkforceDemandMillions));
        var player = _galaxy.Civilizations.First(item => item.Id == _galaxy.PlayerCivilizationId);
        var isCapitalHub = colony.Kind == SettlementKind.Colony && colony.SystemId == player.HomeSystemId &&
            colony.Id == _galaxy.Colonies.Where(item => item.CivilizationId == player.Id &&
                item.Kind == SettlementKind.Colony && item.SystemId == player.HomeSystemId)
                .MaxBy(item => item.PopulationMillions)?.Id;
        var hubName = colony.Kind == SettlementKind.ResourceOutpost ? "Sealed outpost hub" :
            isCapitalHub ? "Planetary hub" : "Command center";
        var hubUpgrade = SurfaceConstruction.GetHubUpgradeCost(_galaxy, colony);
        var surfaceCapabilities = new AdaptiveResearchConstructionCapabilityView(_adaptiveResearch!);
        var hubUpgradeLock = hubUpgrade is null ? null : SurfaceConstruction.GetHubUpgradeLockReason(
            _galaxy, player.Id, colony, surfaceCapabilities);
        return new(colony.Id, bodyId, body.Name, colony.Name, UiCurrency, PlayerEconomy.Credits, PlayerEconomy.Industry, output.Supply, output.Demand,
            output.StoredPowerDays, output.PowerStorageCapacityDays, output.StorageChargePerDay, output.StorageDischargePerDay,
            FreightSimulation.GetPortTransferCapacityPerDay(colony),
            colony.SurfaceBuildings.OrderBy(item => item.Id).Select(item =>
            {
                var definition = SurfaceBuildingCatalog.Find(item.TypeId)!;
                var upgrade = definition.UpgradeTypeId is null ? null : SurfaceBuildingCatalog.Find(definition.UpgradeTypeId);
                var upgradeCreditCost = SurfaceConstruction.GetUpgradeAuthorizationCost(_galaxy, colony, definition);
                var upgradeLock = SurfaceConstruction.GetBuildingUpgradeLockReason(_galaxy, colony.CivilizationId,
                    definition, surfaceCapabilities);
                var stage = SurfaceConstruction.GetConstructionStage(item);
                var siteConstructionFeedback = item.IsComplete
                    ? null
                    : ColonySurfaceFeedbackReadModel.GetConstruction(item, constructionFeedback);
                return new UiSurfaceBuilding(item.Id, item.TypeId, definition.Name, item.X, item.Z, item.RotationDegrees,
                    item.IndustryProgress / definition.IndustryCost, definition.IndustryCost, item.IsComplete,
                    output.PoweredBuildingIds.Contains(item.Id), item.IsComplete && upgrade is not null && item.PendingUpgradeTypeId is null, upgrade?.Name,
                    upgradeCreditCost, definition.UpgradeIndustryCost,
                    item.IsComplete && upgrade is not null && PlayerEconomy.Credits + 0.0001 >= upgradeCreditCost &&
                    PlayerEconomy.Industry + 0.0001 >= definition.UpgradeIndustryCost,
                    output.StaffedBuildingIds.Contains(item.Id), item.IsEnabled, upgradeLock,
                    item.OperatingPriority > 0, item.Condition, item.Condition <= SurfaceConstruction.MinimumOperationalCondition
                        ? 0.0 : .5 + .5 * item.Condition, SurfaceConstruction.GetRepairIndustryCost(item),
                    PlayerEconomy.Industry + .0001 >= SurfaceConstruction.GetRepairIndustryCost(item),
                    stage.Name, stage.PhaseProgress, stage.RemainingMaterials,
                    SurfaceConstruction.GetEssentialServicePriority(item.TypeId) > 0, item.UpgradeDaysRemaining,
                    siteConstructionFeedback?.StoredMaterials ?? PlayerEconomy.Industry,
                    siteConstructionFeedback?.SharedSiteDemand ?? 0, siteConstructionFeedback?.MinimumDaysRemaining ?? 0,
                    siteConstructionFeedback?.Status ?? "Operational", siteConstructionFeedback?.RecoveryAction ?? string.Empty);
            }).ToArray(),
            SurfaceBuildingCatalog.All.Where(item => SurfaceConstruction.IsAvailableForSettlement(colony, item)).Select(item =>
            {
                var authorizationCost = SurfaceConstruction.GetAuthorizationCost(_galaxy, colony, item);
                return new UiSurfaceBuildOption(item.Id, item.Name, item.Description,
                    item.IndustryCost, authorizationCost, item.FootprintRadius,
                    PlayerEconomy.Credits + 0.0001 >= authorizationCost, constructionFeedback.StoredMaterials,
                    constructionFeedback.SharedSiteDemand);
            }).ToArray(),
            colony.Kind == SettlementKind.Colony ? output.CreditsPerDay : 0.0,
            output.UpkeepCreditsPerDay,
            PlayerEconomy.LastBaseOperationsFundingFraction,
            colony.Kind == SettlementKind.Colony
                ? output.IndustryPerDay * PlayerEconomy.LastBaseOperationsFundingFraction : 0.0,
            output.SciencePerDay,
            specialization.Name, specialization.Description, specialization.CompletedComplexes, specialization.Active,
            SurfaceVisualClass(body), colony.PopulationMillions,
            habitat.Environment?.RequiredMitigationCategories ?? 0, output.HabitatSupportReduction,
            SurfaceConstruction.GetBuildingCapacity(colony), hubName, colony.SurfaceHubLevel, isCapitalHub,
            hubUpgrade is not null, hubUpgrade?.CreditCost ?? 0.0, hubUpgrade?.IndustryCost ?? 0.0,
            hubUpgradeLock is null && hubUpgrade is { } cost && PlayerEconomy.Credits + 0.0001 >= cost.CreditCost &&
                PlayerEconomy.Industry + 0.0001 >= cost.IndustryCost,
            hubUpgradeLock,
            SurfaceConstruction.GetConstructionCostMultiplier(_galaxy, colony),
            SurfaceConstruction.GetEnvironmentalWearMultiplier(_galaxy, colony),
            outpost.IsResourceOutpost,
            outpost.ExtractionPerDay, outpost.StoredMaterials, outpost.StorageCapacity,
            outpost.RemainingDepositMaterials, outpost.InitialDepositMaterials, outpost.DepositMaterialName,
            outpost.DepositGrade, outpost.DepositAccessibility, outpost.ExtractionYieldMultiplier, outpost.Status,
            sustenance.FoodCapacityMillions, sustenance.WaterCapacityMillions,
            sustenance.HousingCapacityMillions, sustenance.SupportedPopulationMillions, sustenance.SupportRatio, sustenance.LimitingSupply,
            sustenanceFeedback.EffectiveSupportRatio, sustenanceFeedback.LimitingSupply, sustenanceFeedback.IsBuffered,
            sustenanceFeedback.IsDeclining, sustenanceFeedback.FoodDaysUntilDepletion, sustenanceFeedback.WaterDaysUntilDepletion,
            sustenanceFeedback.Status, sustenanceFeedback.RecoveryAction,
            output.WorkforceAvailableMillions, output.WorkforceDemandMillions,
            labor.WorkingAgePopulationMillions, labor.EmployedPopulationMillions, labor.EmploymentRate,
            colony.StoredFoodPopulationDaysMillions / Math.Max(.001, colony.PopulationMillions),
            colony.StoredWaterPopulationDaysMillions / Math.Max(.001, colony.PopulationMillions), colony.SurfaceHubUpgradeDaysRemaining);
    }

    private static string SurfaceVisualClass(PlanetaryBodyState body)
    {
        var environment = body.Environment;
        if (environment.IsImmersedEnvironment) return "oceanic";
        if (environment.TemperatureKelvin < 200) return "frozen";
        if (environment.TemperatureKelvin > 410) return "hot";
        if (environment.Atmosphere == PlanetaryAtmosphereRegime.Vacuum) return "airless";
        if (environment.AvailableSolvent == PlanetarySolventRegime.Water &&
            environment.Atmosphere is PlanetaryAtmosphereRegime.OxygenNitrogen or PlanetaryAtmosphereRegime.OxygenRich)
            return "temperate";
        if (environment.Atmosphere == PlanetaryAtmosphereRegime.Reducing) return "reducing";
        return "rocky";
    }

    public UiSurfaceOrderResult UiPlaceSurfaceBuilding(string typeId, float x, float z, float rotationDegrees)
    {
        var snapshot = BuildSurfaceSnapshot();
        if (!UiIsSurfaceOpen || (UiIsMenuOpen || UiIsDeveloperToolsOpen) || snapshot is null)
            return new(false, "Open an owned colony surface before placing a building.");
        var result = SurfaceConstruction.Place(_galaxy, _galaxy.PlayerCivilizationId, snapshot.ColonyId, typeId, x, z, rotationDegrees);
        SetStatus(result.Message, 5);
        return new(result.Accepted, result.Message);
    }

    public UiSurfaceOrderResult UiRemoveSurfaceBuilding(int buildingId)
    {
        var snapshot = BuildSurfaceSnapshot();
        if (!UiIsSurfaceOpen || (UiIsMenuOpen || UiIsDeveloperToolsOpen) || snapshot is null)
            return new(false, "Open an owned colony surface before removing a building.");
        var result = SurfaceConstruction.Remove(_galaxy, _galaxy.PlayerCivilizationId, snapshot.ColonyId, buildingId);
        SetStatus(result.Message, 6);
        return new(result.Accepted, result.Message);
    }

    public UiSurfaceOrderResult UiUpgradeSurfaceBuilding(int buildingId)
    {
        var snapshot = BuildSurfaceSnapshot();
        if (!UiIsSurfaceOpen || (UiIsMenuOpen || UiIsDeveloperToolsOpen) || snapshot is null)
            return new(false, "Open an owned colony surface before upgrading a building.");
        var result = SurfaceConstruction.Upgrade(_galaxy, _galaxy.PlayerCivilizationId, snapshot.ColonyId,
            buildingId, new AdaptiveResearchConstructionCapabilityView(_adaptiveResearch!));
        SetStatus(result.Message, 6);
        return new(result.Accepted, result.Message);
    }

    public UiSurfaceOrderResult UiRepairSurfaceBuilding(int buildingId)
    {
        var snapshot = BuildSurfaceSnapshot();
        if (!UiIsSurfaceOpen || (UiIsMenuOpen || UiIsDeveloperToolsOpen) || snapshot is null)
            return new(false, "Open an owned colony surface before repairing a building.");
        var result = SurfaceConstruction.Repair(_galaxy, _galaxy.PlayerCivilizationId, snapshot.ColonyId, buildingId);
        SetStatus(result.Message, 6);
        return new(result.Accepted, result.Message);
    }

    public UiSurfaceOrderResult UiSetSurfaceBuildingEnabled(int buildingId, bool enabled)
    {
        var snapshot = BuildSurfaceSnapshot();
        if (!UiIsSurfaceOpen || (UiIsMenuOpen || UiIsDeveloperToolsOpen) || snapshot is null)
            return new(false, "Open an owned colony surface before changing building operations.");
        var result = SurfaceConstruction.SetEnabled(
            _galaxy, _galaxy.PlayerCivilizationId, snapshot.ColonyId, buildingId, enabled);
        SetStatus(result.Message, 6);
        return new(result.Accepted, result.Message);
    }

    public UiSurfaceOrderResult UiSetSurfaceBuildingPriority(int buildingId, bool prioritized)
    {
        var snapshot = BuildSurfaceSnapshot();
        if (!UiIsSurfaceOpen || (UiIsMenuOpen || UiIsDeveloperToolsOpen) || snapshot is null)
            return new(false, "Open an owned colony surface before changing operating priority.");
        var result = SurfaceConstruction.SetOperatingPriority(
            _galaxy, _galaxy.PlayerCivilizationId, snapshot.ColonyId, buildingId, prioritized);
        SetStatus(result.Message, 6);
        return new(result.Accepted, result.Message);
    }

    public UiSurfaceOrderResult UiUpgradeSurfaceHub()
    {
        var snapshot = BuildSurfaceSnapshot();
        if (!UiIsSurfaceOpen || (UiIsMenuOpen || UiIsDeveloperToolsOpen) || snapshot is null)
            return new(false, "Open an owned colony surface before expanding its administration.");
        var result = SurfaceConstruction.UpgradeHub(_galaxy, _galaxy.PlayerCivilizationId,
            snapshot.ColonyId, new AdaptiveResearchConstructionCapabilityView(_adaptiveResearch!));
        SetStatus(result.Message, 7);
        return new(result.Accepted, result.Message);
    }
}
