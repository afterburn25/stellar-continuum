using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Combat;
using Game.Simulation.Construction;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Simulation.Shipbuilding;

public sealed class ShipbuildingSimulation
{
    public const double IndustryPerDay = 20;
    private readonly IShipbuildingCapabilityView _capabilityView;
    private readonly IShipbuildingStrategicPreferenceView? _strategicPreferenceView;

    public ShipbuildingSimulation(
        IShipbuildingCapabilityView? capabilityView = null,
        IShipbuildingStrategicPreferenceView? strategicPreferenceView = null)
    {
        _capabilityView = capabilityView ?? new PrototypeShipbuildingCapabilityView();
        _strategicPreferenceView = strategicPreferenceView;
    }

    public IReadOnlyList<ShipbuildingEvent> Advance(
        GalaxyState galaxy,
        IReadOnlyDictionary<int, double>? industryBudgets = null, double simulationDays = 1) =>
        AdvanceCore(galaxy, industryBudgets, null, simulationDays);

    public ShipPropulsionPerformance GetEffectivePropulsion(
        GalaxyState galaxy,
        int civilizationId,
        ShipDesignDefinition design)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        ArgumentNullException.ThrowIfNull(design);
        if (_capabilityView.HasCivilizationCapability(
                galaxy, civilizationId, ShipbuildingCapabilityIds.ExtendedInterstellarTransit))
        {
            return new ShipPropulsionPerformance(
                design.StrategicSpeed * 1.35,
                design.MaximumLegRangeLightYears * 1.75,
                design.FuelEnduranceLightYears * 1.75,
                "Long-range warp architecture");
        }
        if (_capabilityView.HasCivilizationCapability(
                galaxy, civilizationId, ShipbuildingCapabilityIds.ReliableInterstellarTransit))
        {
            return new ShipPropulsionPerformance(
                design.StrategicSpeed * 1.18,
                design.MaximumLegRangeLightYears * 1.30,
                design.FuelEnduranceLightYears * 1.35,
                "Stable warp drive");
        }
        return new ShipPropulsionPerformance(
            design.StrategicSpeed,
            design.MaximumLegRangeLightYears,
            design.FuelEnduranceLightYears,
            "Prototype warp drive");
    }

    public IReadOnlyList<ShipbuildingEvent> AdvanceForCivilization(GalaxyState galaxy, int civilizationId,
        double industryBudget, double simulationDays = 1) => AdvanceCore(galaxy,
            new Dictionary<int, double> { [civilizationId] = industryBudget }, civilizationId, simulationDays);

    private IReadOnlyList<ShipbuildingEvent> AdvanceCore(GalaxyState galaxy,
        IReadOnlyDictionary<int, double>? industryBudgets, int? onlyCivilizationId, double simulationDays)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (!double.IsFinite(simulationDays) || simulationDays < 0) throw new ArgumentOutOfRangeException(nameof(simulationDays));
        if (simulationDays == 0) return Array.Empty<ShipbuildingEvent>();
        if (onlyCivilizationId is null) EnsureAutomaticOrders(galaxy);

        var events = new List<ShipbuildingEvent>();

        foreach (var civilization in galaxy.Civilizations)
        {
            if (onlyCivilizationId is int selected && civilization.Id != selected) continue;
            if (civilization.IsSeededAncient)
                continue;

            var state = galaxy.ShipyardStates.First(s => s.CivilizationId == civilization.Id);
            if (state.ActiveDesignId is null)
                continue;

            var definition = ShipDesignRegistry.Get(state.ActiveDesignId);
            var economy = galaxy.Economies.First(e => e.CivilizationId == civilization.Id);
            var remaining = Math.Max(0.0, definition.IndustryCost - state.ActiveBuildProgress);
            var availableIndustry = ResolveBudget(industryBudgets, civilization.Id, economy.Industry);
            var spend = Math.Min(remaining, Math.Min(availableIndustry, IndustryPerDay * simulationDays));
            if (spend <= 0.0 && remaining > 0.0001)
                continue;

            economy.Industry -= spend;
            state.ActiveBuildProgress += spend;

            if (state.ActiveBuildProgress + 0.0001 < definition.IndustryCost)
                continue;

            // Population reserved when a colony ship was ordered becomes physical cargo on
            // the completed fleet. Species identity follows the same conservation chain.
            var embarkedPopulation = state.ReservedPopulationMillions;
            var embarkedPopulationSpeciesId = state.ReservedPopulationSpeciesId;
            var fleet = CreateFleet(
                galaxy,
                civilization,
                definition,
                embarkedPopulation,
                embarkedPopulationSpeciesId);
            galaxy.Fleets.Add(fleet);
            state.ActiveDesignId = null;
            state.ActiveOrderId = null;
            state.ActiveBuildProgress = 0.0;
            state.ActiveAuthorizationCredits = 0.0;
            state.ReservedPopulationMillions = 0.0;
            state.ReservedPopulationSpeciesId = null;
            state.ReservedPopulationSourceColonyId = null;
            PromoteNextBuild(state);
            events.Add(new ShipbuildingEvent(civilization.Id, fleet.Id, definition.Id, $"{civilization.Name} completed {fleet.Name}."));
        }

        return events;
    }

    /// <summary>
    /// Selects missing AI ship orders without spending Industry. Population reservation remains
    /// part of order creation, while Core can resolve shared Industry before production advances.
    /// Strategic preference is advisory: it may fill one currently missing role, but never bypasses
    /// design prerequisites, duplicate-role bounds, expansion policy or colony population rules.
    /// </summary>
    public void EnsureAutomaticOrders(GalaxyState galaxy)
    {
        ArgumentNullException.ThrowIfNull(galaxy);

        foreach (var civilization in galaxy.Civilizations)
        {
            if (civilization.IsSeededAncient || civilization.IsPlayer)
                continue;

            var state = galaxy.ShipyardStates.First(s => s.CivilizationId == civilization.Id);
            if (state.ActiveDesignId is not null)
                continue;

            var preference = _strategicPreferenceView?.GetPreference(civilization.Id)
                ?? ShipbuildingStrategicPreference.None;
            var design = SelectAiDesign(galaxy, civilization, preference);
            if (design is not null)
                TryStartBuild(galaxy, civilization.Id, design.Id, out _);
        }
    }

    public double GetIndustryDemand(GalaxyState galaxy, int civilizationId, double simulationDays = double.PositiveInfinity)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var state = galaxy.ShipyardStates.First(s => s.CivilizationId == civilizationId);
        if (state.ActiveDesignId is null)
            return 0.0;

        var definition = ShipDesignRegistry.Get(state.ActiveDesignId);
        return Math.Min(Math.Max(0.0, definition.IndustryCost - state.ActiveBuildProgress), IndustryPerDay * Math.Max(0, simulationDays));
    }

    public ShipbuildingOrderResult StartBuild(GalaxyState galaxy, int civilizationId, string designId)
    {
        var accepted = TryStartBuild(galaxy, civilizationId, designId, out var message);
        return new ShipbuildingOrderResult(accepted, message);
    }

    public IReadOnlyList<ShipDesignDefinition> GetAvailableDesigns(GalaxyState galaxy, int civilizationId)
    {
        return ShipDesignRegistry.All
            .Where(design => GetLockReason(galaxy, civilizationId, design) is null)
            .ToArray();
    }

    public string? GetLockReason(GalaxyState galaxy, int civilizationId, ShipDesignDefinition design)
    {
        var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == civilizationId);
        var missing = design.Prerequisites.AllCivilizationCapabilities
            .Where(capabilityId => !_capabilityView.HasCivilizationCapability(galaxy, civilizationId, capabilityId))
            .Select(ShipbuildingCapabilityIds.DisplayName)
            .Concat(design.Prerequisites.RequiredConstructionProjects
                .Where(projectId => !construction.CompletedProjectIds.Contains(projectId))
                .Select(projectId => ConstructionRegistry.Get(projectId).Name))
            .ToList();
        if (design.Prerequisites.AnyCivilizationCapabilities.Count > 0 &&
            !design.Prerequisites.AnyCivilizationCapabilities.Any(capabilityId =>
                _capabilityView.HasCivilizationCapability(galaxy, civilizationId, capabilityId)))
            missing.Add("one of " + string.Join(" or ", design.Prerequisites.AnyCivilizationCapabilities
                .Select(ShipbuildingCapabilityIds.DisplayName)));
        return missing.Count == 0 ? null : "requires " + string.Join(", ", missing);
    }

    private bool TryStartBuild(GalaxyState galaxy, int civilizationId, string designId, out string message)
    {
        var civilization = galaxy.Civilizations.FirstOrDefault(c => c.Id == civilizationId);
        if (civilization is null)
        {
            message = "Unknown civilization.";
            return false;
        }

        var state = galaxy.ShipyardStates.First(s => s.CivilizationId == civilizationId);
        if (state.PendingBuildCount >= ShipyardState.MaxPendingBuilds)
        {
            message = $"The shipyard queue is full ({ShipyardState.MaxPendingBuilds} pending vessels maximum).";
            return false;
        }

        ShipDesignDefinition definition;
        try { definition = ShipDesignRegistry.Get(designId); }
        catch (InvalidOperationException)
        {
            message = "Unknown ship design.";
            return false;
        }

        if (GetLockReason(galaxy, civilizationId, definition) is { } lockReason)
        {
            message = $"{definition.Name} {lockReason}.";
            return false;
        }

        var economy = galaxy.Economies.First(e => e.CivilizationId == civilizationId);
        var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, civilizationId);
        if (!double.IsFinite(economy.Credits) || economy.Credits + 0.0001 < definition.CreditCost)
        {
            message = $"{currency.Format(definition.CreditCost)} is required to authorize {definition.Name}.";
            return false;
        }

        if (!TryPrepareOrderId(state, out var orderId, out message))
            return false;

        var reservedPopulation = 0.0;
        string? reservedPopulationSpeciesId = null;
        int? reservedPopulationSourceColonyId = null;
        if (definition.PopulationCostMillions > 0.0)
        {
            var source = galaxy.Colonies
                .Where(c => c.CivilizationId == civilizationId)
                .OrderByDescending(c => c.PopulationMillions)
                .FirstOrDefault();
            if (source is null || !double.IsFinite(source.PopulationMillions) || source.PopulationMillions < definition.PopulationCostMillions + 500.0)
            {
                message = $"At least {definition.PopulationCostMillions + 500.0:0} million population is required before reserving colonists for this ship.";
                return false;
            }

            if (!SpeciesCatalog.TryGet(source.PopulationSpeciesId, out _))
            {
                message = $"The source colony references unknown population species '{source.PopulationSpeciesId}'.";
                return false;
            }

            reservedPopulation = definition.PopulationCostMillions;
            reservedPopulationSpeciesId = source.PopulationSpeciesId;
            reservedPopulationSourceColonyId = source.Id;
        }

        if (state.ActiveDesignId is null)
        {
            economy.Credits -= definition.CreditCost;
            if (reservedPopulationSourceColonyId is int sourceId)
                galaxy.Colonies.First(colony => colony.Id == sourceId).PopulationMillions -= reservedPopulation;
            state.ActiveDesignId = definition.Id;
            state.ActiveOrderId = orderId;
            state.ActiveBuildProgress = 0.0;
            state.ActiveAuthorizationCredits = definition.CreditCost;
            state.ReservedPopulationMillions = reservedPopulation;
            state.ReservedPopulationSpeciesId = reservedPopulationSpeciesId;
            state.ReservedPopulationSourceColonyId = reservedPopulationSourceColonyId;
            state.NextOrderSequence++;
            message = $"Ship construction started: {definition.Name}. Authorized for {currency.Format(definition.CreditCost)}.";
            return true;
        }

        economy.Credits -= definition.CreditCost;
        if (reservedPopulationSourceColonyId is int queuedSourceId)
            galaxy.Colonies.First(colony => colony.Id == queuedSourceId).PopulationMillions -= reservedPopulation;
        state.QueuedBuilds.Add(new ShipBuildOrderState
        {
            OrderId = orderId,
            DesignId = definition.Id,
            AuthorizationCredits = definition.CreditCost,
            ReservedPopulationMillions = reservedPopulation,
            ReservedPopulationSpeciesId = reservedPopulationSpeciesId,
            ReservedPopulationSourceColonyId = reservedPopulationSourceColonyId,
        });
        state.NextOrderSequence++;
        message = $"Queued {definition.Name} for {currency.Format(definition.CreditCost)}. {state.PendingBuildCount}/{ShipyardState.MaxPendingBuilds} pending vessel slots are now in use.";
        return true;
    }

    private static double ResolveBudget(
        IReadOnlyDictionary<int, double>? industryBudgets,
        int civilizationId,
        double availableIndustry)
    {
        if (industryBudgets is null)
            return availableIndustry;
        if (!industryBudgets.TryGetValue(civilizationId, out var budget))
            return 0.0;
        if (!double.IsFinite(budget))
            throw new ArgumentOutOfRangeException(nameof(industryBudgets), "Industry budgets must be finite.");

        return Math.Min(availableIndustry, Math.Max(0.0, budget));
    }

    public ShipbuildingCancellationAssessment AssessCancellation(GalaxyState galaxy, int civilizationId, string orderId)
    {
        var state = galaxy.ShipyardStates.FirstOrDefault(s => s.CivilizationId == civilizationId);
        var economy = galaxy.Economies.FirstOrDefault(e => e.CivilizationId == civilizationId);
        if (state is null || economy is null || string.IsNullOrWhiteSpace(orderId))
            return new(false, false, null, 0, "Unknown shipyard order.");

        var activeMatch = state.ActiveOrderId == orderId && state.ActiveDesignId is not null;
        var queuedMatches = state.QueuedBuilds.Select((order, index) => (order, index))
            .Where(candidate => candidate.order.OrderId == orderId).ToArray();
        if ((activeMatch ? 1 : 0) + queuedMatches.Length == 0)
            return new(false, false, null, 0, "That shipyard order is no longer pending.");
        if ((activeMatch ? 1 : 0) + queuedMatches.Length != 1)
            return new(false, false, null, 0, "Shipyard order identity is ambiguous; repair the save before cancelling.");

        var designId = activeMatch ? state.ActiveDesignId! : queuedMatches[0].order.DesignId;
        ShipDesignDefinition design;
        try { design = ShipDesignRegistry.Get(designId); }
        catch (InvalidOperationException)
        { return new(false, activeMatch, designId, 0, "The shipyard order references an unknown design."); }

        var paidQuote = activeMatch ? state.ActiveAuthorizationCredits : queuedMatches[0].order.AuthorizationCredits;
        var progress = activeMatch ? state.ActiveBuildProgress : 0.0;
        var population = activeMatch ? state.ReservedPopulationMillions : queuedMatches[0].order.ReservedPopulationMillions;
        var speciesId = activeMatch ? state.ReservedPopulationSpeciesId : queuedMatches[0].order.ReservedPopulationSpeciesId;
        var sourceId = activeMatch ? state.ReservedPopulationSourceColonyId : queuedMatches[0].order.ReservedPopulationSourceColonyId;
        if (!double.IsFinite(paidQuote) || paidQuote < 0 || !double.IsFinite(progress) || progress < 0 ||
            !double.IsFinite(population) || population < 0)
            return new(false, activeMatch, designId, 0, "The shipyard order has invalid accounting data.");
        if (activeMatch && progress > design.IndustryCost + 0.0001)
            return new(false, true, designId, 0, "The shipyard order records more progress than the design requires.");
        if (!TryResolvePopulationReturn(galaxy, civilizationId, population, speciesId, sourceId, out _, out var reason))
            return new(false, activeMatch, designId, 0, reason!);

        var refund = activeMatch
            ? paidQuote * Math.Clamp((design.IndustryCost - progress) / design.IndustryCost, 0, 1)
            : paidQuote;
        if (!double.IsFinite(economy.Credits) || !double.IsFinite(refund) || economy.Credits > double.MaxValue - refund)
            return new(false, activeMatch, designId, 0, "The refund cannot be represented in the civilization treasury.");
        if (activeMatch && state.QueuedBuilds.Count > 0 && !CanPromote(state.QueuedBuilds[0], out reason))
            return new(false, true, designId, 0, reason!);
        return new(true, activeMatch, designId, refund, null);
    }

    public ShipbuildingCancellationResult CancelBuild(GalaxyState galaxy, int civilizationId, string orderId)
    {
        var assessment = AssessCancellation(galaxy, civilizationId, orderId);
        if (!assessment.CanCancel)
            return new(false, assessment.Blocker ?? "Unknown shipyard order.", 0);

        var state = galaxy.ShipyardStates.Single(s => s.CivilizationId == civilizationId);
        var economy = galaxy.Economies.Single(e => e.CivilizationId == civilizationId);
        var design = ShipDesignRegistry.Get(assessment.DesignId!);
        var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, civilizationId);
        if (assessment.IsActive)
        {
            TryResolvePopulationReturn(galaxy, civilizationId, state.ReservedPopulationMillions, state.ReservedPopulationSpeciesId, state.ReservedPopulationSourceColonyId, out var colony, out _);
            if (colony is not null) colony.PopulationMillions += state.ReservedPopulationMillions;
            economy.Credits += assessment.RefundCredits;
            state.ActiveDesignId = null; state.ActiveOrderId = null; state.ActiveBuildProgress = 0; state.ActiveAuthorizationCredits = 0;
            state.ReservedPopulationMillions = 0; state.ReservedPopulationSpeciesId = null; state.ReservedPopulationSourceColonyId = null;
            PromoteNextBuild(state);
            return new(true, $"Cancelled {design.Name}; refunded {currency.Format(assessment.RefundCredits)}. Consumed materials are not refunded.", assessment.RefundCredits);
        }
        var index = state.QueuedBuilds.FindIndex(order => order.OrderId == orderId);
        var queued = state.QueuedBuilds[index];
        TryResolvePopulationReturn(galaxy, civilizationId, queued.ReservedPopulationMillions, queued.ReservedPopulationSpeciesId, queued.ReservedPopulationSourceColonyId, out var queuedColony, out _);
        if (queuedColony is not null) queuedColony.PopulationMillions += queued.ReservedPopulationMillions;
        state.QueuedBuilds.RemoveAt(index); economy.Credits += assessment.RefundCredits;
        return new(true, $"Cancelled queued {design.Name}; refunded {currency.Format(assessment.RefundCredits)}.", assessment.RefundCredits);
    }

    private static bool TryResolvePopulationReturn(GalaxyState galaxy, int civilizationId, double population, string? speciesId, int? sourceColonyId, out ColonyState? colony, out string? reason)
    {
        colony = null;
        if (population <= 0) { reason = null; return true; }
        colony = sourceColonyId is int id ? galaxy.Colonies.FirstOrDefault(c => c.Id == id) : null;
        if (colony is null || colony.CivilizationId != civilizationId || colony.PopulationSpeciesId != speciesId)
        { reason = "Reserved colonists cannot be returned because their original colony is no longer a valid owned source."; return false; }
        if (!double.IsFinite(colony.PopulationMillions) || colony.PopulationMillions > double.MaxValue - population)
        { reason = "Reserved colonists cannot be returned because their original colony has invalid population accounting."; return false; }
        reason = null; return true;
    }

    private static bool TryPrepareOrderId(ShipyardState state, out string orderId, out string message)
    {
        orderId = string.Empty;
        if (state.NextOrderSequence <= 0 || state.NextOrderSequence == long.MaxValue)
        { message = "This shipyard cannot allocate another stable order identity."; return false; }
        var identities = state.QueuedBuilds.Select(order => order.OrderId).ToList();
        if (state.ActiveDesignId is not null) identities.Add(state.ActiveOrderId ?? string.Empty);
        if (identities.Any(id => !ShipyardState.IsValidPersistedOrderId(id)) ||
            identities.Distinct(StringComparer.Ordinal).Count() != identities.Count)
        { message = "This shipyard has invalid or duplicate vessel order identities."; return false; }
        if (identities.Any(id => ShipyardState.TryReadCanonicalSequence(id, state.CivilizationId, out var sequence) && sequence >= state.NextOrderSequence))
        { message = "This shipyard's order counter does not follow its existing vessel identities."; return false; }
        var candidateOrderId = ShipyardState.FormatOrderId(state.CivilizationId, state.NextOrderSequence);
        if (state.ActiveOrderId == candidateOrderId || state.QueuedBuilds.Any(order => order.OrderId == candidateOrderId))
        { message = "This shipyard's next order identity collides with an existing vessel order."; return false; }
        orderId = candidateOrderId;
        message = string.Empty; return true;
    }

    private static bool CanPromote(ShipBuildOrderState order, out string? reason)
    {
        try { ShipDesignRegistry.Get(order.DesignId); }
        catch (InvalidOperationException) { reason = "The next queued vessel references an unknown design and cannot be promoted."; return false; }
        if (!ShipyardState.IsValidPersistedOrderId(order.OrderId) || !double.IsFinite(order.AuthorizationCredits) || order.AuthorizationCredits < 0 ||
            !double.IsFinite(order.ReservedPopulationMillions) || order.ReservedPopulationMillions < 0)
        { reason = "The next queued vessel has invalid accounting and cannot be promoted."; return false; }
        reason = null; return true;
    }

    private static void PromoteNextBuild(ShipyardState state)
    {
        if (state.QueuedBuilds.Count == 0)
            return;

        var next = state.QueuedBuilds[0];
        state.QueuedBuilds.RemoveAt(0);
        state.ActiveDesignId = next.DesignId;
        state.ActiveOrderId = next.OrderId;
        state.ActiveBuildProgress = 0.0;
        state.ActiveAuthorizationCredits = next.AuthorizationCredits;
        state.ReservedPopulationMillions = next.ReservedPopulationMillions;
        state.ReservedPopulationSpeciesId = next.ReservedPopulationSpeciesId;
        state.ReservedPopulationSourceColonyId = next.ReservedPopulationSourceColonyId;
    }

    private ShipDesignDefinition? SelectAiDesign(
        GalaxyState galaxy,
        CivilizationState civilization,
        ShipbuildingStrategicPreference preference)
    {
        var available = GetAvailableDesigns(galaxy, civilization.Id);
        if (available.Count == 0)
            return null;

        var activeFleets = galaxy.Fleets
            .Where(fleet => fleet.IsActive && fleet.CivilizationId == civilization.Id)
            .ToArray();

        if (preference.PreferredNewFleetRole is { } preferredRole &&
            NeedsNewRole(civilization, activeFleets, preferredRole, preference.DeferNewColonization))
        {
            var preferred = available.FirstOrDefault(design => design.Role == preferredRole);
            if (preferred is not null)
                return preferred;
        }

        // Preserve the existing early-release fallback when strategy has no actionable preference.
        if (!activeFleets.Any(fleet => fleet.Role == FleetRole.Scout))
            return available.FirstOrDefault(design => design.Role == FleetRole.Scout);
        if (civilization.Traits.ScientificCuriosity >= 0.60 &&
            !activeFleets.Any(fleet => fleet.Role == FleetRole.Science))
        {
            return available.FirstOrDefault(design => design.Role == FleetRole.Science);
        }

        if (!preference.DeferNewColonization &&
            civilization.ExpansionAllowed &&
            !activeFleets.Any(fleet => fleet.Role == FleetRole.Colony && fleet.EmbarkedPopulationMillions > 0.0))
        {
            return available.FirstOrDefault(design => design.Role == FleetRole.Colony);
        }

        return null;
    }

    private static bool NeedsNewRole(
        CivilizationState civilization,
        IReadOnlyCollection<FleetState> activeFleets,
        FleetRole role,
        bool deferNewColonization) => role switch
    {
        FleetRole.Scout => !activeFleets.Any(fleet => fleet.Role == FleetRole.Scout),
        FleetRole.Science => !activeFleets.Any(fleet => fleet.Role == FleetRole.Science),
        FleetRole.Logistics => !activeFleets.Any(fleet => fleet.Role == FleetRole.Logistics),
        FleetRole.Military => !activeFleets.Any(fleet => fleet.Role == FleetRole.Military),
        FleetRole.Colony => !deferNewColonization &&
                            civilization.ExpansionAllowed &&
                            !activeFleets.Any(fleet =>
                                fleet.Role == FleetRole.Colony &&
                                fleet.EmbarkedPopulationMillions > 0.0),
        _ => false,
    };

    private FleetState CreateFleet(
        GalaxyState galaxy,
        CivilizationState civilization,
        ShipDesignDefinition definition,
        double embarkedPopulationMillions,
        string? embarkedPopulationSpeciesId)
    {
        var propulsion = GetEffectivePropulsion(galaxy, civilization.Id, definition);
        var home = galaxy.Systems.First(system => system.Id == civilization.HomeSystemId);
        var nextId = galaxy.Fleets.Count == 0 ? 0 : galaxy.Fleets.Max(fleet => fleet.Id) + 1;
        var roleCount = galaxy.Fleets.Count(f => f.CivilizationId == civilization.Id && f.Role == definition.Role) + 1;
        var name = definition.Role switch
        {
            FleetRole.Scout => civilization.IsPlayer ? $"Pathfinder {roleCount}" : $"{civilization.Name} Scout {roleCount}",
            FleetRole.Science => civilization.IsPlayer ? $"Discovery {roleCount}" : $"{civilization.Name} Science {roleCount}",
            FleetRole.Colony when definition.Id == ShipDesignRegistry.ResourceOutpostShipId => civilization.IsPlayer ? $"Prospector {roleCount}" : $"{civilization.Name} Prospector {roleCount}",
            FleetRole.Colony => civilization.IsPlayer ? $"Pioneer {roleCount}" : $"{civilization.Name} Pioneer {roleCount}",
            FleetRole.Military => civilization.IsPlayer ? $"Sentinel {roleCount}" : $"{civilization.Name} Patrol {roleCount}",
            FleetRole.Logistics => civilization.IsPlayer ? $"Lifeline {roleCount}" : $"{civilization.Name} Freighter {roleCount}",
            _ => $"{civilization.Name} Vessel {roleCount}",
        };

        var isPopulatedColonyShip = definition.Role == FleetRole.Colony && embarkedPopulationMillions > 0.0;
        if (isPopulatedColonyShip &&
            (string.IsNullOrWhiteSpace(embarkedPopulationSpeciesId) || !SpeciesCatalog.TryGet(embarkedPopulationSpeciesId, out _)))
        {
            throw new InvalidOperationException("A populated colony ship must carry a known species identity.");
        }

        return new FleetState
        {
            Id = nextId,
            CivilizationId = civilization.Id,
            Name = name,
            Role = definition.Role,
            DesignId = definition.Id,
            Position = home.Position,
            CurrentSystemId = home.Id,
            StrategicSpeed = propulsion.StrategicSpeed,
            MaximumLegRangeLightYears = propulsion.MaximumLegRangeLightYears,
            FuelCapacityLightYears = propulsion.FuelEnduranceLightYears,
            FuelRemainingLightYears = propulsion.FuelEnduranceLightYears,
            SensorRange = definition.SensorRange,
            CargoMaterialCapacity = definition.CargoMaterialCapacity,
            IsActive = true,
            EmbarkedPopulationMillions = isPopulatedColonyShip
                ? Math.Max(0.0, embarkedPopulationMillions)
                : 0.0,
            EmbarkedPopulationSpeciesId = isPopulatedColonyShip
                ? embarkedPopulationSpeciesId
                : null,
            Combat = CombatProfileRegistry.CreateInitialState(definition.CombatProfileId, definition.Role),
        };
    }
}

public sealed record ShipbuildingEvent(int CivilizationId, int FleetId, string DesignId, string Message);
public sealed record ShipbuildingOrderResult(bool Accepted, string Message);
public sealed record ShipbuildingCancellationAssessment(
    bool CanCancel,
    bool IsActive,
    string? DesignId,
    double RefundCredits,
    string? Blocker);
public sealed record ShipbuildingCancellationResult(bool Accepted, string Message, double RefundedCredits);
public sealed record ShipPropulsionPerformance(
    double StrategicSpeed,
    double MaximumLegRangeLightYears,
    double FuelEnduranceLightYears,
    string PropulsionGeneration);
