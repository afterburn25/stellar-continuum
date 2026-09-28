using System.Runtime.CompilerServices;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

internal static class FleetBiologicalLoadChecks
{
    [ModuleInitializer]
    internal static void Initialize()
    {
        Run();
        Console.WriteLine("PASS: combined fleet biological load");
    }

    public static void Run()
    {
        ValidateCrewOnlyLoad();
        ValidateSameSpeciesPassengersRemainOneHabitatPopulation();
        ValidateMixedSpeciesPassengersExposeAccommodationNeeds();
        ValidateNaturalDormancyIsSpeciesBounded();
    }

    private static void ValidateCrewOnlyLoad()
    {
        var galaxy = CreateGalaxyWithPlayerSpecies(SpeciesCatalog.TerranBaselineId);
        var fleet = AddTestFleet(galaxy, FleetRole.Scout, "Crew-Only Load Scout");
        var snapshot = new FleetBiologicalLoadView().Build(galaxy, fleet.Id);

        Require(!snapshot.CarriesPassengers, "crew-only fleet incorrectly reported passengers");
        Require(snapshot.PassengerSpeciesId is null && snapshot.PassengerPopulationMillions == 0.0,
            "crew-only fleet retained passenger identity or amount");
        RequireClose(snapshot.TotalAdultBiomassKg, snapshot.CrewAdultBiomassKg,
            "crew-only fleet total biomass did not equal crew biomass");
        RequireClose(snapshot.TotalMetabolicDemandUnits, snapshot.CrewMetabolicDemandUnits,
            "crew-only fleet total metabolic load did not equal crew demand");
        Require(!snapshot.RequiresSeparateEnvironmentalAccommodation &&
                !snapshot.RequiresDedicatedPassengerNutrition &&
                !snapshot.RequiresCrossSpeciesQuarantineAssessment &&
                !snapshot.RequiresXenomedicalInterfaceAdaptation,
            "crew-only fleet invented cross-species accommodation requirements");
    }

    private static void ValidateSameSpeciesPassengersRemainOneHabitatPopulation()
    {
        var galaxy = CreateGalaxyWithPlayerSpecies(SpeciesCatalog.TerranBaselineId);
        var fleet = AddTestFleet(galaxy, FleetRole.Colony, "Same-Species Passenger Test");
        fleet.EmbarkedPopulationMillions = 2.0;
        fleet.EmbarkedPopulationSpeciesId = SpeciesCatalog.TerranBaselineId;

        var snapshot = new FleetBiologicalLoadView().Build(galaxy, fleet.Id);
        var terran = SpeciesCatalog.Get(SpeciesCatalog.TerranBaselineId);
        var expectedPassengerBiomass =
            fleet.EmbarkedPopulationMillions * terran.Physiology.TypicalAdultMassKg * 1_000_000.0;

        Require(snapshot.CarriesPassengers, "populated colony fleet did not report passengers");
        Require(!snapshot.MultipleSpeciesAboard,
            "same-species crew/passengers were incorrectly treated as multiple species");
        RequireClose(snapshot.PassengerAdultBiomassKg, expectedPassengerBiomass,
            "passenger biomass was not derived from passenger species physiology");
        RequireClose(
            snapshot.TotalAdultBiomassKg,
            snapshot.CrewAdultBiomassKg + snapshot.PassengerAdultBiomassKg,
            "combined fleet biomass did not conserve crew plus passenger biomass");
        RequireClose(
            snapshot.TotalMetabolicDemandUnits,
            snapshot.CrewMetabolicDemandUnits + snapshot.PassengerMetabolicDemandUnits,
            "combined fleet metabolism did not conserve crew plus passenger demand");
        Require(!snapshot.RequiresSeparateEnvironmentalAccommodation &&
                !snapshot.RequiresDedicatedPassengerNutrition,
            "same-species crew/passengers invented separate habitat or nutrition requirements");
    }

    private static void ValidateMixedSpeciesPassengersExposeAccommodationNeeds()
    {
        var galaxy = CreateGalaxyWithPlayerSpecies(SpeciesCatalog.TerranBaselineId);
        var fleet = AddTestFleet(galaxy, FleetRole.Colony, "Mixed-Species Passenger Test");
        fleet.EmbarkedPopulationMillions = 1.0;
        fleet.EmbarkedPopulationSpeciesId = SpeciesCatalog.CryogenicHydrocarbonId;

        var beforePopulation = fleet.EmbarkedPopulationMillions;
        var beforePassengerSpecies = fleet.EmbarkedPopulationSpeciesId;
        var snapshot = new FleetBiologicalLoadView().Build(galaxy, fleet.Id);

        Require(snapshot.MultipleSpeciesAboard,
            "Terran crew with cryogenic passengers did not report multiple species aboard");
        Require(snapshot.RequiresSeparateEnvironmentalAccommodation,
            "chemically incompatible Terran/cryogenic populations did not require separate environmental accommodation");
        Require(snapshot.RequiresDedicatedPassengerNutrition,
            "chemically incompatible Terran/cryogenic populations did not require dedicated nutrition");
        Require(snapshot.TotalAdultBiomassKg > snapshot.CrewAdultBiomassKg,
            "mixed-species passenger load did not increase total biomass");
        Require(snapshot.TotalMetabolicDemandUnits > snapshot.CrewMetabolicDemandUnits,
            "mixed-species passenger load did not increase total metabolic demand");

        RequireClose(fleet.EmbarkedPopulationMillions, beforePopulation,
            "fleet biological-load view mutated passenger population");
        Require(fleet.EmbarkedPopulationSpeciesId == beforePassengerSpecies,
            "fleet biological-load view mutated passenger species identity");
    }

    private static void ValidateNaturalDormancyIsSpeciesBounded()
    {
        var galaxy = CreateGalaxyWithPlayerSpecies(SpeciesCatalog.TerranBaselineId);
        var fleet = AddTestFleet(galaxy, FleetRole.Colony, "Dormancy Passenger Test");
        fleet.EmbarkedPopulationMillions = 1.0;
        fleet.EmbarkedPopulationSpeciesId = SpeciesCatalog.CryogenicHydrocarbonId;
        var view = new FleetBiologicalLoadView();

        var typical = view.Build(
            galaxy,
            fleet.Id,
            PopulationMetabolicOperatingState.TypicalDay,
            PopulationMetabolicOperatingState.TypicalDay);
        var dormantPassengers = view.Build(
            galaxy,
            fleet.Id,
            PopulationMetabolicOperatingState.TypicalDay,
            PopulationMetabolicOperatingState.NaturalDormancy);

        Require(
            dormantPassengers.PassengerMetabolicDemandUnits < typical.PassengerMetabolicDemandUnits,
            "cryogenic natural dormancy did not reduce passenger metabolic demand");
        Require(
            dormantPassengers.PassengerNaturalDormancyMode is not null and not DormancyMode.None,
            "cryogenic passenger load did not expose its natural dormancy mode");
        Require(dormantPassengers.PassengerMaximumNaturalDormancyDays > 0.0,
            "cryogenic passenger load did not expose a bounded natural dormancy duration");

        var rejected = false;
        try
        {
            _ = view.Build(
                galaxy,
                fleet.Id,
                PopulationMetabolicOperatingState.NaturalDormancy,
                PopulationMetabolicOperatingState.TypicalDay);
        }
        catch (InvalidOperationException ex) when (
            ex.Message.Contains("no natural dormancy", StringComparison.OrdinalIgnoreCase))
        {
            rejected = true;
        }

        Require(rejected,
            "Terran crew was allowed to claim natural dormancy life-support savings without a biological dormancy mode");
    }

    private static GalaxyState CreateGalaxyWithPlayerSpecies(string speciesId)
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x4249_4F4C_4F41_444CL,
            new GalaxyGenerationSettings
            {
                SystemCount = 36,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 420.0f,
            });

        var index = galaxy.Civilizations.ToList().FindIndex(c => c.Id == galaxy.PlayerCivilizationId);
        Require(index >= 0, "validation galaxy had no player civilization");
        galaxy.Civilizations[index] = galaxy.Civilizations[index] with { SpeciesId = speciesId };
        return galaxy;
    }

    private static FleetState AddTestFleet(GalaxyState galaxy, FleetRole role, string name)
    {
        var player = galaxy.Civilizations.First(c => c.Id == galaxy.PlayerCivilizationId);
        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        var design = ShipDesignRegistry.GetCurrentDesignForRole(role);
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0 ? 20_000 : galaxy.Fleets.Max(existing => existing.Id) + 20_000,
            CivilizationId = player.Id,
            Name = name,
            Role = role,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = design.StrategicSpeed,
            SensorRange = design.SensorRange,
            IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static void RequireClose(double actual, double expected, string message)
    {
        var scale = Math.Max(1.0, Math.Max(Math.Abs(actual), Math.Abs(expected)));
        if (Math.Abs(actual - expected) > 0.000000001 * scale)
            throw new InvalidOperationException($"{message}: expected {expected}, got {actual}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
