using System;

namespace Game.Simulation.Research.Adaptive;

/// <summary>
/// Composes the validated competence-aware research authority with causal Research Pressure,
/// visible-only agenda/AI planning, sparse foreign-technology assessment/exchange semantics,
/// bounded foreign-discovery materialization, and deterministic experimental outcomes.
/// This remains side-by-side with the legacy gameplay research loop.
/// </summary>
public sealed class AdaptiveResearchStrategicRuntime
{
    private AdaptiveResearchStrategicRuntime(
        AdaptiveResearchAuthority authority,
        AdaptiveResearchPressureCatalog pressureCatalog,
        AdaptiveResearchPressureRuntime pressure,
        AdaptiveResearchAgendaCatalog agendaCatalog,
        AdaptiveResearchAgendaRuntime agenda,
        AdaptiveResearchForeignTechnologyCatalog foreignTechnologyCatalog,
        AdaptiveResearchForeignTechnologyRuntime foreignTechnology,
        AdaptiveResearchForeignDiscoveryCatalog foreignDiscoveryCatalog,
        AdaptiveResearchForeignDiscoveryRuntime foreignDiscovery,
        AdaptiveResearchOutcomeCatalog outcomeCatalog,
        AdaptiveResearchOutcomeRuntime outcomes)
    {
        Authority = authority;
        PressureCatalog = pressureCatalog;
        Pressure = pressure;
        AgendaCatalog = agendaCatalog;
        Agenda = agenda;
        ForeignTechnologyCatalog = foreignTechnologyCatalog;
        ForeignTechnology = foreignTechnology;
        ForeignDiscoveryCatalog = foreignDiscoveryCatalog;
        ForeignDiscovery = foreignDiscovery;
        OutcomeCatalog = outcomeCatalog;
        Outcomes = outcomes;
    }

    public AdaptiveResearchAuthority Authority { get; }
    public AdaptiveResearchPressureCatalog PressureCatalog { get; }
    public AdaptiveResearchPressureRuntime Pressure { get; }
    public AdaptiveResearchAgendaCatalog AgendaCatalog { get; }
    public AdaptiveResearchAgendaRuntime Agenda { get; }
    public AdaptiveResearchForeignTechnologyCatalog ForeignTechnologyCatalog { get; }
    public AdaptiveResearchForeignTechnologyRuntime ForeignTechnology { get; }
    public AdaptiveResearchForeignDiscoveryCatalog ForeignDiscoveryCatalog { get; }
    public AdaptiveResearchForeignDiscoveryRuntime ForeignDiscovery { get; }
    public AdaptiveResearchOutcomeCatalog OutcomeCatalog { get; }
    public AdaptiveResearchOutcomeRuntime Outcomes { get; }

    public static AdaptiveResearchStrategicRuntime LoadFromDirectory(string rootPath)
    {
        var authority = AdaptiveResearchAuthority.LoadFromDirectory(rootPath);
        var pressureCatalog = AdaptiveResearchPressureCatalog.LoadFromDirectory(rootPath, authority.Catalog);
        var pressure = new AdaptiveResearchPressureRuntime(authority.Kernel, pressureCatalog);
        var agendaCatalog = AdaptiveResearchAgendaCatalog.LoadFromDirectory(
            rootPath,
            authority.Catalog,
            authority.ExpertiseCatalog);
        var agenda = new AdaptiveResearchAgendaRuntime(authority, agendaCatalog);
        var foreignTechnologyCatalog = AdaptiveResearchForeignTechnologyCatalog.LoadFromDirectory(
            rootPath,
            authority.Catalog,
            authority.ExpertiseCatalog);
        var foreignTechnology = new AdaptiveResearchForeignTechnologyRuntime(authority, foreignTechnologyCatalog);
        var foreignDiscoveryCatalog = AdaptiveResearchForeignDiscoveryCatalog.LoadFromDirectory(rootPath, authority.Catalog);
        var foreignDiscovery = new AdaptiveResearchForeignDiscoveryRuntime(
            authority,
            foreignTechnology,
            foreignDiscoveryCatalog);
        var outcomeCatalog = AdaptiveResearchOutcomeCatalog.LoadFromDirectory(rootPath, authority.Catalog);
        var outcomes = new AdaptiveResearchOutcomeRuntime(authority, outcomeCatalog, pressure);
        return new AdaptiveResearchStrategicRuntime(
            authority,
            pressureCatalog,
            pressure,
            agendaCatalog,
            agenda,
            foreignTechnologyCatalog,
            foreignTechnology,
            foreignDiscoveryCatalog,
            foreignDiscovery,
            outcomeCatalog,
            outcomes);
    }
}
