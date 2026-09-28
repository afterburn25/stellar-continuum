using System.Runtime.CompilerServices;
using Game.Simulation.Colonization;
using Game.Simulation.Generation;
using Game.Simulation.Species;

namespace Game.Simulation.Validation;

internal static class ColonySpeciesEnvironmentValidation
{
    [Game.Validation.RegressionCheck]
    internal static void Run()
    {
        ValidateReadOnlyColonyEnvironmentSnapshot();
        Console.WriteLine("PASS: read-only colony species environment burden view");
    }

    private static void ValidateReadOnlyColonyEnvironmentSnapshot()
    {
        var galaxy = new GalaxyGenerator().Generate(
            0x434F_4C4F_4E59_5350L,
            new GalaxyGenerationSettings
            {
                SystemCount = 40,
                PreWarpCivilizationCount = 5,
                AncientCivilizationCount = 1,
                Radius = 470.0f,
            });

        var colonization = new ColonizationSimulation();
        var colony = galaxy.Colonies
            .OrderBy(candidate => candidate.Id)
            .First(candidate => colonization.ResolveCompatibilityColonyWorld(galaxy, candidate) is not null);
        var body = colonization.ResolveCompatibilityColonyWorld(galaxy, colony)
            ?? throw new InvalidOperationException("validation colony did not resolve a physical world");

        var originalPopulation = colony.PopulationMillions;
        var originalInfrastructure = colony.Infrastructure;
        var originalStability = colony.Stability;
        var originalSpecies = colony.PopulationSpeciesId;
        var originalBodyId = colony.PlanetaryBodyId;

        var view = new ColonySpeciesEnvironmentView();
        var snapshot = view.Build(galaxy, colony.Id);
        var habitability = new SpeciesPlanetaryHabitabilityEvaluator().Evaluate(body, colony.PopulationSpeciesId);
        var cohort = CurrentPopulationSpeciesBridge.FromColony(colony).AsUnadaptedCohort();
        var habitat = PlanetaryHabitatEnvironmentMapper.Map(body.Environment);
        var requirements = new SpeciesPopulationRequirementsEvaluator().Evaluate(cohort, habitat);
        var metabolism = SpeciesMetabolicEnvelopeEvaluator.Evaluate(cohort);

        Require(snapshot.ColonyId == colony.Id, "colony environment snapshot changed colony identity");
        Require(snapshot.PlanetaryBodyId == body.Id, "colony environment snapshot did not resolve the occupied physical world");
        Require(snapshot.SpeciesId == colony.PopulationSpeciesId, "colony environment snapshot changed population species identity");
        Require(Math.Abs(snapshot.PopulationMillions - colony.PopulationMillions) < 0.0000001, "colony environment snapshot changed population quantity");
        Require(Math.Abs(snapshot.NaturalHabitability - habitability.Environment.NaturalHabitability) < 0.0000001, "colony environment snapshot changed natural habitability");
        Require(Math.Abs(snapshot.UnprotectedOperationalCapacity - habitability.Environment.UnprotectedOperationalCapacity) < 0.0000001, "colony environment snapshot changed operational capacity");
        Require(snapshot.LimitingFactor == habitability.Environment.LimitingFactor, "colony environment snapshot changed limiting factor");
        Require(snapshot.ColonizationViability == habitability.Viability, "colony environment snapshot changed colonization viability");
        Require(Math.Abs(snapshot.AdultBiomassMillionKg - requirements.AdultBiomassMillionKg) < 0.0000001, "colony environment snapshot changed derived biomass");
        Require(Math.Abs(snapshot.TypicalDayMetabolicDemandMillions - metabolism.TypicalDayAverageDemandMillions) < 0.0000001, "colony environment snapshot changed metabolic demand");
        Require(snapshot.RequiredEnvironmentalMitigationCategories == requirements.RequiredEnvironmentalMitigationCategories, "colony environment snapshot changed mitigation category count");

        // The view is intentionally non-authoritative: calculating biological burden must not
        // mutate current population/economy-facing colony state.
        Require(Math.Abs(colony.PopulationMillions - originalPopulation) < 0.0000001, "colony burden read mutated population");
        Require(Math.Abs(colony.Infrastructure - originalInfrastructure) < 0.0000001, "colony burden read mutated infrastructure");
        Require(Math.Abs(colony.Stability - originalStability) < 0.0000001, "colony burden read mutated stability");
        Require(colony.PopulationSpeciesId == originalSpecies, "colony burden read mutated population species");
        Require(colony.PlanetaryBodyId == originalBodyId, "colony burden read mutated colony body identity");

        var civilizationViews = view.BuildForCivilization(galaxy, colony.CivilizationId);
        Require(civilizationViews.Any(candidate => candidate.ColonyId == colony.Id),
            "civilization colony burden view omitted an owned populated colony");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
