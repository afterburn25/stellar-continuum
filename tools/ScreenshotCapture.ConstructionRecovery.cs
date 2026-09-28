using System;
using System.Linq;
using System.IO;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Game.Presentation;
using Game.Simulation.Models;
using Game.Simulation;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyFreshConstructionRecoveryAsync(MainMenuLayer menu, ConfirmationDialog dialog)
    {
        await OpenCampaignMenuAsync();
        await ClickNamedButtonAsync(menu, "NewPlayerCampaign");
        await ClickNamedButtonAsync(menu, "SandboxCampaignOption");
        await ClickNamedButtonAsync(menu, "StartConfiguredSandbox");
        await ClickControlAsync(dialog.GetOkButton());
        await WaitForCampaignLoadingAsync();
        Require(!_main.UiIsDeveloperMode && !_main.UiIsMenuOpen,
            "Recovery journey must start an ordinary Player Sandbox through its confirmation.");
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await OpenSectionAsync("industry");
        await ClickNamedButtonAsync(ActivePanel(), "Chooseresearch_network");
        await WaitForRefreshAsync();
        await VerifyConstructionRecoveryAsync();
        await VerifyIndustryPriorityPersistenceAsync();
    }

    private async Task VerifyIndustryPriorityPersistenceAsync()
    {
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await OpenSectionAsync("economy");
        var panel = ActivePanel();
        var controls = Descendants(panel).OfType<Control>().Single(control => control.Name == "IndustryPriorityControls");
        var status = Descendants(panel).OfType<Label>().Single(label => label.Name == "IndustryPriorityStatus");
        foreach (var priority in new[]
                 {
                     IndustryPriority.Balanced,
                     IndustryPriority.InfrastructureFirst,
                     IndustryPriority.ShipbuildingFirst,
                 })
        {
            var button = controls.GetNode<Button>("IndustryPriority_" + priority);
            await ClickControlAsync(button);
            await WaitForRefreshAsync();
            Require(_main.UiIndustryPriority.Priority == priority,
                $"Industry priority command did not select {priority}.");
            Require(button.ButtonPressed && status.Text.Contains(_main.UiIndustryPriority.DisplayName, StringComparison.Ordinal),
                $"Industry priority presentation did not reflect {priority}.");
            Check(true, "industry-priority-pointer-" + priority);
        }

        var selected = _main.UiIndustryPriority.Priority;
        await SaveViewportAsync("production-economy-priority.png");
        await OpenSectionAsync("menu");
        await ClickButtonAsync(ActivePanel(), "Save");
        var path = ProjectSettings.GlobalizePath("user://saves/autosave.json");
        Require(File.Exists(path), "Industry priority save was not written through the Player menu.");
        var saved = FindConstructionGalaxy(JsonNode.Parse(File.ReadAllText(path)))
            ?? throw new InvalidOperationException("Saved campaign has no galaxy payload.");
        var player = saved["PlayerCivilizationId"]!.GetValue<int>();
        var economy = saved["Economies"]!.AsArray()
            .Single(item => item!["CivilizationId"]!.GetValue<int>() == player)!;
        Check(economy["IndustryPriority"]?.GetValue<int>() == (int)selected,
            "industry-priority-save-persisted");

        var applicationRevision = _main.UiCampaignApplicationRevision;
        await OpenSectionAsync("economy");
        await ClickNamedButtonAsync(ActivePanel(), "IndustryPriority_" + IndustryPriority.Balanced);
        await WaitForRefreshAsync();
        Require(_main.UiIndustryPriority.Priority == IndustryPriority.Balanced && selected != IndustryPriority.Balanced,
            "Industry priority fixture did not create a visible unsaved change before Load.");
        await OpenCampaignMenuAsync();
        var menu = _main.GetNode<MainMenuLayer>("MainMenuLayer");
        var confirmation = FindNode<ConfirmationDialog>(menu)
            ?? throw new InvalidOperationException("Saved-campaign Load confirmation is unavailable.");
        await LoadCurrentCampaignThroughMenuAsync(menu, confirmation);
        Require(_main.UiCampaignApplicationRevision > applicationRevision && _main.UiIndustryPriority.Priority == selected,
            "Player Load did not replace the campaign and restore its saved industry priority.");
        await OpenSectionAsync("economy");
        var restoredStatus = Descendants(ActivePanel()).OfType<Label>().Single(label => label.Name == "IndustryPriorityStatus");
        Check(restoredStatus.Text.Contains(_main.UiIndustryPriority.DisplayName, StringComparison.Ordinal),
            "industry-priority-load-reflected-in-economy-panel");
        await VerifyShipCancellationAsync();
    }

    private async Task VerifyShipCancellationAsync()
    {
        // This is a separate, explicitly Developer-labelled fixture. It runs after all
        // ordinary Player surface/fleet checks and never counts as Player progression.
        await OpenCampaignMenuAsync();
        var menu = _main.GetNode<MainMenuLayer>("MainMenuLayer");
        await ClickNamedButtonAsync(menu, "OpenDevelopment");
        await ClickNamedButtonAsync(menu, "NewDeveloperCampaign");
        var confirmation = FindNode<ConfirmationDialog>(menu)
            ?? throw new InvalidOperationException("Developer campaign confirmation is unavailable.");
        Require(confirmation.Visible, "Developer ship fixture did not request a fresh confirmed Developer campaign.");
        await ClickControlAsync(confirmation.GetOkButton());
        await WaitForCampaignLoadingAsync();
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await OpenCampaignMenuAsync();
        await ClickNamedButtonAsync(menu, "OpenDevelopment");
        await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "DeveloperTools");
        var tools = _main.GetNode<DeveloperToolsLayer>("DeveloperToolsLayer");
        await ClickNamedButtonAsync(tools, "DeveloperCommand_unlock_technology");
        await ClickNamedButtonAsync(tools, "DeveloperCommand_grant_resources");
        await ClickNamedButtonAsync(_main, "DeveloperToolsClose");
        await OpenSectionAsync("ships");

        var source = _main.UiOwnedColonies.OrderByDescending(colony => colony.PopulationMillions).First();
        await ClickNamedButtonAsync(ActivePanel(), "Choosecolony_ship");
        await WaitForRefreshAsync();
        await ClickNamedButtonAsync(ActivePanel(), "Chooseresource_outpost_ship");
        await WaitForRefreshAsync();
        Require(_main.UiShipyardOrders.Count == 2 &&
                _main.UiShipyardOrders[0].State == "Active" &&
                _main.UiShipyardOrders[1].State == "Queued",
            "Developer ship cancellation fixture did not create active and queued orders.");
        var activeProof = _main.UiShipyardOrders.Single(order => order.State == "Active");
        var activeProofCancel = Descendants(ActivePanel()).OfType<Button>()
            .Single(button => button.Name == "CancelShipBuild_" + activeProof.OrderId);
        var previousVoiceSettings = await BeginCaptionLayoutProbeAsync("production-caption-layout-720p");
        await RevealControlAsync(activeProofCancel);
        AssertCaptionDoesNotCover(activeProofCancel, "shipyard-caption-safe-area-preserves-cost-actions-720p");
        await SaveViewportAsync("production-ship-queue-720p.png");
        _main.UiVoice?.Stop(); _main.UiVoice?.ApplySettings(previousVoiceSettings);

        // Cancel a genuinely queued order before it can ever be promoted. Record every
        // conserved quantity at the actual cancellation boundary, not before clock motion.
        var queued = _main.UiShipyardOrders.Single(order => order.State == "Queued");
        var queuedPopulationBefore = _main.UiOwnedColonies.Single(colony => colony.ColonyId == source.ColonyId).PopulationMillions;
        var queuedCreditsBefore = _main.UiDashboard.Credits;
        var queuedMaterialsBefore = _main.UiDashboard.Industry;
        var queuedCancel = Descendants(ActivePanel()).OfType<Button>()
            .Single(button => button.Name == "CancelShipBuild_" + queued.OrderId);
        await RevealControlAsync(queuedCancel);
        await ClickControlAsync(queuedCancel);
        await WaitForRefreshAsync();
        Check(_main.UiShipyardOrders is [{ State: "Active" }] &&
              Math.Abs(_main.UiOwnedColonies.Single(colony => colony.ColonyId == source.ColonyId).PopulationMillions -
                  (queuedPopulationBefore + queued.ReservedPopulationMillions)) < .0001 &&
              Math.Abs(_main.UiDashboard.Credits - queuedCreditsBefore - queued.RefundPreview) < .0001 &&
              Math.Abs(_main.UiDashboard.Industry - queuedMaterialsBefore) < .0001,
            "queued-ship-cancel-before-promotion-conserves-population-materials-and-refund");

        // Recreate the queue so the active cancellation below must promote a real persisted order.
        await ClickNamedButtonAsync(ActivePanel(), "Chooseresource_outpost_ship");
        await WaitForRefreshAsync();
        queued = _main.UiShipyardOrders.Single(order => order.State == "Queued");

        // Allow ordinary simulation to consume a small, real amount of the active build.
        _main.UiSetSpeed((int)SimulationClock.SpeedLevel.Normal);
        if (_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        var partialOrderId = _main.UiShipyardOrders.Single(order => order.State == "Active").OrderId;
        var partialDeadline = Time.GetTicksMsec() + 15000;
        while (Time.GetTicksMsec() < partialDeadline)
        {
            var observed = _main.UiShipyardOrders.FirstOrDefault(order => order.OrderId == partialOrderId)
                ?? throw new InvalidOperationException("Ship cancellation fixture lost the active order while waiting for partial progress.");
            if (observed.State != "Active" || observed.Progress >= 1)
                throw new InvalidOperationException("Ship cancellation fixture completed the active order before partial-progress evidence was captured.");
            if (observed.Progress > 0) break;
            await WaitForRefreshAsync();
        }
        if (!_main.UiIsPaused) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        await WaitForRefreshAsync();

        var active = _main.UiShipyardOrders.Single(order => order.State == "Active");
        queued = _main.UiShipyardOrders.Single(order => order.State == "Queued");
        Require(active.Progress > 0 && active.Progress < 1 && active.SourceColonyId == source.ColonyId,
            "Ship cancellation fixture did not preserve a partially progressed active order before its bounded deadline.");

        await OpenSectionAsync("menu");
        await ClickButtonAsync(ActivePanel(), "Save");
        var path = ProjectSettings.GlobalizePath("user://saves/developer-autosave.json");
        Require(File.Exists(path), "Shipyard cancellation save was not written through the Developer menu.");
        var saved = FindConstructionGalaxy(JsonNode.Parse(File.ReadAllText(path)))
            ?? throw new InvalidOperationException("Saved Developer campaign has no galaxy payload.");
        var player = saved["PlayerCivilizationId"]!.GetValue<int>();
        var savedShipyard = saved["ShipyardStates"]!.AsArray()
            .Single(item => item!["CivilizationId"]!.GetValue<int>() == player)!;
        var savedApplicationRevision = _main.UiCampaignApplicationRevision;
        var savedActive = active;
        var savedQueued = queued;
        var savedPopulation = _main.UiOwnedColonies.Single(colony => colony.ColonyId == source.ColonyId).PopulationMillions;
        var savedCredits = _main.UiDashboard.Credits;
        var savedMaterials = _main.UiDashboard.Industry;
        Check(savedShipyard["ActiveOrderId"]?.GetValue<string>() == active.OrderId &&
              savedShipyard["QueuedBuilds"]!.AsArray().Any(item => item!["OrderId"]!.GetValue<string>() == queued.OrderId),
            "shipyard-orders-save-preserves-stable-identities");

        await OpenSectionAsync("ships");
        var unsavedQueuedCancel = Descendants(ActivePanel()).OfType<Button>()
            .Single(button => button.Name == "CancelShipBuild_" + savedQueued.OrderId);
        await RevealControlAsync(unsavedQueuedCancel);
        await ClickControlAsync(unsavedQueuedCancel);
        await WaitForRefreshAsync();
        Check(!_main.UiShipyardOrders.Any(order => order.OrderId == savedQueued.OrderId) &&
              _main.UiShipyardOrders.Single(order => order.OrderId == savedActive.OrderId).State == "Active" &&
              Math.Abs(_main.UiOwnedColonies.Single(colony => colony.ColonyId == source.ColonyId).PopulationMillions -
                  (savedPopulation + savedQueued.ReservedPopulationMillions)) < .0001 &&
              Math.Abs(_main.UiDashboard.Credits - savedCredits - savedQueued.RefundPreview) < .0001 &&
              Math.Abs(_main.UiDashboard.Industry - savedMaterials) < .0001,
            "shipyard-unsaved-queued-cancellation-removes-order-and-refunds-visible-costs");

        await OpenCampaignMenuAsync();
        var loadMenu = _main.GetNode<MainMenuLayer>("MainMenuLayer");
        var loadConfirmation = FindNode<ConfirmationDialog>(loadMenu)
            ?? throw new InvalidOperationException("Developer ship fixture Load confirmation is unavailable.");
        await ClickNamedButtonAsync(loadMenu, "OpenDevelopment");
        await LoadCurrentCampaignThroughMenuAsync(loadMenu, loadConfirmation);
        var restoredActive = _main.UiShipyardOrders.Single(order => order.OrderId == savedActive.OrderId);
        var restoredQueued = _main.UiShipyardOrders.Single(order => order.OrderId == savedQueued.OrderId);
        Check(_main.UiCampaignApplicationRevision > savedApplicationRevision &&
              restoredActive.State == savedActive.State &&
              Math.Abs(restoredActive.Progress - savedActive.Progress) < .000001 &&
              restoredActive.MaterialsRemaining == savedActive.MaterialsRemaining &&
              restoredActive.AuthorizationCredits == savedActive.AuthorizationCredits &&
              restoredQueued.State == savedQueued.State &&
              Math.Abs(restoredQueued.Progress - savedQueued.Progress) < .000001 &&
              restoredQueued.MaterialsRemaining == savedQueued.MaterialsRemaining &&
              restoredQueued.AuthorizationCredits == savedQueued.AuthorizationCredits &&
              Math.Abs(_main.UiOwnedColonies.Single(colony => colony.ColonyId == source.ColonyId).PopulationMillions - savedPopulation) < .0001 &&
              Math.Abs(_main.UiDashboard.Credits - savedCredits) < .0001 &&
              Math.Abs(_main.UiDashboard.Industry - savedMaterials) < .0001 &&
              _main.UiIsPaused,
            "shipyard-load-replaces-campaign-and-restores-saved-orders-and-economy");
        await OpenSectionAsync("ships");
        active = _main.UiShipyardOrders.Single(order => order.State == "Active");
        queued = _main.UiShipyardOrders.Single(order => order.State == "Queued");
        var persistedQueue = savedShipyard["QueuedBuilds"]!.AsArray().Single()?.AsObject()
            ?? throw new InvalidOperationException("Saved shipyard queue entry is missing.");
        Require(active.OrderId == savedShipyard["ActiveOrderId"]!.GetValue<string>() &&
                queued.OrderId == persistedQueue["OrderId"]!.GetValue<string>(),
            "Shipyard save/load lost stable cancellation identities.");
        var activeCancel = Descendants(ActivePanel()).OfType<Button>()
            .Single(button => button.Name == "CancelShipBuild_" + active.OrderId);
        Require(activeCancel.TooltipText.Contains(_main.UiFormatMoney(active.RefundPreview), StringComparison.Ordinal),
            "Active ship cancellation did not disclose its exact paid refund.");
        var activePopulationBefore = _main.UiOwnedColonies.Single(colony => colony.ColonyId == source.ColonyId).PopulationMillions;
        var creditsBeforeActiveCancel = _main.UiDashboard.Credits;
        var materialsBeforeActiveCancel = _main.UiDashboard.Industry;
        await ClickControlAsync(activeCancel);
        await WaitForRefreshAsync();
        var promoted = _main.UiShipyardOrders.Single(order => order.State == "Active");
        var populationAfterActive = _main.UiOwnedColonies.Single(colony => colony.ColonyId == source.ColonyId).PopulationMillions;
        Check(Math.Abs(populationAfterActive - (activePopulationBefore + active.ReservedPopulationMillions)) < .0001 &&
              Math.Abs(_main.UiDashboard.Credits - creditsBeforeActiveCancel - active.RefundPreview) < .0001 &&
              Math.Abs(_main.UiDashboard.Industry - materialsBeforeActiveCancel) < .0001 &&
              promoted.OrderId == queued.OrderId &&
              !_main.UiShipyardOrders.Any(order => order.OrderId == active.OrderId),
            "active-ship-cancel-refunds-paid-remainder-and-promotes-queue");

        var promotedCancel = Descendants(ActivePanel()).OfType<Button>()
            .Single(button => button.Name == "CancelShipBuild_" + promoted.OrderId);
        var promotedPopulationBefore = _main.UiOwnedColonies.Single(colony => colony.ColonyId == source.ColonyId).PopulationMillions;
        var creditsBeforeQueuedCancel = _main.UiDashboard.Credits;
        var materialsBeforePromotedCancel = _main.UiDashboard.Industry;
        await ClickControlAsync(promotedCancel);
        await WaitForRefreshAsync();
        var populationAfterAll = _main.UiOwnedColonies.Single(colony => colony.ColonyId == source.ColonyId).PopulationMillions;
        Check(Math.Abs(populationAfterAll - (promotedPopulationBefore + promoted.ReservedPopulationMillions)) < .0001 &&
              Math.Abs(_main.UiDashboard.Credits - creditsBeforeQueuedCancel - promoted.RefundPreview) < .0001 &&
              Math.Abs(_main.UiDashboard.Industry - materialsBeforePromotedCancel) < .0001 &&
              _main.UiShipyardOrders.Count == 0,
            "promoted-ship-cancel-returns-population-and-paid-authorization");
        Check(!_main.UiShipChoices.Any(choice => choice.Id == active.OrderId || choice.Id == promoted.OrderId),
            "cancelled-ship-order-identities-are-no-longer-actionable");
    }

    // Orders and recovery use the same visible controls as a player. No resources,
    // research, progress or private state are injected to make the journey pass.
    private async Task VerifyConstructionRecoveryAsync(bool captureEvidence = false)
    {
        var resume = !_main.UiIsPaused;
        if (resume) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
        if (!_sidebar.IsDrawerOpen || _sidebar.ActiveSection != "industry") await OpenSectionAsync("industry");
        Require(_main.UiConstructionOrders is [{ Id: "research_network", State: "Active" }],
            "Queue acceptance requires the ordinary opening research-network project.");
        var original = _main.UiConstructionOrders.Single();
        var credits = _main.UiDashboard.Credits;
        var materials = _main.UiDashboard.Industry;
        await ClickNamedButtonAsync(ActivePanel(), "Chooseindustrial_automation");
        await WaitForRefreshAsync();
        var queued = _main.UiConstructionOrders.Single(order => order.Id == "industrial_automation");
        Check(queued.State == "Queued" && queued.Progress == 0 && queued.AuthorizationCredits > 0 &&
            Math.Abs(_main.UiDashboard.Credits - (credits - queued.AuthorizationCredits)) < .0001 &&
            _main.UiDashboard.Industry == materials &&
            _main.UiConstructionOrders.First() == original,
            "construction-queue-debits-once-without-free-work");
        var duplicate = Descendants(ActivePanel()).OfType<Button>().Single(button => button.Name == "Chooseindustrial_automation");
        Check(duplicate.Disabled, "construction-queue-duplicate-disabled");
        Check(_main.UiConstructionOrders.Select(order => order.Id).SequenceEqual(
                new[] { "research_network", "industrial_automation" }),
            "construction-queue-order-visible");
        foreach (var order in _main.UiConstructionOrders)
        {
            var cancel = Descendants(ActivePanel()).OfType<Button>().Single(button => button.Name == "CancelConstruction_" + order.Id);
            await RevealControlAsync(cancel);
            AssertInsideViewport(cancel, "construction cancellation " + order.Id);
            Require(cancel.TooltipText.Contains(_main.UiFormatMoney(order.RefundPreview), StringComparison.Ordinal),
                "Cancellation control must disclose the exact refund before the click.");
        }
        Check(true, "construction-refund-controls-fit-720p");
        if (captureEvidence) await SaveViewportAsync("construction-queue-720p.png");

        await OpenSectionAsync("menu");
        await ClickButtonAsync(ActivePanel(), "Save");
        var path = ProjectSettings.GlobalizePath(_main.UiIsDeveloperMode
            ? "user://saves/developer-autosave.json" : "user://saves/autosave.json");
        var saved = FindConstructionGalaxy(JsonNode.Parse(File.ReadAllText(path)))
            ?? throw new InvalidOperationException("Saved campaign has no construction state.");
        var player = saved["PlayerCivilizationId"]!.GetValue<int>();
        var state = saved["ConstructionStates"]!.AsArray().Single(item => item!["CivilizationId"]!.GetValue<int>() == player)!;
        Check(state["QueuedProjects"]!.AsArray().Select(item => item!["ProjectId"]!.GetValue<string>())
                .SequenceEqual(new[] { "industrial_automation" }) &&
            Math.Abs(state["ActiveProjectAuthorizationCredits"]!.GetValue<double>() - original.AuthorizationCredits) < .0001,
            "construction-queue-saved-through-player-control");

        await OpenSectionAsync("industry");
        var beforeCancel = _main.UiDashboard.Credits;
        await ClickNamedButtonAsync(ActivePanel(), "CancelConstruction_industrial_automation");
        await WaitForRefreshAsync();
        Check(Math.Abs(_main.UiDashboard.Credits - beforeCancel - queued.RefundPreview) < .0001 &&
            _main.UiDashboard.Industry == materials &&
            _main.UiConstructionOrders.Select(order => order.Id).SequenceEqual(new[] { "research_network" }),
            "construction-queued-cancellation-refunds-authorization-only");
        await ClickNamedButtonAsync(ActivePanel(), "Chooseindustrial_automation");
        await WaitForRefreshAsync();
        beforeCancel = _main.UiDashboard.Credits;
        await ClickNamedButtonAsync(ActivePanel(), "CancelConstruction_research_network");
        await WaitForRefreshAsync();
        Check(Math.Abs(_main.UiDashboard.Credits - beforeCancel - original.RefundPreview) < .0001 &&
            _main.UiDashboard.Industry == materials &&
            _main.UiConstructionOrders is [{ Id: "industrial_automation", State: "Active", Progress: 0 }],
            "construction-active-cancellation-promotes-without-material-refund");
        await ClickNamedButtonAsync(ActivePanel(), "CancelConstruction_industrial_automation");
        await WaitForRefreshAsync();
        await ClickNamedButtonAsync(ActivePanel(), "Chooseresearch_network");
        await WaitForRefreshAsync();
        Require(_main.UiConstructionOrders is [{ Id: "research_network", State: "Active", Progress: 0 }],
            "Recovery did not allow the player to authorize the cancelled project again.");
        if (resume) await ClickNamedButtonAsync(_main, "SimulationPlaybackButton");
    }

    private static JsonObject? FindConstructionGalaxy(JsonNode? node)
    {
        if (node is JsonObject obj)
        {
            if (obj.ContainsKey("ConstructionStates") && obj.ContainsKey("PlayerCivilizationId")) return obj;
            foreach (var pair in obj)
                if (FindConstructionGalaxy(pair.Value) is { } found) return found;
        }
        else if (node is JsonArray array)
            foreach (var child in array)
                if (FindConstructionGalaxy(child) is { } found) return found;
        return null;
    }
}
