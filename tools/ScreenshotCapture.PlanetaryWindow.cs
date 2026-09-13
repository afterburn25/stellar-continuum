using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Simulation.Construction;
using Game.Simulation.Models;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyPlanetaryWindowAsync(Node menu)
    {
        Require(OS.GetUserDataDir().Contains("PlanetaryVerification", StringComparison.Ordinal), "Planetary capture requires an isolated verification user directory.");
        await ClickNamedButtonAsync(menu, "ResumeCampaign");
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await ClickButtonAsync(_dock, "Home"); await WaitForCameraAsync();
        await ClickButtonAsync(_dock, "Open System"); await WaitForCameraAsync();
        await ClickPositionAsync(BodyPoint(3), MouseButton.Left, doubleClick: true); await WaitForCameraAsync();
        await ClickNamedButtonAsync(_main, "SpatialSurface"); await WaitForRefreshAsync();
        var panel = _main.GetNode<PlanetaryWindow>("PlanetSurfaceLayer/PlanetaryWindow");
        Check(panel.IsOpen && _main.UiCurrentSurface?.PlanetName == "Earth", "planetary-window-opens-from-planet-navigation");
        Check(!_main.UiPlaceSurfaceBuilding("power_generator", 100, 100, 0).Accepted, "retired-free-placement-ui-rejects-orders");
        Check(Descendants(panel).OfType<Button>().Count(b => b.Name.ToString().StartsWith("PlanetarySlot_")) == 32, "established-homeworld-has-32-visible-slots");
        await ClickNamedButtonAsync(panel, "PlanetarySlot_3");
        await ClickNamedButtonAsync(panel, "PlanetaryBuild_power_generator"); await WaitForRefreshAsync();
        var building = _main.UiCurrentSurface!.Buildings.Single();
        Check(building.SlotIndex == 3 && !building.Complete, "real-slot-and-palette-clicks-reserve-a-construction-slot");
        Check(!_main.UiBuildPlanetarySlot(3, "science_lab").Accepted, "duplicate-slot-order-is-rejected");
        await ClickNamedButtonAsync(panel, "PlanetaryRemove");
        var confirm = Descendants(panel).OfType<ConfirmationDialog>().Single();
        Check(confirm.Visible, "building-removal-has-an-in-game-confirmation");
        await ClickControlAsync(confirm.GetOkButton()); await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface!.Buildings.Count == 0, "confirmed-cancellation-releases-the-slot");

        // Controlled verification fixture only; all subsequent management actions use UI events.
        var galaxy = (GalaxyState)typeof(Main).GetField("_galaxy", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(_main)!;
        var colony = galaxy.Colonies.Single(c => c.Id == _main.UiCurrentSurface.ColonyId);
        var economy = galaxy.Economies.Single(e => e.CivilizationId == galaxy.PlayerCivilizationId);
        economy.Credits = 2000; economy.Industry = 3000; colony.SurfaceHubLevel = 0;
        panel.Close(); panel.Open(); await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface!.BuildingCapacity == 0 && Descendants(panel).OfType<Button>().Where(b => b.Name.ToString().StartsWith("PlanetarySlot_")).All(b => b.Disabled), "unfinished-command-center-locks-all-building-slots");
        await SaveViewportAsync("planetary-command-required-1280x720.png", 1280, 720);
        await ClickNamedButtonAsync(panel, "PlanetaryCommand"); await WaitForRefreshAsync();
        Check(colony.SurfaceHubLevel == 0 && colony.SurfaceHubUpgradeDaysRemaining > 0, "command-center-button-starts-timed-construction");
        SurfaceConstruction.Advance(galaxy, galaxy.PlayerCivilizationId, 0, colony.SurfaceHubUpgradeDaysRemaining);
        await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface!.BuildingCapacity == 16, "completing-command-center-unlocks-first-tier");
        foreach (var (slot, type) in new[] { (0, "power_generator"), (1, "science_lab"), (2, "fabricator"), (3, "controlled_agriculture") })
        {
            await ClickNamedButtonAsync(panel, "PlanetarySlot_" + slot);
            await ClickNamedButtonAsync(panel, "PlanetaryBuild_" + type); await WaitForRefreshAsync();
        }
        SurfaceConstruction.Advance(galaxy, galaxy.PlayerCivilizationId, 2000, 30);
        await WaitForRefreshAsync();
        Check(colony.SurfaceBuildings.All(b => b.IsComplete), "surface-buildings-complete-through-authoritative-construction");
        await ClickNamedButtonAsync(panel, "PlanetarySlot_0"); await ClickNamedButtonAsync(panel, "PlanetaryEnable"); await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface!.PowerDemand > _main.UiCurrentSurface.PowerSupply, "disabling-generator-exposes-real-power-deficit");
        colony.Stability = .45; await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface!.Planet!.CreditFlow.NetCreditsPerDay < 0 && Descendants(panel).OfType<Label>().Any(label => label.Text.StartsWith("−") && label.Text.Contains("UED")), "negative-credit-flow-remains-signed-in-the-planetary-window");
        var tabs = Descendants(panel).OfType<TabContainer>().Single(); tabs.CurrentTab = 0;
        await SaveViewportAsync("planetary-window-1280x720.png", 1280, 720);
        await ResizeResponsiveWindowAsync(new Vector2I(1920, 1080)); await WaitForRefreshAsync();
        await SaveViewportAsync("planetary-window-1920x1080.png", 1920, 1080);
        Check(Descendants(panel).OfType<Label>().Any(l => l.Text.Contains("POWER DEFICIT")), "planetary-alerts-identify-the-power-shortage");
        var beforeSlots = _main.UiCurrentSurface!.Buildings.Select(b => b.SlotIndex).ToArray();
        await ClickNamedButtonAsync(panel, "PlanetarySave"); await WaitForRefreshAsync();
        Check(_main.UiCurrentSurface.Buildings.Select(b => b.SlotIndex).SequenceEqual(beforeSlots), "saving-preserves-planetary-slots");
        await ClickNamedButtonAsync(panel, "PlanetaryBack"); await WaitForRefreshAsync();
        Check(!_main.UiIsSurfaceOpen, "return-to-orbit-closes-planetary-window");
        File.WriteAllText(Path.Combine(_outputDirectory, "planetary-window-verification.json"), JsonSerializer.Serialize(new { checks = _checks, mouseActions = _mouseActions, userDirectory = OS.GetUserDataDir(), captures = _captures }, new JsonSerializerOptions { WriteIndented = true }));
    }
}
