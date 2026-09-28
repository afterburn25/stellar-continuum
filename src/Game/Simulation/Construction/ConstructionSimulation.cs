using System;
using System.Collections.Generic;
using System.Linq;
using Game.Simulation.Models;

namespace Game.Simulation.Construction;

public sealed class ConstructionSimulation
{
    public const double IndustryPerDay = 30;
    private readonly IConstructionCapabilityView _capabilityView;

    public ConstructionSimulation(IConstructionCapabilityView? capabilityView = null) =>
        _capabilityView = capabilityView ?? new PrototypeConstructionCapabilityView();

    public IReadOnlyList<ConstructionEvent> Advance(
        GalaxyState galaxy,
        IReadOnlyDictionary<int, double>? industryBudgets = null,
        double simulationDays = 1) =>
        AdvanceCore(galaxy, industryBudgets, null, simulationDays);

    public IReadOnlyList<ConstructionEvent> AdvanceForCivilization(GalaxyState galaxy, int civilizationId,
        double industryBudget, double simulationDays) => AdvanceCore(galaxy,
            new Dictionary<int, double> { [civilizationId] = industryBudget }, civilizationId, simulationDays);

    private IReadOnlyList<ConstructionEvent> AdvanceCore(GalaxyState galaxy,
        IReadOnlyDictionary<int, double>? industryBudgets, int? onlyCivilizationId, double simulationDays)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        if (!double.IsFinite(simulationDays) || simulationDays < 0) throw new ArgumentOutOfRangeException(nameof(simulationDays));
        if (simulationDays == 0) return Array.Empty<ConstructionEvent>();
        if (onlyCivilizationId is null) EnsureAutomaticOrders(galaxy);

        var events = new List<ConstructionEvent>();

        foreach (var civilization in galaxy.Civilizations)
        {
            if (onlyCivilizationId is int selected && civilization.Id != selected) continue;
            if (civilization.IsSeededAncient)
                continue;

            var state = galaxy.ConstructionStates.First(c => c.CivilizationId == civilization.Id);
            var economy = galaxy.Economies.First(e => e.CivilizationId == civilization.Id);

            PromoteQueuedProject(galaxy, civilization.Id, state);

            var availableIndustry = ResolveBudget(industryBudgets, civilization.Id, economy.Industry);
            var surfaceDemand = SurfaceConstruction.GetIndustryDemand(galaxy, civilization.Id, simulationDays);
            var projectDemand = state.ActiveProjectId is null ? 0 :
                Math.Min(IndustryPerDay * simulationDays, Math.Max(0, ConstructionRegistry.Get(state.ActiveProjectId).IndustryCost - state.ActiveProjectProgress));
            var surfaceBudget = surfaceDemand > 0
                ? Math.Min(surfaceDemand, Math.Min(availableIndustry, surfaceDemand + projectDemand) *
                    surfaceDemand / (surfaceDemand + projectDemand))
                : 0;
            SurfaceConstruction.Advance(galaxy, civilization.Id, surfaceBudget, simulationDays);
            availableIndustry = Math.Min(economy.Industry, Math.Max(0, availableIndustry - surfaceBudget));

            if (state.ActiveProjectId is null)
                continue;

            var project = ConstructionRegistry.Get(state.ActiveProjectId);
            var remaining = Math.Max(0.0, project.IndustryCost - state.ActiveProjectProgress);
            var spend = Math.Min(remaining, Math.Min(availableIndustry, IndustryPerDay * simulationDays));
            if (spend <= 0.0 && remaining > 0.0001)
                continue;

            economy.Industry -= spend;
            state.ActiveProjectProgress += spend;

            if (state.ActiveProjectProgress + 0.0001 < project.IndustryCost)
                continue;

            state.CompletedProjectIds.Add(project.Id);
            state.ActiveProjectId = null;
            state.ActiveProjectProgress = 0.0;
            state.ActiveProjectAuthorizationCredits = 0.0;
            events.Add(new ConstructionEvent(civilization.Id, project.Id, $"{civilization.Name} completed {project.Name}."));
            PromoteQueuedProject(galaxy, civilization.Id, state);
        }

        return events;
    }

    /// <summary>Selects missing AI construction orders through the same paid authorization path as players.</summary>
    public void EnsureAutomaticOrders(GalaxyState galaxy)
    {
        ArgumentNullException.ThrowIfNull(galaxy);

        foreach (var civilization in galaxy.Civilizations)
        {
            if (civilization.IsSeededAncient)
                continue;

            var state = galaxy.ConstructionStates.First(c => c.CivilizationId == civilization.Id);
            PromoteQueuedProject(galaxy, civilization.Id, state);
            if (civilization.IsPlayer)
                continue;
            if (state.ActiveProjectId is not null || state.QueuedProjects.Count > 0)
                continue;

            var economy = galaxy.Economies.First(e => e.CivilizationId == civilization.Id);
            var project = GetAvailableProjects(galaxy, civilization.Id)
                .Where(candidate => economy.Credits + 0.0001 >= candidate.CreditCost)
                .OrderByDescending(candidate => Score(candidate, civilization))
                .ThenBy(candidate => candidate.IndustryCost)
                .FirstOrDefault();
            if (project is not null)
                StartProject(galaxy, civilization.Id, project.Id);
        }
    }

    public double GetIndustryDemand(GalaxyState galaxy, int civilizationId, double simulationDays = double.PositiveInfinity)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var state = galaxy.ConstructionStates.First(c => c.CivilizationId == civilizationId);
        if (state.ActiveProjectId is null)
            return SurfaceConstruction.GetIndustryDemand(galaxy, civilizationId, simulationDays);

        var project = ConstructionRegistry.Get(state.ActiveProjectId);
        return Math.Min(Math.Max(0.0, project.IndustryCost - state.ActiveProjectProgress), IndustryPerDay * Math.Max(0, simulationDays)) +
            SurfaceConstruction.GetIndustryDemand(galaxy, civilizationId, simulationDays);
    }

    public ConstructionOrderResult StartProject(GalaxyState galaxy, int civilizationId, string projectId)
    {
        var civilization = galaxy.Civilizations.FirstOrDefault(c => c.Id == civilizationId);
        if (civilization is null)
            return new ConstructionOrderResult(false, "Unknown civilization.");

        var state = galaxy.ConstructionStates.First(c => c.CivilizationId == civilizationId);
        PromoteQueuedProject(galaxy, civilizationId, state);
        if (state.ActiveProjectId is not null)
            return new ConstructionOrderResult(false, "A construction project is already in progress.");
        if (state.QueuedProjects.Count > 0)
            return new ConstructionOrderResult(false, QueueBlockedMessage(galaxy, civilizationId));

        return AuthorizeAndStart(galaxy, civilizationId, projectId, state);
    }

    public ConstructionOrderResult QueueProject(GalaxyState galaxy, int civilizationId, string projectId)
    {
        var civilization = galaxy.Civilizations.FirstOrDefault(c => c.Id == civilizationId);
        if (civilization is null) return new ConstructionOrderResult(false, "Unknown civilization.");
        var state = galaxy.ConstructionStates.First(c => c.CivilizationId == civilizationId);
        PromoteQueuedProject(galaxy, civilizationId, state);
        if (state.ActiveProjectId is null && state.QueuedProjects.Count == 0)
            return AuthorizeAndStart(galaxy, civilizationId, projectId, state);
        if (state.QueuedProjects.Count >= ConstructionState.MaxQueuedProjects)
            return new ConstructionOrderResult(false, $"The construction queue is full ({ConstructionState.MaxQueuedProjects} projects maximum).");
        var project = ValidateOrder(galaxy, civilizationId, projectId, state, out var rejection);
        if (project is null) return new ConstructionOrderResult(false, rejection!);
        if (state.ActiveProjectId == project.Id || state.QueuedProjects.Any(order => order.ProjectId == project.Id))
            return new ConstructionOrderResult(false, $"{project.Name} is already active or queued.");
        var economy = galaxy.Economies.First(e => e.CivilizationId == civilizationId);
        var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, civilizationId);
        if (economy.Credits + 0.0001 < project.CreditCost)
            return new ConstructionOrderResult(false, $"{currency.Format(project.CreditCost)} is required to authorize {project.Name}.");
        economy.Credits -= project.CreditCost;
        state.QueuedProjects.Add(new QueuedConstructionProject(project.Id, project.CreditCost));
        return new ConstructionOrderResult(true, $"Queued {project.Name}. Authorized for {currency.Format(project.CreditCost)}.");
    }

    public ConstructionCancellationResult CancelProject(GalaxyState galaxy, int civilizationId, string projectId)
    {
        var state = galaxy.ConstructionStates.FirstOrDefault(c => c.CivilizationId == civilizationId);
        var economy = galaxy.Economies.FirstOrDefault(e => e.CivilizationId == civilizationId);
        if (state is null || economy is null) return new ConstructionCancellationResult(false, "Unknown civilization.", 0);
        if (state.ActiveProjectId == projectId && ConstructionRegistry.Find(projectId) is { } active)
        {
            var fraction = active.IndustryCost <= 0 ? 0 : Math.Clamp((active.IndustryCost - state.ActiveProjectProgress) / active.IndustryCost, 0, 1);
            var refund = Math.Max(0, state.ActiveProjectAuthorizationCredits) * fraction;
            economy.Credits += refund;
            state.ActiveProjectId = null; state.ActiveProjectProgress = 0; state.ActiveProjectAuthorizationCredits = 0;
            PromoteQueuedProject(galaxy, civilizationId, state);
            var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, civilizationId);
            return new ConstructionCancellationResult(true, $"Cancelled {active.Name}; refunded {currency.Format(refund)}. Consumed materials are not refunded.", refund);
        }
        var index = state.QueuedProjects.FindIndex(order => order.ProjectId == projectId);
        if (index < 0) return new ConstructionCancellationResult(false, "That project is not active or queued.", 0);
        var queued = state.QueuedProjects[index]; state.QueuedProjects.RemoveAt(index); economy.Credits += queued.AuthorizationCredits;
        PromoteQueuedProject(galaxy, civilizationId, state);
        var queuedName = ConstructionRegistry.Get(queued.ProjectId).Name;
        var queuedCurrency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, civilizationId);
        return new ConstructionCancellationResult(true, $"Cancelled queued {queuedName}; refunded {queuedCurrency.Format(queued.AuthorizationCredits)}.", queued.AuthorizationCredits);
    }

    public double GetCancellationRefundPreview(ConstructionState state, string projectId)
    {
        if (state.ActiveProjectId == projectId && ConstructionRegistry.Find(projectId) is { } project)
            return Math.Max(0, state.ActiveProjectAuthorizationCredits) * Math.Clamp((project.IndustryCost - state.ActiveProjectProgress) / project.IndustryCost, 0, 1);
        return state.QueuedProjects.FirstOrDefault(order => order.ProjectId == projectId)?.AuthorizationCredits ?? 0;
    }

    private ConstructionOrderResult AuthorizeAndStart(GalaxyState galaxy, int civilizationId, string projectId, ConstructionState state)
    {
        var project = ValidateOrder(galaxy, civilizationId, projectId, state, out var rejection);
        if (project is null) return new ConstructionOrderResult(false, rejection!);
        var economy = galaxy.Economies.First(e => e.CivilizationId == civilizationId);
        var currency = Game.Simulation.Economy.SovereignCurrencyCatalog.ForCivilization(galaxy, civilizationId);
        if (economy.Credits + 0.0001 < project.CreditCost) return new ConstructionOrderResult(false, $"{currency.Format(project.CreditCost)} is required to authorize {project.Name}.");
        economy.Credits -= project.CreditCost; state.ActiveProjectId = project.Id; state.ActiveProjectProgress = 0; state.ActiveProjectAuthorizationCredits = project.CreditCost;
        return new ConstructionOrderResult(true, $"Construction started: {project.Name}. Authorized for {currency.Format(project.CreditCost)}.");
    }

    private ConstructionProjectDefinition? ValidateOrder(GalaxyState galaxy, int civilizationId, string projectId, ConstructionState state, out string? rejection)
    {
        var project = ConstructionRegistry.Find(projectId);
        if (project is null) { rejection = "Unknown construction project."; return null; }
        if (state.CompletedProjectIds.Contains(project.Id)) { rejection = $"{project.Name} is already complete."; return null; }
        if (GetLockReason(galaxy, civilizationId, project) is { } reason) { rejection = $"{project.Name} is locked: {reason}."; return null; }
        rejection = null; return project;
    }

    private void PromoteQueuedProject(GalaxyState galaxy, int civilizationId, ConstructionState state)
    {
        if (state.ActiveProjectId is not null || state.QueuedProjects.Count == 0) return;
        var order = state.QueuedProjects[0];
        var project = ConstructionRegistry.Find(order.ProjectId);
        if (project is null || state.CompletedProjectIds.Contains(order.ProjectId) || GetLockReason(galaxy, civilizationId, project) is not null) return;
        state.QueuedProjects.RemoveAt(0); state.ActiveProjectId = order.ProjectId; state.ActiveProjectProgress = 0; state.ActiveProjectAuthorizationCredits = order.AuthorizationCredits;
    }

    public string? GetQueueBlockerReason(GalaxyState galaxy, int civilizationId)
    {
        var state = galaxy.ConstructionStates.First(c => c.CivilizationId == civilizationId);
        if (state.ActiveProjectId is not null || state.QueuedProjects.Count == 0) return null;
        var project = ConstructionRegistry.Find(state.QueuedProjects[0].ProjectId);
        return project is null ? "Queued project is unavailable." : GetLockReason(galaxy, civilizationId, project);
    }

    private string QueueBlockedMessage(GalaxyState galaxy, int civilizationId) =>
        GetQueueBlockerReason(galaxy, civilizationId) is { } reason
            ? $"The queued construction head is blocked: {reason}. Cancel it or restore its requirements first."
            : "A queued construction project is waiting to start.";

    private static double ResolveBudget(
        IReadOnlyDictionary<int, double>? industryBudgets,
        int civilizationId,
        double availableIndustry)
    {
        if (!double.IsFinite(availableIndustry))
            throw new ArgumentOutOfRangeException(nameof(availableIndustry), "Available Industry must be finite.");
        availableIndustry = Math.Max(0, availableIndustry);
        if (industryBudgets is null)
            return availableIndustry;
        if (!industryBudgets.TryGetValue(civilizationId, out var budget))
            return 0.0;
        if (!double.IsFinite(budget))
            throw new ArgumentOutOfRangeException(nameof(industryBudgets), "Industry budgets must be finite.");

        return Math.Min(availableIndustry, Math.Max(0.0, budget));
    }

    public IReadOnlyList<ConstructionProjectDefinition> GetAvailableProjects(GalaxyState galaxy, int civilizationId)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == civilizationId);
        return ConstructionRegistry.All
            .Where(project => !construction.CompletedProjectIds.Contains(project.Id) &&
                GetLockReason(galaxy, civilizationId, project) is null)
            .ToArray();
    }

    public string? GetLockReason(GalaxyState galaxy, int civilizationId, ConstructionProjectDefinition project)
    {
        ArgumentNullException.ThrowIfNull(galaxy);
        var construction = galaxy.ConstructionStates.First(state => state.CivilizationId == civilizationId);
        var missingCapabilities = project.RequiredTechnologies
            .Where(id => !_capabilityView.HasCivilizationCapability(galaxy, civilizationId, id))
            .Select(id => Research.TechnologyRegistry.Get(id).Name);
        var missingProjects = (project.RequiredProjects ?? Array.Empty<string>())
            .Where(id => !construction.CompletedProjectIds.Contains(id))
            .Select(id => ConstructionRegistry.Get(id).Name);
        var missing = missingCapabilities.Concat(missingProjects).ToArray();
        return missing.Length == 0 ? null : "requires " + string.Join(" and ", missing);
    }

    private ConstructionProjectDefinition? SelectAiProject(
        GalaxyState galaxy,
        CivilizationState civilization,
        ConstructionState state)
    {
        return GetAvailableProjects(galaxy, civilization.Id)
            .OrderByDescending(project => Score(project, civilization))
            .ThenBy(project => project.IndustryCost)
            .FirstOrDefault();
    }

    private static double Score(ConstructionProjectDefinition project, CivilizationState civilization)
    {
        var traits = civilization.Traits;
        return project.Category switch
        {
            ConstructionCategory.Science => 1.0 + traits.ScientificCuriosity * 0.90,
            ConstructionCategory.Industry => 1.0 + traits.Greed * 0.55 + traits.Territoriality * 0.20,
            ConstructionCategory.Orbital => 1.15 + traits.Territoriality * 0.30 + traits.ScientificCuriosity * 0.20,
            ConstructionCategory.Ftl => 1.30 + traits.ScientificCuriosity * 0.40 + traits.Aggression * 0.20,
            _ => 1.0,
        };
    }
}

public sealed record ConstructionEvent(int CivilizationId, string ProjectId, string Message);
public sealed record ConstructionOrderResult(bool Accepted, string Message);
public sealed record ConstructionCancellationResult(bool Accepted, string Message, double RefundedCredits);
