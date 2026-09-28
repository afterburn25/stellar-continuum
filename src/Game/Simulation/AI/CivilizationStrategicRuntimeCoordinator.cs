using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Industry;
using Game.Simulation.Models;
using Game.Simulation.Shipbuilding;

namespace Game.Simulation.AI;

/// <summary>
/// Bounded runtime bridge for the early-release Civilization strategic planner.
///
/// Foreign information is supplied through an observer-local IStrategicKnowledgeProvider.
/// Isolated simulation usage defaults to EmptyStrategicKnowledgeProvider; the integrated
/// save-v9 campaign runtime can inject persisted Diplomacy views without exposing hidden state.
///
/// Strategic reviews are derived state and are not persisted. A campaign change clears the
/// planner/provider cache, and each civilization is reviewed only when its scheduled review
/// boundary is reached.
/// </summary>
public sealed class CivilizationStrategicRuntimeCoordinator
{
    private readonly CivilizationStrategicDirector _director;
    private readonly StrategicIndustryPriorityProvider _industryPriorities;
    private readonly StrategicShipbuildingPreferenceProvider _shipbuildingPreferences;
    private readonly IStrategicKnowledgeProvider _knowledgeProvider;
    private readonly Dictionary<int, long> _nextReviewTick = new();
    private long? _campaignSeed;
    private double _strategicDays;

    public CivilizationStrategicRuntimeCoordinator(
        CivilizationStrategicDirector? director = null,
        StrategicIndustryPriorityProvider? industryPriorities = null,
        IStrategicKnowledgeProvider? knowledgeProvider = null,
        StrategicShipbuildingPreferenceProvider? shipbuildingPreferences = null)
    {
        _director = director ?? new CivilizationStrategicDirector();
        _industryPriorities = industryPriorities ?? new StrategicIndustryPriorityProvider();
        _knowledgeProvider = knowledgeProvider ?? new EmptyStrategicKnowledgeProvider();
        _shipbuildingPreferences = shipbuildingPreferences ?? new StrategicShipbuildingPreferenceProvider();
    }

    public IIndustryPriorityProvider IndustryPriorityProvider => _industryPriorities;
    public IShipbuildingStrategicPreferenceView ShipbuildingStrategicPreferenceView => _shipbuildingPreferences;
    public int PublishedIntentCount => _industryPriorities.PublishedIntentCount;

    /// <summary>
    /// Advances the derived strategic review clock and publishes only reviews that reached
    /// their scheduled boundary. Player and seeded-ancient civilizations remain untouched.
    /// </summary>
    public IReadOnlyList<CivilizationStrategicReview> Advance(GalaxyState galaxy, double simulationDays)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (!double.IsFinite(simulationDays) || simulationDays < 0.0)
            throw new ArgumentOutOfRangeException(nameof(simulationDays), "Strategic time must be finite and non-negative.");

        EnsureCampaign(galaxy.Seed);
        _strategicDays += simulationDays;
        var nowTick = Math.Max(0L, (long)Math.Floor(_strategicDays));

        var reviews = new List<CivilizationStrategicReview>();
        foreach (var civilization in galaxy.Civilizations
                     .Where(civilization => !civilization.IsPlayer && !civilization.IsSeededAncient)
                     .OrderBy(civilization => civilization.Id))
        {
            if (_nextReviewTick.TryGetValue(civilization.Id, out var nextTick) && nowTick < nextTick)
                continue;

            var knowledge = _knowledgeProvider.Build(civilization.Id, nowTick);
            if (knowledge.ObservedAtTick > nowTick)
                throw new InvalidOperationException("Strategic knowledge provider returned observations from the future.");

            var review = _director.Review(
                galaxy,
                civilization.Id,
                civilization.Traits,
                knowledge,
                nowTick,
                forceReview: false);

            _industryPriorities.Publish(review);
            _shipbuildingPreferences.Publish(review);
            _nextReviewTick[civilization.Id] = review.Plan.ReviewAfterTick;
            reviews.Add(review);
        }

        return reviews;
    }

    public IndustryPriorityWeights GetIndustryWeights(int civilizationId) =>
        _industryPriorities.GetWeights(civilizationId);

    public ShipbuildingStrategicPreference GetShipbuildingPreference(int civilizationId) =>
        _shipbuildingPreferences.GetPreference(civilizationId);

    public void Reset()
    {
        _campaignSeed = null;
        _strategicDays = 0.0;
        _nextReviewTick.Clear();
        _director.Clear();
        _industryPriorities.Clear();
        _shipbuildingPreferences.Clear();
    }

    private void EnsureCampaign(long seed)
    {
        if (_campaignSeed == seed)
            return;

        _campaignSeed = seed;
        _strategicDays = 0.0;
        _nextReviewTick.Clear();
        _director.Clear();
        _industryPriorities.Clear();
        _shipbuildingPreferences.Clear();
    }
}
