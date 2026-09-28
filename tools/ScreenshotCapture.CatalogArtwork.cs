using System;
using System.Linq;
using System.Threading.Tasks;
using Game.Presentation;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private async Task VerifyCatalogArtworkAsync()
    {
        await VerifyResearchCardLayoutAsync();
        await OpenSectionAsync("research");
        var workspace = (ResearchWorkspaceView)ActivePanel();
        var detail = _main.UiResearchHorizon.First(node => node.CanPause || node.CanStart);
        await SelectResearchProgramThroughSearchAsync(detail.Id);
        var portrait = Descendants(workspace).OfType<TextureRect>().Single(x => x.Name == "ResearchInspectorArtwork");
        Require(portrait.Texture?.ResourcePath == CatalogArtwork.AssetPath(CatalogArtwork.ResearchArt(detail.Id, true), true),
            "Research inspector did not load its actual artwork.");
        Require(portrait.Texture?.GetWidth() == 512, "Research inspector fell back to a symbol.");
        var locked = Descendants(workspace).OfType<Button>().Where(x => x.Name.ToString().StartsWith("ResearchLocked_", StringComparison.Ordinal)).ToArray();
        Require(locked.All(x => x.Icon?.ResourcePath == CatalogArtwork.AssetPath(CatalogArtwork.Concealed)),
            "Unrevealed nodes leaked a subject image.");
        await SaveViewportAsync("research-artwork-720p.png");
        await CloseDrawerAsync();
        await OpenCampaignMenuAsync();
        var menu = _main.GetNode<MainMenuLayer>("MainMenuLayer");
        await ClickNamedButtonAsync(menu, "OpenDevelopment");
        if (!_main.UiIsDeveloperMode)
        {
            await ClickNamedButtonAsync(menu, "ModeDeveloper");
            await WaitForCampaignLoadingAsync();
            await OpenCampaignMenuAsync();
            await ClickNamedButtonAsync(menu, "OpenDevelopment");
        }
        await ClickNamedButtonAsync(menu, "DeveloperTools");
        var tools = _main.GetNode<DeveloperToolsLayer>("DeveloperToolsLayer");
        await ClickNamedButtonAsync(tools, "DeveloperArtworkLibrary");
        var gallery = Descendants(tools).OfType<CatalogArtworkGallery>().Single();
        await WaitFramesAsync(5);
        for (var page = 0; page < 4; page++)
        {
            Require(Descendants(gallery).OfType<Button>().Count(x => x.Icon?.GetWidth() == 128) == (page == 3 ? 6 : 12),
                "Human station library has missing upgrade images.");
            await SaveViewportAsync("module-artwork-human-page-" + page + ".png");
            if (page < 3) { await ClickButtonAsync(gallery, "Next"); await WaitFramesAsync(3); }
        }
        var selector = Descendants(gallery).OfType<OptionButton>().Single();
        for (var race = 1; race <= 3; race++)
        {
            selector.Select(race); selector.EmitSignal(OptionButton.SignalName.ItemSelected, race);
            await WaitFramesAsync(5);
            Require(Descendants(gallery).OfType<Button>().Count(x => x.Icon?.GetWidth() == 128) == 12,
                "Race module library has missing images.");
            await SaveViewportAsync("module-artwork-race-" + race + ".png");
        }
        await PressKeyAsync(Key.Escape); await WaitFramesAsync(2);
        Require(!Descendants(tools).OfType<CatalogArtworkGallery>().Any() && tools.IsOpen && _main.UiIsPaused,
            "Closing the artwork library did not return to the paused tools window.");
        await ClickNamedButtonAsync(tools, "DeveloperToolsClose");
        Check(true, "catalog-artwork-visible-with-hidden-research-concealed");
    }
}
