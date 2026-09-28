using System.Numerics;
using System.Runtime.CompilerServices;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;
using Game.Simulation.Species;

internal static class FleetCrewSpeciesChecks
{
    [ModuleInitializer]
    internal static void Initialize()
    {
        Run();
        Console.WriteLine("PASS: reconstructible fleet crew Species physiology");
    }

    public static void Run()
    {
        ValidateCurrentDesignCrewComplements();
        ValidateFleetCrewIdentityAndPhysiology();
        ValidateCrewAndPassengerSpeciesRemainSeparate();
        ValidateBiologyDoesNotMutateFleetPerformanceState();
    }

    private static void ValidateCurrentDesignCrewComplements()
    {
        foreach (var design in ShipDesignRegistry.All)
        {
            Require(design.CrewComplementIndividuals > 0,
                $"ship design {design.Id} has no positive crew complement");
            Require(design.CrewComplementIndividuals <= 100_000,
                $"ship design {design.Id} crew complement escaped the bounded aggregate model");
            var probe = new FleetState
            {
                Id = 900_000,
                CivilizationId = 0,
                Name = "Design identity probe",
                Role = design.Role,
                DesignId = design.Id,
                Position = Vector2.Zero,
            };
            Require(ShipDesignRegistry.GetForFleet(probe).Id == design.Id,
                $"persistent fleet resolver did not reconstruct ship design {design.Id}");
        }
    }

    private static void ValidateFleetCrewIdentityAndPhysiology()
    {
        var galaxy = CreateGalaxy();
        var view = new CurrentFleetCrewSpeciesView();
        var fleets = Enum.GetValues<FleetRole>()
            .Select(role => AddTestFleet(galaxy, role, $"Crew Physiology {role}"))
            .ToArray();

        foreach (var fleet in fleets)
        {
            var snapshot = view.Build(galaxy, fleet.Id);
            var civilization = galaxy.Civilizations.First(c => c.Id == fleet.CivilizationId);
            var species = SpeciesCatalog.Get(civilization.SpeciesId);
            var design = ShipDesignRegistry.GetCurrentDesignForRole(fleet.Role);

            Require(snapshot.CrewSpeciesId == civilization.SpeciesId,
                $"fleet {fleet.Id} crew species did not follow the current owner-species bridge");
            Require(snapshot.DesignId == design.Id,
                $"fleet {fleet.Id} crew snapshot reconstructed the wrong design");
            Require(snapshot.CrewComplementIndividuals == design.CrewComplementIndividuals,
                $"fleet {fleet.Id} crew complement did not match design data");
            RequireClose(
                snapshot.CrewAdultBiomassKg,
                design.CrewComplementIndividuals * species.Physiology.TypicalAdultMassKg,
                $"fleet {fleet.Id} crew biomass was not derived from species physiology");
            Require(snapshot.BaselineMetabolicDemandUnits > 0.0 && snapshot.TypicalDayMetabolicDemandUnits > 0.0,
                $"fleet {fleet.Id} crew metabolic demand was not positive");
        }
    }

    private static void ValidateCrewAndPassengerSpeciesRemainSeparate()
    {
        var galaxy = CreateGalaxy();
        var player = galaxy.Civilizations.First(c => c.Id == galaxy.PlayerCivilizationId);
        var passengerSpecies = SpeciesCatalog.All.First(species => species.Id != player.SpeciesId).Id;
        var fleet = AddTestFleet(galaxy, FleetRole.Colony, "Mixed Bridge Colony Test");
        fleet.EmbarkedPopulationMillions = 1.0;
        fleet.EmbarkedPopulationSpeciesId = passengerSpecies;

        var snapshot = new CurrentFleetCrewSpeciesView().Build(galaxy, fleet.Id);
        Require(snapshot.CrewSpeciesId == player.SpeciesId,
            "colony-ship crew species was incorrectly overwritten by passenger species");
        Require(snapshot.EmbarkedPopulationSpeciesId == passengerSpecies,
            "colony-ship passenger species was not preserved separately from crew species");
        Require(!snapshot.CrewMatchesEmbarkedPopulationSpecies,
            "crew/passenger bridge incorrectly reported different species as matching");
    }

    private static void ValidateBiologyDoesNotMutateFleetPerformanceState()
    {
        var galaxy = CreateGalaxy();
        var fleet = AddTestFleet(galaxy, FleetRole.Scout, "Crew Biology Neutrality Scout");
        var civilizationIndex = galaxy.Civilizations.ToList().FindIndex(c => c.Id == fleet.CivilizationId);
        Require(civilizationIndex >= 0, "test fleet owner was not present in the validation galaxy");
        var originalCivilization = galaxy.Civilizations[civilizationIndex];
        var originalSpeed = fleet.StrategicSpeed;
        var originalSensors = fleet.SensorRange;
        var originalCombat = fleet.Combat;

        var terranCivilization = originalCivilization with { SpeciesId = SpeciesCatalog.TerranBaselineId };
        galaxy.Civilizations[civilizationIndex] = terranCivilization;
        var terran = new CurrentFleetCrewSpeciesView().Build(galaxy, fleet.Id);

        var cryogenicCivilization = originalCivilization with { SpeciesId = SpeciesCatalog.CryogenicHydrocarbonId };
        galaxy.Civilizations[civilizationIndex] = cryogenicCivilization;
        var cryogenic = new CurrentFleetCrewSpeciesView().Build(galaxy, fleet.Id);

        Require(terran.CrewSpeciesId != cryogenic.CrewSpeciesId,
            "crew physiology comparison did not actually change species");
        Require(terran.CrewAdultBiomassKg != cryogenic.CrewAdultBiomassKg,
            "different crew physiology did not change aggregate biomass");
        Require(terran.TypicalDayMetabolicDemandUnits != cryogenic.TypicalDayMetabolicDemandUnits,
            "different crew physiology did not change life-support demand");

        RequireClose(fleet.StrategicSpeed, originalSpeed,
            "crew species directly changed strategic speed");
        RequireClose(fleet.SensorRange, originalSensors,
            "crew species directly changed sensor range");
        Require(ReferenceEquals(fleet.Combat, originalCombat),
            "crew physiology view mutated fleet Combat state");
    }

    private static FleetState AddTestFleet(GalaxyState galaxy, FleetRole role, string name)
    {
        var player = galaxy.Civilizations.First(c => c.Id == galaxy.PlayerCivilizationId);
        var home = galaxy.Systems.First(system => system.Id == player.HomeSystemId);
        var design = ShipDesignRegistry.GetCurrentDesignForRole(role);
        var fleet = new FleetState
        {
            Id = galaxy.Fleets.Count == 0
                ? 10_000
                : galaxy.Fleets.Max(existing => existing.Id) + 10_000,
            CivilizationId = player.Id,
            Name = name,
            Role = role,
            DesignId = design.Id,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = design.StrategicSpeed,
            SensorRange = design.SensorRange,
            IsActive = true,
        };
        galaxy.Fleets.Add(fleet);
        return fleet;
    }

    private static GalaxyState CreateGalaxy() =>
        new GalaxyGenerator().Generate(
            0x4352_4557_5350_4543L,
            new GalaxyGenerationSettings
            {
                SystemCount = 36,
                PreWarpCivilizationCount = 4,
                AncientCivilizationCount = 1,
                Radius = 420.0f,
            });

    private static void RequireClose(double actual, double expected, string message)
    {
        if (Math.Abs(actual - expected) > 0.000000001)
            throw new InvalidOperationException($"{message}: expected {expected}, got {actual}");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
