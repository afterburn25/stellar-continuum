using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using Godot;

namespace Game.Tools;

public partial class ScreenshotCapture
{
    private const string StartupFailureCapture = "startup-failure-ui.png";
    private const string StartupFailureEvidence = "startup-failure-ui.json";

    private async Task VerifyStartupFailureUiAsync(string savePath, string saveHash, long saveLength)
    {
        Require(saveLength > 0 && File.Exists(savePath),
            "The startup-failure UI check requires an isolated campaign created by ordinary startup.");
        Require(saveLength > 4096, "The isolated campaign save is unexpectedly small.");

        await WaitFramesAsync(5);
        var viewport = GetViewport().GetVisibleRect();
        Require(viewport.Size == new Vector2(1280, 720),
            "The startup-failure acceptance run must render at 1280x720.");
        var layer = _main.GetNodeOrNull<CanvasLayer>("StartupFailure")
            ?? throw new InvalidOperationException("The actual startup failure did not present its recovery screen.");
        var controls = Descendants(layer).OfType<Control>().Where(control => control.IsVisibleInTree()).ToArray();
        var title = controls.OfType<Label>().Single(label => label.Text == "CAMPAIGN COULD NOT START");
        var message = controls.OfType<Label>().Single(label => label.Text.StartsWith(
            "Stellar Continuum stopped before opening the campaign.", StringComparison.Ordinal));
        var exit = controls.OfType<Button>().Single(button => button.Name == "ExitAfterStartupFailure");
        var backdrop = controls.OfType<ColorRect>().Single();

        AssertInsideViewport(title, "startup failure title");
        AssertInsideViewport(message, "startup failure guidance");
        AssertInsideViewport(exit, "startup failure Exit safely");
        Require(exit.IsVisibleInTree() && !exit.Disabled && exit.FocusMode == Control.FocusModeEnum.All,
            "The startup-failure Exit safely action was not visible, enabled, and keyboard reachable.");
        Require(Encloses(viewport, ScreenRect(backdrop)) && Encloses(ScreenRect(backdrop), viewport),
            "The startup-failure backdrop did not shield the complete viewport.");
        Check(GetTree().Paused, "startup-failure-pauses-partial-world");
        Check(layer.Layer >= 1000, "startup-failure-layer-shields-partial-world");
        Check(exit.CustomMinimumSize.X >= 180 && exit.CustomMinimumSize.Y >= 44,
            "startup-failure-exit-is-readable-and-reachable");

        var pointerRevision = _main.UiPointerCommandRevision;
        await ClickPositionAsync(new Vector2(24, 24), MouseButton.Left);
        Check(_main.UiPointerCommandRevision == pointerRevision && layer.IsInsideTree() && exit.IsVisibleInTree(),
            "startup-failure-backdrop-blocks-gameplay-input");
        Require(HashFile(savePath) == saveHash && new FileInfo(savePath).Length == saveLength,
            "Presenting the startup failure changed the isolated campaign save.");
        Check(true, "startup-failure-preserves-campaign-save-before-exit");
        await SaveViewportAsync(StartupFailureCapture);

        var exitActivated = false;
        void RecordExit()
        {
            exitActivated = true;
            WriteStartupFailureEvidence(saveHash, saveLength, exit, exitActivated);
            GD.Print("STELLAR_FOCUSED_STARTUP_FAILURE_EXIT_ACTIVATED");
        }
        exit.Pressed += RecordExit;
        WriteStartupFailureEvidence(saveHash, saveLength, exit, exitActivated);
        GD.Print("STELLAR_FOCUSED_STARTUP_FAILURE_UI_READY");
        await ClickControlAsync(exit);
        Require(exitActivated, "The actual Exit safely button did not activate.");
        await WaitFramesAsync(180);
        throw new InvalidOperationException("The actual Exit safely action did not terminate the process with its failure code.");
    }

    private void WriteStartupFailureEvidence(string saveHash, long saveLength, Button exit, bool exitActivated)
    {
        var exitBounds = ScreenRect(exit);
        var capturePath = Path.Combine(_outputDirectory, StartupFailureCapture);
        var evidence = new
        {
            schema_version = 1,
            git_sha = System.Environment.GetEnvironmentVariable("STELLAR_CAPTURE_SHA") ?? "unknown",
            scope = "actual IntegratedMain startup failure at 1280x720",
            injection = "--stellar-startup-failure-ui",
            input_mode = "Input.ParseInputEvent",
            viewport = new { width = 1280, height = 720 },
            exit_bounds = new { x = exitBounds.Position.X, y = exitBounds.Position.Y,
                width = exitBounds.Size.X, height = exitBounds.Size.Y },
            save = new { file = "autosave.json", bytes = saveLength, sha256 = saveHash },
            exit_activated = exitActivated,
            checks = _checks.ToArray(),
            capture = new { file = StartupFailureCapture,
                bytes = File.Exists(capturePath) ? new FileInfo(capturePath).Length : 0,
                sha256 = File.Exists(capturePath) ? HashFile(capturePath) : string.Empty },
        };
        File.WriteAllText(Path.Combine(_outputDirectory, StartupFailureEvidence),
            JsonSerializer.Serialize(evidence, new JsonSerializerOptions { WriteIndented = true }));
    }
}
