using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.AI;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Generation;

public sealed class CivilizationSeeder
{
    private static readonly CivilizationTemplate[] PreWarpTemplates =
    {
        new("Aster Union", CivilizationArchetype.Adaptive, new CivilizationTraits(0.35, 0.25, 0.30, 0.55, 0.45, 1.00)),
        new("Kesh Exchange", CivilizationArchetype.Mercantile, new CivilizationTraits(0.20, 0.15, 0.80, 0.45, 0.30, 1.00)),
        new("Velari Institute", CivilizationArchetype.Scientific, new CivilizationTraits(0.18, 0.20, 0.25, 0.95, 0.25, 1.00)),
        new("Dravak Compact", CivilizationArchetype.Militarist, new CivilizationTraits(0.82, 0.58, 0.30, 0.30, 0.62, 1.00)),
        new("Orryn Enclave", CivilizationArchetype.Isolationist, new CivilizationTraits(0.12, 0.55, 0.18, 0.58, 0.18, 1.00)),
        new("Tarkesh Reach", CivilizationArchetype.Territorial, new CivilizationTraits(0.56, 0.92, 0.38, 0.32, 0.48, 1.00)),
        new("Seren Accord", CivilizationArchetype.Diplomatic, new CivilizationTraits(0.12, 0.08, 0.22, 0.52, 0.22, 1.00)),
        new("Kor Vow", CivilizationArchetype.HonorBound, new CivilizationTraits(0.66, 0.38, 0.16, 0.28, 0.78, 0.88, HonorBound: true)),
        new("Namar Coalition", CivilizationArchetype.Adaptive, new CivilizationTraits(0.28, 0.24, 0.42, 0.48, 0.38, 1.00)),
        new("Ilyr Concord", CivilizationArchetype.Diplomatic, new CivilizationTraits(0.10, 0.12, 0.30, 0.60, 0.20, 1.00)),
        new("Vask Dominion", CivilizationArchetype.Militarist, new CivilizationTraits(0.76, 0.62, 0.24, 0.26, 0.58, 1.00)),
        new("Pelagos Combine", CivilizationArchetype.Mercantile, new CivilizationTraits(0.18, 0.18, 0.76, 0.50, 0.26, 1.00)),
        new("Thren Observatory", CivilizationArchetype.Scientific, new CivilizationTraits(0.14, 0.16, 0.22, 0.90, 0.24, 1.00)),
    };

    private static readonly CivilizationTemplate[] AncientTemplates =
    {
        new("Aurelian Custodians", CivilizationArchetype.AncientCustodian, new CivilizationTraits(0.08, 0.08, 0.05, 0.96, 0.10, 1.00)),
        new("Veyr Archive", CivilizationArchetype.AncientArchivist, new CivilizationTraits(0.04, 0.04, 0.04, 1.00, 0.08, 1.00)),
        new("Orison Keepers", CivilizationArchetype.AncientCustodian, new CivilizationTraits(0.10, 0.06, 0.03, 0.92, 0.12, 1.00)),
    };

    /// <summary>
    /// Compatibility overload for legacy migration/tests. Physical worlds are regenerated from
    /// the same canonical deterministic planet catalog used by a live campaign, then the normal
    /// species-relative homeworld planner is used.
    /// </summary>
    public List<CivilizationState> Seed(
        IReadOnlyList<StarSystemState> systems,
        int preWarpCount,
        int ancientCount,
        long seed) =>
        Seed(
            systems,
            new PlanetaryBodyGenerator().Generate(seed, systems),
            preWarpCount,
            ancientCount,
            seed);

    public List<CivilizationState> Seed(
        IReadOnlyList<StarSystemState> systems,
        IReadOnlyList<PlanetaryBodyState> planetaryBodies,
        int preWarpCount,
        int ancientCount,
        long seed,
        string playerSpeciesId = SpeciesCatalog.TerranBaselineId)
    {
        ArgumentNullException.ThrowIfNull(systems);
        ArgumentNullException.ThrowIfNull(planetaryBodies);

        if (preWarpCount < 1 || preWarpCount > PreWarpTemplates.Length)
            throw new ArgumentOutOfRangeException(nameof(preWarpCount));
        if (ancientCount < 0 || ancientCount > AncientTemplates.Length)
            throw new ArgumentOutOfRangeException(nameof(ancientCount));
        if (!SpeciesCatalog.TryGet(playerSpeciesId, out _))
            throw new ArgumentException($"Unknown player species '{playerSpeciesId}'.", nameof(playerSpeciesId));
        var civilizationCount = preWarpCount + ancientCount;
        if (systems.Count < civilizationCount)
            throw new InvalidOperationException("There are fewer star systems than seeded civilizations.");

        // Fresh canonical starts reserve the one human founding faction; other physiology
        // remains deterministic. Legacy catalogs retain the original assignment policy.
        // AI archetype/template selection is intentionally independent from biology.
        var canonicalStarts = systems.Any(SolCatalogPreset.IsSol);
        var speciesIds = Enumerable.Range(0, civilizationCount)
            .Select(civilizationId => canonicalStarts
                ? SpeciesAssignmentPolicy.AssignNewCampaign(seed, civilizationId)
                : SpeciesAssignmentPolicy.Assign(seed, civilizationId))
            .ToArray();
        // Preserve the canonical Human faction whenever the player selected rivals. With zero
        // rivals, a nonhuman player owns the sole founding faction instead of silently creating
        // a Human opponent that contradicts the campaign option.
        var playerCivilizationId = playerSpeciesId == SpeciesCatalog.TerranBaselineId || preWarpCount == 1 ? 0 : 1;
        if (canonicalStarts && playerSpeciesId != SpeciesCatalog.TerranBaselineId)
            speciesIds[playerCivilizationId] = playerSpeciesId;
        var homeworlds = new SpeciesHomeworldPlanner()
            .Plan(systems, planetaryBodies, speciesIds)
            .ToDictionary(assignment => assignment.CivilizationId);

        var random = new Random(unchecked((int)((seed * 397) ^ (seed >> 32) ^ 0x51A7C0DE)));
        var preWarpDeck = PreWarpTemplates.OrderBy(_ => random.Next()).Take(preWarpCount).ToArray();
        var ancientDeck = AncientTemplates.OrderBy(_ => random.Next()).Take(ancientCount).ToArray();
        var civilizations = new List<CivilizationState>(civilizationCount);

        for (var i = 0; i < preWarpCount; i++)
        {
            var template = preWarpDeck[i];
            var civilizationId = civilizations.Count;
            var home = homeworlds[civilizationId];
            civilizations.Add(new CivilizationState(
                civilizationId,
                canonicalStarts && speciesIds[civilizationId] == SpeciesCatalog.TerranBaselineId && civilizationId == 0
                    ? "Human Commonwealth"
                    : template.Name,
                home.SystemId,
                template.Archetype,
                template.Traits,
                i == playerCivilizationId,
                CivilizationDevelopmentStage.PreWarp,
                false,
                true,
                false,
                home.SpeciesId));
        }

        for (var i = 0; i < ancientCount; i++)
        {
            var template = ancientDeck[i];
            var civilizationId = civilizations.Count;
            var home = homeworlds[civilizationId];
            civilizations.Add(new CivilizationState(
                civilizationId,
                template.Name,
                home.SystemId,
                template.Archetype,
                template.Traits,
                false,
                CivilizationDevelopmentStage.AncientSpacefaring,
                true,
                false,
                true,
                home.SpeciesId));
        }

        return civilizations.Select(c => c with
        {
            Leadership = CivilizationLeadershipState.CreateFoundingRoster(c.Id, c.SpeciesId == SpeciesCatalog.TerranBaselineId),
        }).ToList();
    }

    private sealed record CivilizationTemplate(
        string Name,
        CivilizationArchetype Archetype,
        CivilizationTraits Traits);
}
