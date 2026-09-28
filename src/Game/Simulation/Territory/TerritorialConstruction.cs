using System;
using System.Linq;
using Game.Simulation.Models;
using Game.Simulation.Economy;

namespace Game.Simulation.Territory;

/// <summary>Explicit regional orbital works. Capital is debited once; a real logistics vessel must work on site.</summary>
public static class TerritorialConstruction
{
    public static TerritorialOrderResult Preview(GalaxyState galaxy, int owner, int systemId, TerritorialInstallationKind kind)
    {
        if (!Enum.IsDefined(kind) || !galaxy.Civilizations.Any(c => c.Id == owner) ||
            !galaxy.Systems.Any(s => s.Id == systemId) || !galaxy.Knowledge.IsSystemFullySurveyed(owner, systemId))
            return new(false, "Survey this system before planning regional infrastructure.");
        var definition = TerritorialBalance.Definition(kind);
        var currency = SovereignCurrencyCatalog.ForCivilization(galaxy, owner);
        var price = $"{currency.Format(definition.Credits)}, {definition.Industry:0} industry, {definition.Days:0} days; upkeep {currency.Format(definition.Upkeep)}/day.";
        if (!galaxy.Technologies.First(t => t.CivilizationId == owner).CompletedTechnologyIds.Contains("orbital_industry"))
            return new(false, "Requires Orbital Industry. " + price);
        if (galaxy.Colonies.Any(c => c.SystemId == systemId && c.CivilizationId != owner) ||
            galaxy.Territory?.Installations.Any(i => i.SystemId == systemId && i.CivilizationId != owner) == true)
            return new(false, "Foreign infrastructure occupies this site. Resolve territorial access through diplomacy before construction.");
        if (galaxy.Territory?.Installations.Any(i => i.SystemId == systemId && i.CivilizationId == owner && i.Kind == kind) == true)
            return new(false, "This installation is already present or under construction.");
        if (galaxy.Territory?.Installations.Any(i => !i.IsComplete && i.CivilizationId == owner && i.SystemId == systemId) == true)
            return new(false, "Finish or cancel the current regional construction site first.");
        var builder = galaxy.Fleets.FirstOrDefault(f => CanBuild(f, owner, systemId));
        if (builder is null) return new(false, "Move an active logistics ship here and finish its arrival before construction. " + price);
        var score = TerritorialRuntime.Peek(galaxy)?.Read(owner, systemId);
        if (score is not null && (score.Administration < .035 || score.Supply < .05))
            return new(false, "Site is beyond a supportable frontier. Extend a relay and supply-depot chain from a colony first. " + price);
        var economy = galaxy.Economies.First(e => e.CivilizationId == owner);
        if (economy.Credits < definition.Credits || economy.Industry < definition.Industry)
            return new(false, "Insufficient treasury or industry. " + price);
        return new(true, definition.Name + ": " + price + " The logistics ship must remain on site; moving it pauses work.");
    }
    public static TerritorialOrderResult Start(GalaxyState galaxy, int owner, int systemId, TerritorialInstallationKind kind)
    {
        var runtime = TerritorialRuntime.Initialize(galaxy); runtime.Recompute(galaxy);
        var result = Preview(galaxy, owner, systemId, kind);
        if (!result.Accepted) return result;
        var definition = TerritorialBalance.Definition(kind);
        var builder = galaxy.Fleets.First(f => CanBuild(f, owner, systemId));
        var economy = galaxy.Economies.First(e => e.CivilizationId == owner);
        economy.Credits -= definition.Credits; economy.Industry -= definition.Industry;
        var sites = galaxy.Territory!.Installations;
        var site = new TerritorialInstallation { Id = sites.Select(i => i.Id).DefaultIfEmpty(0).Max() + 1,
            CivilizationId = owner, SystemId = systemId, Kind = kind, BuilderFleetId = builder.Id,
            RequiredDays = definition.Days, PaidCredits = definition.Credits, PaidIndustry = definition.Industry };
        sites.Add(site);
        return new(true, result.Message, site.Id);
    }
    public static TerritorialOrderResult Remove(GalaxyState galaxy, int owner, int installationId)
    {
        var site = galaxy.Territory?.Installations.FirstOrDefault(i => i.Id == installationId && i.CivilizationId == owner);
        if (site is null) return new(false, "No owned installation with that identity.");
        var remaining = site.IsComplete ? 0 : Math.Clamp(1 - site.CompletedDays / site.RequiredDays, 0, 1);
        var economy = galaxy.Economies.First(e => e.CivilizationId == owner);
        economy.Credits += site.PaidCredits * remaining * .8;
        economy.Industry += site.PaidIndustry * remaining * .8;
        galaxy.Territory!.Installations.Remove(site);
        TerritorialRuntime.Initialize(galaxy).Recompute(galaxy);
        return new(true, remaining > 0 ? "Construction cancelled; 80% of unspent capital and materials recovered." : "Installation decommissioned. Its influence and support have ended.");
    }
    public static void Advance(GalaxyState galaxy, double days)
    {
        foreach (var site in galaxy.Territory!.Installations.Where(i => !i.IsComplete))
        {
            var builder = galaxy.Fleets.FirstOrDefault(f => f.Id == site.BuilderFleetId && CanBuild(f, site.CivilizationId, site.SystemId));
            // A replacement logistics ship can resume an abandoned site without repaying capital.
            builder ??= galaxy.Fleets.FirstOrDefault(f => CanBuild(f, site.CivilizationId, site.SystemId));
            if (builder is null) continue;
            site.BuilderFleetId = builder.Id;
            site.CompletedDays = Math.Min(site.RequiredDays, site.CompletedDays + days * CivilizationOperatingCapacity.GetFundingFraction(galaxy, site.CivilizationId));
        }
    }
    public static double Upkeep(GalaxyState galaxy, int owner) => galaxy.Territory?.Installations
        .Where(i => i.CivilizationId == owner).Sum(i => TerritorialBalance.Definition(i.Kind).Upkeep * (i.IsComplete ? 1 : .25)) ?? 0;
    public static double Refueling(GalaxyState galaxy, int owner, int system) =>
        galaxy.Territory?.Installations.Any(i => i.IsComplete && i.CivilizationId == owner && i.SystemId == system &&
            i.Kind is TerritorialInstallationKind.SupplyDepot or TerritorialInstallationKind.NavalBase) == true &&
        (TerritorialRuntime.Peek(galaxy)?.Read(owner, system)?.Supply ?? 0) >= .25
            ? CivilizationOperatingCapacity.GetFundingFraction(galaxy, owner) : 0;
    public static bool CanBuild(FleetState fleet, int owner, int system) => fleet.IsActive && fleet.CivilizationId == owner &&
        fleet.Role == FleetRole.Logistics && fleet.CurrentSystemId == system && fleet.DestinationSystemId is null &&
        fleet.TransitPhase == FleetTransitPhase.None && fleet.CargoMaterials == 0 &&
        fleet.FreightHomeColonyId is null && fleet.FreightTargetOutpostId is null;
}
