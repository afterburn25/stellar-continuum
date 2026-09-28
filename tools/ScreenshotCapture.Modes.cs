using System;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Game.Presentation;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task<string> ReloadDeveloperThroughPlayerAsync(string playerPath, string playerHash)
    {
        Require(HashFile(playerPath) == playerHash, "Developer-only work modified the Player save.");
        var expectedPlayer = SemanticSave(playerPath, developer: false);
        var developerPath = ProjectSettings.GlobalizePath("user://saves/developer-autosave.json");
        var toolsUsed = _main.UiDeveloperToolsUsed;
        await OpenCampaignMenuAsync();
        await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "ModePlayer");
        await WaitForCampaignLoadingAsync();
        Require(!_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed && !_main.UiIsDeveloperToolsOpen &&
            !_main.UiIsMenuOpen && _main.UiModeLabel == "Player mode" &&
            JsonNode.DeepEquals(expectedPlayer, SemanticSave(playerPath, developer: false)),
            "Switching to Player loaded Developer state or lost the exact saved Player world.");
        var expectedDeveloper = SemanticSave(developerPath, developer: true);
        await OpenCampaignMenuAsync();
        await WaitForRefreshAsync();
        Require(Descendants(_main.GetNode("MainMenuLayer")).OfType<Button>()
            .Single(button => button.Name == "DeveloperTools").Disabled,
            "Player mode exposed enabled Developer tools.");
        await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "OpenDevelopment");
        await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "ModeDeveloper");
        await WaitForCampaignLoadingAsync();
        Require(_main.UiIsDeveloperMode && !_main.UiIsMenuOpen,
            "Switching back to Developer did not enter the playable Developer campaign.");
        Require(_main.UiDeveloperToolsUsed == toolsUsed,
            $"Switching back to Developer changed ToolsUsed provenance from {toolsUsed} to {_main.UiDeveloperToolsUsed}.");
        Require(JsonNode.DeepEquals(expectedDeveloper, SemanticSave(developerPath, developer: true)),
            "Switching back to Developer changed the exact saved world or surface state.");
        // Visiting Player legitimately creates its own new checkpoint; subsequent Developer
        // operations must leave these new bytes untouched just as they left the original slot.
        return HashFile(playerPath);
    }

    private async Task VerifyDeveloperToolsAsync(string playerPath, string playerHash)
    {
        Require(_main.UiIsDeveloperMode && !_main.UiDeveloperToolsUsed,
            "Ordinary camera, research, construction, and surface acceptance used a Developer grant.");
        if (_main.UiIsSurfaceOpen)
            await ClickNamedButtonAsync(_main.GetNode("PlanetSurfaceLayer/PlanetSurfaceView"), "SurfaceBack");
        await ClickButtonAsync(_dock, "Back to Region");
        await WaitForCameraAsync();
        await OpenCampaignMenuAsync();
        await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "OpenDevelopment");
        await ClickNamedButtonAsync(_main.GetNode("MainMenuLayer"), "DeveloperTools");
        var tools = _main.GetNode<DeveloperToolsLayer>("DeveloperToolsLayer");
        Check(_main.UiIsDeveloperToolsOpen && _main.UiIsPaused && !_main.UiDeveloperToolsUsed,
            "developer-tools-open-without-automatic-command");
        var camera = ObserveCamera();
        var revision = _main.UiPointerCommandRevision;
        var dashboard = _main.UiDashboard;
        await ClickPositionAsync(new Vector2(1250, 350), MouseButton.Left);
        await ClickPositionAsync(new Vector2(1250, 350), MouseButton.Right, ctrl: true);
        await ClickPositionAsync(new Vector2(1250, 350), MouseButton.WheelUp);
        // Space/Enter activate the focused Close button; these keys instead probe
        // gameplay commands that have no legitimate action in this tools modal.
        foreach (var key in new[] { Key.N, Key.R, Key.C, Key.B, Key.F6 }) await PressKeyAsync(key);
        Check(_main.UiIsDeveloperToolsOpen && _main.UiIsPaused && SameCamera(camera, ObserveCamera()) &&
            _main.UiPointerCommandRevision == revision && Equals(dashboard, _main.UiDashboard),
            "developer-tools-block-gameplay-input");
        foreach (var button in Descendants(tools).OfType<Button>().Where(button => button.IsVisibleInTree()))
        {
            await RevealControlAsync(button);
            AssertInsideViewport(button, "Developer tools " + button.Name);
        }
        Require(_main.UiDeveloperCommands.Count == 6 &&
            _main.UiDeveloperCommands.Any(command => command.Id == "unlock_research") &&
            _main.UiDeveloperCommands.All(command =>
            Descendants(tools).OfType<Button>().Any(button => button.Name == "DeveloperCommand_" + command.Id)),
            "Developer commands are missing their actual selectable controls.");
        Check(true, "developer-tools-controls-reachable-1280x720");
        await ClickNamedButtonAsync(tools, "DeveloperCommand_grant_resources");
        await WaitForRefreshAsync();
        var after = _main.UiDashboard;
        var result = Descendants(tools).OfType<Label>().Single(label => label.Name == "DeveloperCommandResult");
        var provenance = Descendants(tools).OfType<Label>().Single(label => label.Name == "DeveloperProvenance");
        Check(_main.UiDeveloperToolsUsed && _main.UiIsPaused && after.Credits == dashboard.Credits + 1000 &&
            after.Industry == dashboard.Industry + 1000 && after.Science == dashboard.Science &&
            !string.IsNullOrWhiteSpace(result.Text) && provenance.Text.Contains("TOOLS USED", StringComparison.Ordinal) &&
            HashFile(playerPath) == playerHash, "explicit-developer-grant-is-marked-and-isolated");
        await SaveViewportAsync("19-developer-tools.png");
        await ClickNamedButtonAsync(tools, "DeveloperCommand_unlock_technology");
        await WaitForRefreshAsync();
        await ClickNamedButtonAsync(tools, "DeveloperSave");
        Require(HashFile(playerPath) == playerHash, "Developer tool save modified Player data.");
        await ClickNamedButtonAsync(tools, "DeveloperToolsClose");
        Require(!_main.UiIsDeveloperToolsOpen, "Developer tools could not be closed through their own button.");
        await OpenSectionAsync("ships");
        await WaitForRefreshAsync();
        var shipArtwork = Descendants(ActivePanel()).OfType<TextureRect>()
            .Where(texture => texture.Name.ToString().StartsWith("Artwork_", StringComparison.Ordinal))
            .ToArray();
        Check(shipArtwork.Length == _main.UiShipChoices.Count && shipArtwork.Length >= 6 &&
            shipArtwork.All(texture => texture.Texture is { } artwork &&
            artwork.GetWidth() >= 1200 && artwork.GetHeight() >= 1200),
            "shipyard-design-artwork-loaded");
        await SaveViewportAsync("22-shipyard-artwork.png");
        var fleetCountBeforeBuild = _main.UiDashboard.FleetCount;
        await ClickNamedButtonAsync(ActivePanel(), "Choosewarp_scout");
        var shipbuilding = _main.UiDashboard;
        Check((shipbuilding.Shipyard.IsActive && shipbuilding.Shipyard.Title == "Pathfinder Scout") ||
            shipbuilding.FleetCount == fleetCountBeforeBuild + 1,
            "named-ship-design-starts-build");
        _ = await ReloadDeveloperThroughPlayerAsync(playerPath, playerHash);
        Check(_main.UiDeveloperToolsUsed, "developer-tool-provenance-survives-mode-roundtrip");
    }

    private async Task OpenCampaignMenuAsync()
    {
        if (_main.UiIsMenuOpen) return;
        // Mode switches preserve the player's open drawer. Clicking its rail toggle
        // again would close it instead of revealing the existing Campaign button.
        if (!_sidebar.IsDrawerOpen || _sidebar.ActiveSection != "menu")
            await OpenSectionAsync("menu");
        Require(_drawer.IsVisibleInTree() && ActivePanel().Name == "Menu",
            "The open menu drawer does not expose its actual Campaign control.");
        await ClickNamedButtonAsync(ActivePanel(), "CampaignMenu");
        Require(_main.UiIsMenuOpen, "Campaign menu did not open through its visible button.");
    }

    private async Task ReplaceSeedThroughKeyboardAsync(LineEdit field, string text)
    {
        await ClickPositionAsync(ScreenRect(field).GetCenter(), MouseButton.Left);
        Input.ParseInputEvent(new InputEventKey { Keycode = Key.A, PhysicalKeycode = Key.A, CtrlPressed = true, Pressed = true });
        await WaitFramesAsync(1);
        Input.ParseInputEvent(new InputEventKey { Keycode = Key.A, PhysicalKeycode = Key.A, CtrlPressed = true, Pressed = false });
        await PressKeyAsync(Key.Backspace);
        foreach (var character in text)
        {
            var key = (Key)character;
            Input.ParseInputEvent(new InputEventKey { Keycode = key, PhysicalKeycode = key, Unicode = character, Pressed = true });
            await WaitFramesAsync(1);
            Input.ParseInputEvent(new InputEventKey { Keycode = key, PhysicalKeycode = key, Unicode = character, Pressed = false });
        }
        await WaitFramesAsync(2);
        Require(field.Text == text && _main.UiIsMenuOpen, "Real keyboard editing did not restore the Developer seed.");
    }

    private async Task WaitForCampaignLoadingAsync()
    {
        var menu = _main.GetNode<MainMenuLayer>("MainMenuLayer");
        var elapsed = System.Diagnostics.Stopwatch.StartNew();
        while (elapsed.Elapsed < TimeSpan.FromSeconds(20) && menu.IsLoadingCampaign)
            await ToSignal(GetTree().CreateTimer(.05), SceneTreeTimer.SignalName.Timeout);
        Require(!menu.IsLoadingCampaign, "Campaign loading did not finish within 20 seconds.");
        await WaitFramesAsync(2);
    }

    private static JsonObject SemanticSave(string path, bool developer)
    {
        var envelope = JsonNode.Parse(File.ReadAllText(path))?.AsObject()
            ?? throw new InvalidOperationException("Saved mode has no JSON object.");
        var campaign = developer ? envelope["Campaign"]?.AsObject() : envelope;
        Require(campaign is not null, "Developer save has no canonical campaign payload.");
        campaign!.Remove("SavedAtUtc");
        return envelope;
    }
}
