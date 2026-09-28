using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;
using Game.Simulation.Species;
using Game.Simulation.Construction;

namespace Game.Simulation.Economy;

public sealed record CreditFlowSnapshot(
    double ColonyRevenuePerDay,
    double TradeRevenuePerDay,
    double ColonyAdministrationPerDay,
    double PopulationServicesPerDay,
    double HabitatSupportPerDay,
    double FleetOperationsPerDay,
    double OrbitalMaintenancePerDay,
    double SurfaceMaintenancePerDay,
    double ResearchOperationsPerDay)
{
    public double GrossIncomePerDay => ColonyRevenuePerDay + TradeRevenuePerDay;
    public double OperatingCostsPerDay => ColonyAdministrationPerDay + PopulationServicesPerDay + HabitatSupportPerDay + FleetOperationsPerDay + OrbitalMaintenancePerDay + SurfaceMaintenancePerDay + ResearchOperationsPerDay;
    public double NetCreditsPerDay => GrossIncomePerDay - OperatingCostsPerDay;
}

public sealed class EconomySimulation
{
    public const double BaseIndustryStorage = 500.0;
    public const double IndustryStoragePerInfrastructure = 500.0;
    public const double BaselineDailyPopulationGrowthRate = 0.000055;
    public const double UnsupportedPopulationDeclineRatePerDay = 0.00040;
    public const double ColonyAdministrationCreditsPerDay = 1.0;
    public const double OutpostAdministrationCreditsPerDay = 0.12;
    public const double EmploymentTaxCreditsPerBillionWorkersPerDay =
        0.75 / (ColonyLaborEconomy.WorkingAgePopulationFraction * ColonyLaborEconomy.BaselineEmploymentRate);
    public const double PopulationServicesCreditsPerBillionPerDay = 0.50;

    private readonly IColonyPopulationTurnoverPressureView _turnoverPressure;

    public EconomySimulation(IColonyPopulationTurnoverPressureView? turnoverPressure = null)
    {
        _turnoverPressure = turnoverPressure ?? new CurrentColonyPopulationTurnoverPressureView();
    }

    public void Advance(GalaxyState galaxy, double simulationDelta, bool accrueLegacyScience = true)
    {
        if (simulationDelta <= 0.0)
            return;

        foreach (var economy in galaxy.Economies)
        {
            var colonies = galaxy.Colonies.Where(colony => colony.CivilizationId == economy.CivilizationId).ToArray();
            var construction = galaxy.ConstructionStates.First(c => c.CivilizationId == economy.CivilizationId);
            // Adaptive Research applies its funded operating expense after this base economy step.
            // Exclude the previous step's recorded research spend here to avoid charging it twice.
            var creditFlow = GetCreditFlow(galaxy, economy.CivilizationId,
                includeResearchOperations: false, powerIntervalDays: simulationDelta);

            var openingArrears = Math.Max(0.0, economy.OperatingArrears);
            var availableFunds = Math.Max(0.0, economy.Credits) + creditFlow.GrossIncomePerDay * simulationDelta;
            var currentOperatingObligations = creditFlow.OperatingCostsPerDay * simulationDelta;
            var totalObligations = openingArrears + currentOperatingObligations;
            var paid = Math.Min(availableFunds, totalObligations);
            economy.Credits = Math.Max(0.0, availableFunds - paid);
            economy.OperatingArrears = Math.Max(0.0, totalObligations - paid);
            var paidTowardCurrentOperations = Math.Max(0.0, paid - openingArrears);
            var operatingFundingFraction = currentOperatingObligations <= 0.0000001
                ? 1.0
                : Math.Clamp(paidTowardCurrentOperations / currentOperatingObligations, 0.0, 1.0);
            economy.LastBaseOperationsFundingFraction = operatingFundingFraction;

            double industryPerDay = 0.0;
            double sciencePerDay = 0.0;

            foreach (var colony in colonies)
            {
                var populationFactor = Math.Max(0.01, colony.PopulationMillions / 1000.0);
                var infrastructure = Math.Clamp(colony.Infrastructure, 0.1, 5.0);
                var stability = Math.Clamp(colony.Stability, 0.1, 1.2);
                var demographic = _turnoverPressure.Build(galaxy, colony);

                SurfaceConstruction.AdvanceCondition(galaxy, colony, operatingFundingFraction, simulationDelta);
                var surface = SurfaceConstruction.GetOutput(colony, simulationDelta);
                SurfaceConstruction.AdvancePowerStorage(colony, surface, simulationDelta);
                if (colony.Kind == SettlementKind.Colony)
                {
                    industryPerDay += populationFactor * 0.42 * infrastructure * stability;
                    sciencePerDay += populationFactor * 0.25 * infrastructure * stability;
                    industryPerDay += surface.IndustryPerDay;
                }
                sciencePerDay += surface.SciencePerDay;
                ResourceOutpostOperations.Advance(galaxy, colony, simulationDelta, operatingFundingFraction);

                // Economy remains authoritative for the final population mutation and the
                // Terran-normalized base rate. Species supplies a dimensionless effective pace
                // composed from authored life history and, only when authoritative, the exact
                // naturally viable occupied environment. Habitat-supported fallback and legacy
                // null-body colonies remain environmentally neutral until support is modeled.
                if (colony.Kind == SettlementKind.Colony)
                {
                    var sustenance = ColonySustenanceCapacity.GetSnapshot(galaxy, colony, surface);
                    var reserves = ColonySustenanceReserves.Advance(colony, sustenance, simulationDelta);
                    var populationRate = reserves.EffectiveSupportRatio >= 1.0
                        ? BaselineDailyPopulationGrowthRate * stability * demographic.EffectiveGrowthPaceFactor *
                          Math.Clamp(1.0 - (1.0 / sustenance.SupportRatio), 0.0, 1.0)
                        : -UnsupportedPopulationDeclineRatePerDay * Math.Clamp(1.0 - reserves.EffectiveSupportRatio, 0.0, 1.0);
                    colony.PopulationMillions *= Math.Exp(populationRate * simulationDelta);
                }
            }

            if (construction.CompletedProjectIds.Contains("industrial_automation"))
                industryPerDay *= 1.35;
            industryPerDay += construction.CompletedProjectIds
                .Select(ConstructionRegistry.Find)
                .Sum(project => project?.IndustryPerDay ?? 0);
            if (construction.CompletedProjectIds.Contains("research_network"))
                sciencePerDay *= 1.30;

            // An unfunded economy may spend reserves already in storage, but it cannot create
            // fresh industrial or legacy science output for free. Revenue remains
            // collectible so a viable tax base can clear arrears and restore operations.
            industryPerDay *= operatingFundingFraction;
            sciencePerDay *= operatingFundingFraction;

            var netCreditsPerDay = creditFlow.NetCreditsPerDay;
            economy.Industry += industryPerDay * simulationDelta;
            if (accrueLegacyScience)
                economy.Science += sciencePerDay * simulationDelta;
            economy.LastCreditsPerSecond = netCreditsPerDay;
            economy.LastIndustryPerSecond = industryPerDay;
            economy.LastSciencePerSecond = accrueLegacyScience ? sciencePerDay : 0.0;
        }
    }

    public static CreditFlowSnapshot GetCreditFlow(
        GalaxyState galaxy,
        int civilizationId,
        bool includeResearchOperations = true,
        double powerIntervalDays = 1.0)
    {
        double colonyRevenue = 0.0;
        double tradeRevenue = 0.0;
        double administration = 0.0;
        double populationServices = 0.0;
        double habitatSupport = 0.0;
        double surfaceMaintenance = 0.0;
        var habitatBurden = new CurrentColonyHabitatSupportBurdenView();
        var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == civilizationId);
        var industrialAutomation = construction.CompletedProjectIds.Contains("industrial_automation");

        foreach (var colony in galaxy.Colonies.Where(item => item.CivilizationId == civilizationId))
        {
            var populationFactor = Math.Max(0.01, colony.PopulationMillions / 1000.0);
            var infrastructure = Math.Clamp(colony.Infrastructure, 0.1, 5.0);
            var stability = Math.Clamp(colony.Stability, 0.1, 1.2);
            var surface = SurfaceConstruction.GetOutput(colony, powerIntervalDays);
            if (colony.Kind == SettlementKind.Colony)
            {
                var labor = ColonyLaborEconomy.GetSnapshot(colony, industrialAutomation,
                    Math.Min(surface.WorkforceAvailableMillions, surface.WorkforceDemandMillions));
                colonyRevenue += labor.EmployedPopulationMillions / 1000.0 *
                    EmploymentTaxCreditsPerBillionWorkersPerDay * infrastructure * stability *
                    (Game.Simulation.Territory.TerritorialRuntime.Peek(galaxy)?.Read(civilizationId, colony.SystemId)?.TaxCollection ?? 1);
            }
            if (colony.Kind == SettlementKind.Colony)
                tradeRevenue += surface.CreditsPerDay;
            surfaceMaintenance += surface.UpkeepCreditsPerDay;
            // A tiny dependent outpost has real overhead without being charged as though it
            // were a self-governing world of hundreds of millions. Administration reaches the
            // established full-colony rate at 250 million inhabitants.
            administration += GetAdministrationCost(colony.PopulationMillions) *
                (Game.Simulation.Territory.TerritorialRuntime.Peek(galaxy)?.Read(civilizationId, colony.SystemId)?.AdministrationMultiplier ?? 1);
            populationServices += populationFactor * PopulationServicesCreditsPerBillionPerDay * infrastructure;
            var burden = habitatBurden.Build(galaxy, colony.Id);
            habitatSupport += GetHabitatSupportCost(burden) * (1 - surface.HabitatSupportReduction);
        }

        var fleetOperations = galaxy.Fleets
            .Where(fleet => fleet.IsActive && fleet.CivilizationId == civilizationId)
            .Sum(fleet => GetFleetOperatingCost(fleet.Role));

        var orbitalMaintenance = galaxy.ConstructionStates
            .First(state => state.CivilizationId == civilizationId).CompletedProjectIds
            .Select(ConstructionRegistry.Find)
            .Sum(project => project?.UpkeepCreditsPerDay ?? 0);

        var researchOperations = includeResearchOperations
            ? galaxy.Economies.First(state => state.CivilizationId == civilizationId).LastResearchSpendingPerDay
            : 0.0;
        orbitalMaintenance += Game.Simulation.Territory.TerritorialConstruction.Upkeep(galaxy, civilizationId);
        return new(colonyRevenue, tradeRevenue, administration, populationServices, habitatSupport,
            fleetOperations, orbitalMaintenance, surfaceMaintenance, researchOperations);
    }

    public static double GetIndustryStorageCapacity(GalaxyState galaxy, int civilizationId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var civilization = galaxy.Civilizations.First(value => value.Id == civilizationId);
        if (civilization.IsSeededAncient)
            return 50_000.0;

        var capacity = BaseIndustryStorage + galaxy.Colonies
            .Where(colony => colony.CivilizationId == civilizationId)
            .Sum(colony => IndustryStoragePerInfrastructure * Math.Clamp(colony.Infrastructure, 0.1, 5.0));
        var construction = galaxy.ConstructionStates.First(value => value.CivilizationId == civilizationId);
        if (construction.CompletedProjectIds.Contains("industrial_automation")) capacity += 500.0;
        if (construction.CompletedProjectIds.Contains("orbital_launch_complex")) capacity += 250.0;
        if (construction.CompletedProjectIds.Contains("orbital_shipyard")) capacity += 500.0;
        if (construction.CompletedProjectIds.Contains("asteroid_resource_network")) capacity += 1_000.0;
        return capacity;
    }

    public static void ApplyIndustryStorageCaps(
        GalaxyState galaxy,
        IReadOnlyDictionary<int, double>? existingReserves = null)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        foreach (var economy in galaxy.Economies)
        {
            var capacity = GetIndustryStorageCapacity(galaxy, economy.CivilizationId);
            var preservedReserve = existingReserves is not null &&
                existingReserves.TryGetValue(economy.CivilizationId, out var existing)
                    ? Math.Max(capacity, existing)
                    : capacity;
            if (economy.Industry > preservedReserve)
                economy.Industry = preservedReserve;
        }
    }

    public static double GetAdministrationCost(double populationMillions) =>
        Math.Clamp(populationMillions / 250.0,
            OutpostAdministrationCreditsPerDay, ColonyAdministrationCreditsPerDay);

    public static double GetHabitatSupportCost(ColonyHabitatSupportBurden burden) =>
        (burden.Environment?.RequiredMitigationCategories ?? 0) * 0.015 *
        Math.Max(1.0, Math.Sqrt(burden.PopulationMillions));

    public static double GetFleetOperatingCost(FleetRole role) => role switch
    {
        FleetRole.Scout => 0.08,
        FleetRole.Science => 0.12,
        FleetRole.Colony => 0.16,
        FleetRole.Military => 0.35,
        FleetRole.Logistics => 0.14,
        _ => 0.12,
    };
}
