using System;
using System.Collections.Generic;
using System.Linq;

namespace Game.Simulation.Shipbuilding;

public sealed class ShipyardState
{
    public const int MaxPendingBuilds = 8;
    public const int MaxOrderIdLength = 128;

    private readonly List<ShipBuildOrderState> _queuedBuilds = new();

    public required int CivilizationId { get; init; }
    public long NextOrderSequence { get; set; } = 1;
    public string? ActiveDesignId { get; set; }
    public string? ActiveOrderId { get; set; }
    public double ActiveBuildProgress { get; set; }
    public double ActiveAuthorizationCredits { get; set; }
    public double ReservedPopulationMillions { get; set; }
    public string? ReservedPopulationSpeciesId { get; set; }
    public int? ReservedPopulationSourceColonyId { get; set; }

    /// <summary>
    /// Queue access validates only the invariants needed to prevent population-bearing build
    /// records from being silently omitted by bounded persistence. Runtime order creation keeps
    /// the queue within bounds already; these checks are defensive against corrupt/injected state.
    /// </summary>
    public List<ShipBuildOrderState> QueuedBuilds
    {
        get
        {
            ValidatePopulationPersistenceSafety();
            return _queuedBuilds;
        }
    }

    public int PendingBuildCount => (ActiveDesignId is null ? 0 : 1) + _queuedBuilds.Count;

    public static string FormatOrderId(int civilizationId, long sequence) =>
        $"shipyard-{civilizationId}-{sequence}";

    public static bool TryReadCanonicalSequence(string? orderId, int civilizationId, out long sequence)
    {
        sequence = 0;
        var prefix = $"shipyard-{civilizationId}-";
        return orderId is not null && orderId.StartsWith(prefix, StringComparison.Ordinal) &&
               long.TryParse(orderId.AsSpan(prefix.Length), out sequence) && sequence > 0;
    }

    public static bool IsValidPersistedOrderId(string? orderId) =>
        !string.IsNullOrWhiteSpace(orderId) && orderId.Length <= MaxOrderIdLength &&
        orderId.All(character => character is >= 'a' and <= 'z' or >= 'A' and <= 'Z' or >= '0' and <= '9' or '-' or '_');

    private void ValidatePopulationPersistenceSafety()
    {
        var knownDesignIds = ShipDesignRegistry.All
            .Select(design => design.Id)
            .ToHashSet(StringComparer.Ordinal);

        if (!double.IsFinite(ReservedPopulationMillions))
        {
            throw new InvalidOperationException(
                $"Shipyard {CivilizationId} has non-finite active reserved population; refusing to persist ambiguous colonist state.");
        }

        var activePopulation = Math.Max(0.0, ReservedPopulationMillions);
        var hasKnownActiveDesign = !string.IsNullOrWhiteSpace(ActiveDesignId) &&
                                   knownDesignIds.Contains(ActiveDesignId);

        if (activePopulation > 0.0 && !hasKnownActiveDesign)
        {
            throw new InvalidOperationException(
                $"Shipyard {CivilizationId} has {activePopulation:0.###} million reserved population without a valid active design; refusing a state transition that could discard reserved colonists.");
        }

        // CampaignSaveService serializes at most MaxPendingBuilds queued records. During load,
        // an actually valid active design consumes one of the same pending-build slots, while an
        // invalid zero-population active design is sanitized away and therefore consumes none.
        var loaderQueueCapacity = Math.Max(0, MaxPendingBuilds - (hasKnownActiveDesign ? 1 : 0));
        var acceptedQueueEntries = 0;

        for (var index = 0; index < _queuedBuilds.Count; index++)
        {
            var build = _queuedBuilds[index];
            if (!double.IsFinite(build.ReservedPopulationMillions))
            {
                throw new InvalidOperationException(
                    $"Shipyard {CivilizationId} queued build '{build.DesignId}' has non-finite reserved population; refusing to persist ambiguous colonist state.");
            }

            var population = Math.Max(0.0, build.ReservedPopulationMillions);

            // Anything after the serializer's hard queue cap is omitted before it reaches disk.
            if (index >= MaxPendingBuilds)
            {
                if (population > 0.0)
                {
                    throw new InvalidOperationException(
                        $"Shipyard {CivilizationId} has an unserialized overflow build '{build.DesignId}' retaining {population:0.###} million reserved population; refusing to truncate reserved colonists.");
                }

                continue;
            }

            var hasKnownDesign = !string.IsNullOrWhiteSpace(build.DesignId) &&
                                 knownDesignIds.Contains(build.DesignId);
            if (!hasKnownDesign)
            {
                if (population > 0.0)
                {
                    throw new InvalidOperationException(
                        $"Shipyard {CivilizationId} queued build '{build.DesignId}' has {population:0.###} million reserved population but no valid design; refusing to discard reserved colonists.");
                }

                // The loader deliberately drops invalid zero-population metadata.
                continue;
            }

            if (acceptedQueueEntries >= loaderQueueCapacity)
            {
                if (population > 0.0)
                {
                    throw new InvalidOperationException(
                        $"Shipyard {CivilizationId} queue exceeds its bounded capacity while overflow build '{build.DesignId}' retains {population:0.###} million reserved population; refusing to truncate reserved colonists.");
                }

                // A valid zero-population entry beyond the logical pending-build capacity can be
                // safely omitted by the loader without changing any physical population.
                continue;
            }

            acceptedQueueEntries++;
        }
    }
}

public sealed class ShipBuildOrderState
{
    public string OrderId { get; init; } = string.Empty;
    public required string DesignId { get; init; }
    public double AuthorizationCredits { get; init; }
    public double ReservedPopulationMillions { get; init; }
    public string? ReservedPopulationSpeciesId { get; init; }
    public int? ReservedPopulationSourceColonyId { get; init; }
}
