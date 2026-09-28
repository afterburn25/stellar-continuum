using System.Runtime.CompilerServices;
using Game.Simulation.Generation;
using Game.Simulation.Models;
using Game.Simulation.Species;

internal static class ColonyHabitatSupportBurdenChecks
{
    [ModuleInitializer]
    internal static void Initialize()
    {
        Run();
        Console.WriteLine("PASS: raw colony habitat-support burden boundaries");
    }

    public static void Run()
    {
        ValidateGeneratedFoundingColoniesExposeExactBurden();
        ValidateSpeciesPhysiologyChangesRawBurdenWithoutEnvironmentGuess();
        ValidateHabitatSupportedFallbackPreservesPhysicalRequirements();
    }

    private static void ValidateGeneratedFoundingColoniesExposeExactBurden()
    {
        var galaxy = CreateGalaxy();
        var view = new CurrentColonyHabitatSupportBurdenView();

        foreach (var colony in galaxy.Colonies.OrderBy(colony => colony.Id))
        {
            var burden = view.Build(galaxy, colony.Id);
            var species = SpeciesCatalog.Get(colony.PopulationSpeciesId);
            var metabolic = SpeciesMetabolicEnvelopeEvaluator.Evaluate(
                SpeciesPopulationCohort.Founding(species.Id, colony.PopulationMillions));

            Require(colony.PlanetaryBodyId is not null,
                $"new founding colony {colony.Id} unexpectedly lacks exact body identity");
            Require(burden.UsesExactOccupiedBody && burden.Environment is not null,
                $"new founding colony {colony.Id} did not expose exact environmental support requirements");
            Require(burden.Environment.PlanetaryBodyId == colony.PlanetaryBodyId,
                $"colony {colony.Id} habitat-support burden resolved the wrong body");
            RequireClose(burden.PopulationMillions, colony.PopulationMillions,
                $"colony {colony.Id} habitat-support burden changed population quantity");
            RequireClose(burden.TypicalDayMetabolicDemandMillions, metabolic.TypicalDayAverageDemandMillions,
                $"colony {colony.Id} metabolic burden did not derive from Species metabolism");
            RequireClose(burden.AdultBiomassMillionKg,
                colony.PopulationMillions * species.Physiology.TypicalAdultMassKg,
                $"colony {colony.Id} biomass burden did not derive from Species physiology");

            ValidateCategoryPopulation(
                burden.Environment.RequiresGravityMitigation,
                burden.GravityMitigationPopulationMillions,
                burden.PopulationMillions,
                $"colony {colony.Id} gravity mitigation population");
            ValidateCategoryPopulation(
                burden.Environment.RequiresThermalControl,
                burden.ThermalControlPopulationMillions,
                burden.PopulationMillions,
                $"colony {colony.Id} thermal-control population");
            ValidateCategoryPopulation(
                burden.Environment.RequiresPressureControl,
                burden.PressureControlPopulationMillions,
                burden.PopulationMillions,
                $"colony {colony.Id} pressure-control population");
            ValidateCategoryPopulation(
                burden.Environment.RequiresSealedHabitat,
                burden.SealedHabitatPopulationMillions,
                burden.PopulationMillions,
                $"colony {colony.Id} sealed-habitat population");
            ValidateCategoryPopulation(
                burden.Environment.RequiresArtificialBiosphere,
                burden.ArtificialBiospherePopulationMillions,
                burden.PopulationMillions,
                $"colony {colony.Id} artificial-biosphere population");
            ValidateCategoryPopulation(
                burden.Environment.RequiresRadiationShielding,
                burden.RadiationShieldingPopulationMillions,
                burden.PopulationMillions,
                $"colony {colony.Id} radiation-shielding population");
        }
    }

    private static void ValidateSpeciesPhysiologyChangesRawBurdenWithoutEnvironmentGuess()
    {
        var galaxy = CreateGalaxy();
        var civilization = galaxy.Civilizations.OrderBy(c => c.Id).First();
        const double population = 1000.0;

        var terran = AddLegacyNullBodyColony(
            galaxy,
            civilization.Id,
            civilization.HomeSystemId,
            SpeciesCatalog.TerranBaselineId,
            population);
        var highGravity = AddLegacyNullBodyColony(
            galaxy,
            civilization.Id,
            civilization.HomeSystemId,
            SpeciesCatalog.CompactHighGravityId,
            population);

        var view = new CurrentColonyHabitatSupportBurdenView();
        var terranBurden = view.Build(galaxy, terran.Id);
        var highGravityBurden = view.Build(galaxy, highGravity.Id);

        Require(terranBurden.Environment is null && highGravityBurden.Environment is null,
            "legacy null-body burden incorrectly inferred environmental support requirements");
        Require(!terranBurden.RequiresEnvironmentalSupport && !highGravityBurden.RequiresEnvironmentalSupport,
            "legacy null-body burden incorrectly claimed exact environmental support");
        Require(terranBurden.AdultBiomassMillionKg != highGravityBurden.AdultBiomassMillionKg,
            "different Species body mass did not change raw colony biomass burden");
        Require(terranBurden.TypicalDayMetabolicDemandMillions != highGravityBurden.TypicalDayMetabolicDemandMillions,
            "different Species metabolism did not change raw colony life-support demand");
        RequireClose(terranBurden.GravityMitigationPopulationMillions, 0.0,
            "legacy Terran colony claimed guessed gravity-control population");
        RequireClose(highGravityBurden.SealedHabitatPopulationMillions, 0.0,
            "legacy high-gravity colony claimed guessed sealed-habitat population");
    }

    private static void ValidateHabitatSupportedFallbackPreservesPhysicalRequirements()
    {
        var galaxy = CreateGalaxy();
        var habitability = new SpeciesPlanetaryHabitabilityEvaluator();
        var candidate = (
            from species in SpeciesCatalog.All
            from body in galaxy.PlanetaryBodies
            let assessment = habitability.Evaluate(body, species.Id)
            where assessment.Viability == SpeciesColonizationViability.HabitatSupportedFallback
            orderby body.Id, species.Id
            select new { Species = species, Body = body, Assessment = assessment })
            .FirstOrDefault()
            ?? throw new InvalidOperationException("Validation galaxy contained no habitat-supported fallback pair.");

        var colony = new ColonyState
        {
            Id = NextColonyId(galaxy),
            CivilizationId = galaxy.Civilizations.OrderBy(c => c.Id).First().Id,
            SystemId = candidate.Body.SystemId,
            PlanetaryBodyId = candidate.Body.Id,
            Name = "Fallback Habitat Burden Test",
            PopulationSpeciesId = candidate.Species.Id,
            PopulationMillions = 500.0,
            Infrastructure = 1.0,
            Stability = 1.0,
        };
        galaxy.Colonies.Add(colony);

        var burden = new CurrentColonyHabitatSupportBurdenView().Build(galaxy, colony.Id);
        Require(burden.Environment is not null,
            "exact fallback colony did not expose environmental support requirements");
        Require(burden.Environment.ColonizationViability == SpeciesColonizationViability.HabitatSupportedFallback,
            "fallback colony lost its authoritative viability class in the support burden");
        Require(burden.Environment.UsesPrototypeHabitatSupportedFallback,
            "fallback colony was not marked as relying on prototype habitat support");
        Require(burden.Environment.RequiredMitigationCategories > 0,
            "fallback colony exposed no physical mitigation requirements");
        Require(burden.RequiresEnvironmentalSupport,
            "fallback colony with mitigation categories did not report environmental support burden");
    }

    private static ColonyState AddLegacyNullBodyColony(
        GalaxyState galaxy,
        int civilizationId,
        int systemId,
        string speciesId,
        double populationMillions)
    {
        var colony = new ColonyState
        {
            Id = NextColonyId(galaxy),
            CivilizationId = civilizationId,
            SystemId = systemId,
            PlanetaryBodyId = null,
            Name = $"Legacy Burden {speciesId}",
            PopulationSpeciesId = speciesId,
            PopulationMillions = populationMillions,
            Infrastructure = 1.0,
            Stability = 1.0,
        };
        galaxy.Colonies.Add(colony);
        return colony;
    }

    private static void ValidateCategoryPopulation(
        bool required,
        double actualPopulationMillions,
        double totalPopulationMillions,
        string message)
    {
        RequireClose(
            actualPopulationMillions,
            required ? totalPopulationMillions : 0.0,
            message);
    }

    private static int NextColonyId(GalaxyState galaxy) =>
        galaxy.Colonies.Count == 0 ? 0 : galaxy.Colonies.Max(colony => colony.Id) + 1;

    private static GalaxyState CreateGalaxy() =>
        new GalaxyGenerator().Generate(
            0x4841_4249_5441_5453L,
            new GalaxyGenerationSettings
            {
                SystemCount = 72,
                PreWarpCivilizationCount = 6,
                AncientCivilizationCount = 1,
                Radius = 650.0f,
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
