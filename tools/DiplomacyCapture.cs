using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading.Tasks;
using Godot;
using Game.Presentation;
using Game.Simulation.Diplomacy;
using Game.Simulation.Models;
using Game.Simulation.Species;

namespace Game.Tools;

/// <summary>Native game acceptance scene. Contact opportunities are fixture inputs; all player
/// actions use real GUI input and the production observer command adapter. Use isolated APPDATA.</summary>
public partial class DiplomacyCapture : Node
{
    private Main _main = null!;
    private RelationsPanel _host = null!;
    private DiplomacyState _state = null!;
    private int _observer, _foreign, _home;
    private readonly List<string> _checks = new();
    private readonly List<object> _captures = new();
    private string _output = "";
    public override async void _Ready()
    {
        try
        {
            _output = System.Environment.GetEnvironmentVariable("STELLAR_SCREENSHOT_DIR") ??
                throw new InvalidOperationException("An isolated screenshot directory must be supplied.");
            Directory.CreateDirectory(_output);
            await Run();
            File.WriteAllText(Path.Combine(_output, "manifest.json"), JsonSerializer.Serialize(new {
                source = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_SHA"), checks = _checks, captures = _captures,
                input = "Godot Viewport.PushInput GUI events; isolated authoritative contact and species-art fixtures",
                seed = "2026091101"
            }, new JsonSerializerOptions { WriteIndented = true }));
            GD.Print("DIPLOMACY_NATIVE_PASS " + _checks.Count);
            _main.UiVoice?.Stop();
            await AudioDirector.ShutdownAndQuitAsync(GetTree(), 0);
        }
        catch (Exception e)
        {
            GD.PrintErr(e.ToString());
            try { await Capture("failure"); } catch { }
            _main?.UiVoice?.Stop();
            await AudioDirector.ShutdownAndQuitAsync(GetTree(), 1);
        }
    }

    private async Task Run()
    {
        var captureWindow = GetWindow();
        var resolutionText = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_RESOLUTION");
        if (resolutionText is not ("1280x720" or "1920x1080"))
            throw new InvalidOperationException("STELLAR_CAPTURE_RESOLUTION must be 1280x720 or 1920x1080.");
        var resolutionParts = resolutionText.Split('x');
        var requestedResolution = new Vector2I(int.Parse(resolutionParts[0]), int.Parse(resolutionParts[1]));
        _main = GD.Load<PackedScene>("res://scenes/Main.tscn").Instantiate<Main>();
        AddChild(_main);
        // Production applies fullscreen preferences during Main initialization. The
        // acceptance scene must restore its explicitly requested native client size.
        captureWindow.Mode = Window.ModeEnum.Windowed;
        captureWindow.Borderless = false;
        await Frames(2);
        captureWindow.Position = new Vector2I(70, 70);
        captureWindow.Size = requestedResolution;
        var menu = _main.GetNode<MainMenuLayer>("MainMenuLayer");
        var startupWaitStarted = Time.GetTicksMsec();
        while (menu.IsLoadingCampaign && Time.GetTicksMsec() - startupWaitStarted < 20_000)
            await Frames(1);
        Require(!menu.IsLoadingCampaign, "production startup loading completes within 20 seconds");
        await Frames(2);
        var initialImage = GetViewport().GetTexture().GetImage();
        Require(captureWindow.Size == requestedResolution && initialImage.GetWidth() == requestedResolution.X && initialImage.GetHeight() == requestedResolution.Y,
            $"native capture resolution requested={requestedResolution.X}x{requestedResolution.Y} " +
            $"window={captureWindow.Size.X}x{captureWindow.Size.Y} image={initialImage.GetWidth()}x{initialImage.GetHeight()}");
        _main.UiCreateNewCampaignConfirmed("2026091101");
        await Click("ResumeCampaign");
        _main.UiSetPaused(true, false);
        var galaxy = Read<GalaxyState>("_galaxy");
        _observer = galaxy.PlayerCivilizationId;
        _foreign = galaxy.Civilizations.First(c => c.Id != _observer).Id;
        _home = galaxy.Civilizations.First(c => c.Id == _observer).HomeSystemId;
        _state = Read<DiplomacyState>("_diplomacyState");
        _host = _main.GetNode<RelationsPanel>("RelationsPanel");
        Require(_main.GetUiDiplomacyView().Contacts.Count == 0, "fresh campaign starts without foreign contacts");
        await Click("NavRelations"); await Capture("01-no-contacts");
        var revision = _main.UiPointerCommandRevision;
        await ClickPoint(_host.Workspace.GetGlobalRect().GetCenter());
        Require(_main.UiPointerCommandRevision == revision, "workspace consumes map input");

        Opportunity(null, .37, ContactAwareness.DetectedUnidentified, false);
        _host.Refresh(true); await Frames(6);
        await Capture("02-unidentified");
        Require(_host.Workspace.Model!.Selected.TargetCivilizationId is null &&
            _host.Workspace.Model.Selected.Trust is null, "unidentified contact has no identity or relationship values");
        Require(!_host.Workspace.FindChild("CivilizationPortrait", true, false)!.Get("visible").AsBool(), "unknown contact has generic signal imagery");
        Opportunity(_foreign, .86, ContactAwareness.ContactEstablished, false);
        new DiplomacySimulation(_state).ProcessContactOpportunity(new(_foreign, "foreign-observer", _observer, 2, _home,
            ContactAwareness.ContactEstablished, ContactCondition.Active, false, .9));
        _host.Refresh(true); await Frames(6);
        await Capture("03-identified");
        await Click("Action_communication");
        Require(_main.GetUiRelationsState(0, 0).HasVisibleCommunication, "communication command opens actual channel");
        new DiplomacySimulation(_state).ApplyRelationshipImpact(_observer, _foreign,
            new RelationshipImpact(.2, 0, .08, .2, .15, 0, "Successful first exchange"), 3);
        _host.Refresh(true); await Capture("04-communication");

        await Click("Action_negotiate"); await Capture("05-negotiation");
        await Click("Term_nonaggression"); await Capture("06-proposal-review");
        await Click("ConfirmDiplomaticAction");
        await Click("TabProposals");
        var outgoing = _main.GetUiDiplomacyView().Proposals.Single(p => p.Status == DiplomaticProposalStatus.Pending);
        await Capture("07-outgoing");
        await Click("Withdraw_" + outgoing.ProposalId);
        Require(_main.GetUiDiplomacyView().Proposals.Single(p => p.ProposalId == outgoing.ProposalId).Status == DiplomaticProposalStatus.Withdrawn,
            "withdrawal uses authoritative command");
        var gateway = new ObserverDiplomacyCommandService(_state);
        var incoming = gateway.SendProposal(_foreign, _observer, DiplomaticProposalKind.Agreement, 5,
            "We propose mutual non-aggression.", DiplomaticAgreementType.NonAggression);
        Require(incoming.Accepted, "incoming proposal fixture accepted by authoritative service");
        _host.Refresh(true); await Capture("08-incoming");
        await Click("Accept_" + incoming.ProposalId!.Value);
        Require(_main.GetUiDiplomacyView().Agreements.Any(a => a.Type == DiplomaticAgreementType.NonAggression && a.Status == DiplomaticAgreementStatus.Active),
            "acceptance creates non-aggression agreement");
        await Click("TabAgreements"); await Capture("09-agreement");

        await Click("Action_war"); await Capture("10-war-confirmation");
        await Click("CancelDiplomaticAction");
        Require(!_main.GetUiDiplomacyView().Relationships.Any(r => r.PoliticalState == DiplomaticPoliticalState.AtWar), "war cancel leaves diplomacy unchanged");
        await Click("Action_war"); await Click("ConfirmDiplomaticAction");
        Require(_main.GetUiDiplomacyView().Relationships.Any(r => r.PoliticalState == DiplomaticPoliticalState.AtWar), "confirmed war executes");
        await Capture("11-at-war");
        await Click("Action_negotiate"); await Click("Term_ceasefire"); await Click("ConfirmDiplomaticAction");
        var ceasefire = _main.GetUiDiplomacyView().Proposals.Single(p => p.Kind == DiplomaticProposalKind.CeasefireOffer && p.Status == DiplomaticProposalStatus.Pending);
        Require(gateway.RespondToProposal(_foreign, ceasefire.ProposalId, true, 7).Accepted, "foreign ceasefire acceptance");
        _host.Refresh(true); await Capture("12-ceasefire");
        await Click("Action_negotiate"); await Click("Term_peace"); await Click("ConfirmDiplomaticAction");
        var peace = _main.GetUiDiplomacyView().Proposals.Single(p => p.Kind == DiplomaticProposalKind.PeaceOffer && p.Status == DiplomaticProposalStatus.Pending);
        Require(gateway.RespondToProposal(_foreign, peace.ProposalId, true, 8).Accepted, "foreign peace acceptance");
        _host.Refresh(true); await Capture("13-peace");
        await Click("TabHistory"); await Capture("14-history");
        await Click("TabIntelligence"); await Capture("15-intelligence");
        await Click("TabOverview"); await Capture("16-overview");
        await Click("Action_negotiate"); await Click("Term_grant"); await Click("ConfirmDiplomaticAction");
        Require(_state.GetAccessPermission(_observer, _foreign) == AccessPermission.Granted &&
            _state.GetAccessPermission(_foreign, _observer) != AccessPermission.Granted, "access direction stays asymmetric");
        await Click("TabAgreements"); await Capture("17-directional-access");
        await Click("Action_negotiate"); await Click("Term_access"); await Click("ConfirmDiplomaticAction");
        Require(_main.GetUiDiplomacyView().Proposals.Any(p => p.Kind == DiplomaticProposalKind.AccessRequest && p.Status == DiplomaticProposalStatus.Pending), "transit request issued");
        var reject = gateway.SendProposal(_foreign, _observer, DiplomaticProposalKind.Agreement, 9, "A further non-aggression proposal.", DiplomaticAgreementType.NonAggression);
        Require(reject.Accepted, "rejection fixture valid");
        _host.Refresh(true); await Click("TabProposals"); await Click("Reject_" + reject.ProposalId!.Value);
        Require(_main.GetUiDiplomacyView().Proposals.Any(p => p.ProposalId == reject.ProposalId && p.Status == DiplomaticProposalStatus.Rejected), "incoming rejection works");
        new DiplomacySimulation(_state).ProcessContactOpportunity(new(_observer, "observed-01", _foreign,
            _state.GetContact(_observer, _foreign)!.LastObservedTick + 1, _home,
            ContactAwareness.ContactEstablished, ContactCondition.StaleOrLost, false, .61));
        _host.Refresh(true); await Capture("18-stale-contact");
        Require(!_main.GetUiRelationsState(0, 0).HasVisibleCommunication, "stale channel unavailable");
        await Click("DiplomacyClose");
        Require(!_main.UiIsDiplomacyOpen, "return to map closes workspace");
        await Capture("19-navigation-icons");

        // Exercise every registered species composition through the real host. These are
        // explicitly isolated artwork fixtures, not claims about the generated civilization.
        var civilizationIndex = galaxy.Civilizations.ToList().FindIndex(c => c.Id == _foreign);
        var originalCivilization = galaxy.Civilizations[civilizationIndex];
        await Click("NavRelations");
        _main.UiVoice?.Stop();
        _host.SetProcess(false); // Hold the long-caption layout fixture independently of audio timing.
        try
        {
            foreach (var species in new[] { SpeciesCatalog.TerranBaselineId, SpeciesCatalog.PelagicHighPressureId,
                SpeciesCatalog.CompactHighGravityId, SpeciesCatalog.CryogenicHydrocarbonId })
            {
                galaxy.Civilizations[civilizationIndex] = originalCivilization with { SpeciesId = species };
                _host.Refresh(true);
                _host.Workspace.SetTransmissionSubtitle("Ambassador",
                    "Our communications array is ready. We welcome your delegation and await the terms of your proposed agreement.", true);
                await Capture("20-art-framing-" + species);
            }
        }
        finally { galaxy.Civilizations[civilizationIndex] = originalCivilization; _host.SetProcess(true); }
    }

    private void Opportunity(int? target, double confidence, ContactAwareness awareness, bool communication) =>
        new DiplomacySimulation(_state).ProcessContactOpportunity(new(_observer, "observed-01", target, 1, _home,
            awareness, ContactCondition.Active, communication, confidence));
    private T Read<T>(string name) => (T)typeof(Main).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(_main)!;
    private async Task Frames(int count = 5) { for (var i = 0; i < count; i++) await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame); }
    private async Task Click(string name)
    {
        var b = Descendants(_main).OfType<Button>().SingleOrDefault(c => c.Name == name && c.IsVisibleInTree()) ??
            throw new InvalidOperationException("Visible button not found: " + name);
        for (Node? parent = b.GetParent(); parent is not null; parent = parent.GetParent())
            if (parent is ScrollContainer scroll) { scroll.EnsureControlVisible(b); await Frames(); }
        var rect = b.GetGlobalRect();
        Require(GetViewport().GetVisibleRect().Encloses(rect), "click target visible " + name);
        Require(!b.Disabled, "click target enabled " + name);
        await ClickPoint(rect.GetCenter());
        await Frames(8);
    }
    private async Task ClickPoint(Vector2 point)
    {
        GetViewport().PushInput(new InputEventMouseMotion { Position = point, GlobalPosition = point }, true);
        GetViewport().PushInput(new InputEventMouseButton { Position = point, GlobalPosition = point, ButtonIndex = MouseButton.Left, ButtonMask = MouseButtonMask.Left, Pressed = true }, true);
        GetViewport().PushInput(new InputEventMouseButton { Position = point, GlobalPosition = point, ButtonIndex = MouseButton.Left, Pressed = false }, true);
        await Frames(4);
    }
    private async Task Capture(string name)
    {
        await Frames(10); await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);
        if (name.StartsWith("20-art-framing-", StringComparison.Ordinal))
        {
            var portrait = (TextureRect)_host.Workspace.FindChild("CivilizationPortrait", true, false)!;
            var caption = (Control)_host.Workspace.FindChild("TransmissionCaption", true, false)!;
            Require(portrait.Texture is not null && portrait.Texture is not AtlasTexture &&
                portrait.StretchMode == TextureRect.StretchModeEnum.KeepAspectCovered,
                "communications scenery fills the viewscreen " + name);
            var textureSize = portrait.Texture!.GetSize();
            Require(textureSize.X >= 2000 && Math.Abs(textureSize.X / textureSize.Y - 3f) < .03f,
                "panoramic production scene loaded " + name);
            // The authored scene keeps the complete representative inside this central region.
            // Test the actual cover crop, not just the TextureRect's container bounds.
            var scale = Math.Max(portrait.Size.X / textureSize.X, portrait.Size.Y / textureSize.Y);
            var visibleSize = portrait.Size / scale / textureSize;
            var visibleImage = new Rect2((Vector2.One - visibleSize) * .5f, visibleSize);
            Require(visibleImage.Grow(.001f).Encloses(new Rect2(.2f, 0, .6f, 1)),
                $"entire representative remains inside the visible scene {name}; stage={portrait.Size}; visible={visibleImage}");
            Require(!portrait.GetGlobalRect().Intersects(caption.GetGlobalRect()), "caption never covers the alien " + name);
            Require(GetViewport().GetVisibleRect().Encloses(portrait.GetGlobalRect()), "whole portrait fits viewport " + name);
        }
        if (name == "04-communication")
        {
            foreach (var bar in Descendants(_host.Workspace).OfType<ProgressBar>().Where(b => b.IsVisibleInTree()))
            {
                var rect = bar.GetGlobalRect();
                Require(GetViewport().GetVisibleRect().Encloses(rect), "meter inside viewport " + bar.Name);
                for (Node? parent = bar.GetParent(); parent is not null; parent = parent.GetParent())
                    if (parent is ScrollContainer scroll)
                        Require(scroll.GetGlobalRect().Encloses(rect), "meter visible without scrolling " + bar.Name);
            }
            foreach (var button in Descendants(_host.Workspace).OfType<Button>().Where(b => b.IsVisibleInTree() && b.Name.ToString().StartsWith("Action_")))
                Require(GetViewport().GetVisibleRect().Encloses(button.GetGlobalRect()), "action inside viewport " + button.Name);
        }
        var image = GetViewport().GetTexture().GetImage();
        var path = Path.Combine(_output, name + ".png");
        if (image.SavePng(path) != Error.Ok) throw new IOException("Screenshot could not be written: " + path);
        _captures.Add(new { name, width = image.GetWidth(), height = image.GetHeight(), sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))) });
        GD.Print("DIPLOMACY_CAPTURE " + name);
    }
    private void Require(bool pass, string check)
    {
        if (!pass) throw new InvalidOperationException(check);
        _checks.Add(check); GD.Print("DIPLOMACY_CHECK " + check);
    }
    private static IEnumerable<Node> Descendants(Node root)
    {
        foreach (var child in root.GetChildren()) { yield return child; foreach (var nested in Descendants(child)) yield return nested; }
    }
}
