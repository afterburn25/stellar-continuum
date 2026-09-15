using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace Stellar.Editor;
public static class EditorVerification
{
    public static async Task Run(MainWindow window, string output)
    {
        Directory.CreateDirectory(output);
        var checks = new List<string>();
        void Check(bool condition, string label) { if (!condition) throw new InvalidOperationException(label); checks.Add(label); }
        void Reject(Action action, string label) { bool rejected = false; try { action(); } catch { rejected = true; } Check(rejected, label); }
        try
        {
            window.InitializeVerification(Path.Combine(output, "test-library"));
            window.UpdateLayout();
            await window.GenerateAsync(false);
            await window.Viewport.ArtworkReady;
            Check(window.Viewport.Detailed && window.Viewport.ArtworkResolution == 2048, "Detailed galaxy starts with a completed 2048-pixel background");
            var artworkBuilds = window.Viewport.ArtworkBuildCount;
            Check(window.Project.Catalog?["systems"]?.AsArray().Count == 500, "Real native engine generates 500 systems");
            Check(window.Project.Catalog?["colonies"]?.AsArray().Count == 9, "Real native engine seeds nine starting colonies");
            Check(window.Project.Catalog?["phase"]?.GetValue<string>() == "colonies-before-fleets", "Preview retains truthful migration boundary");
            Check(window.Project.EngineCommit == "e80e87f90563801166aeb68d8d8b8b9ec6ce79f3", "Preview records the packaged engine revision");
            window.Viewport.Fit();
            Check(window.Viewport.SelectAt(window.Viewport.ScreenFor(0)), "Viewport hit testing selects a generated star");
            Check(window.InspectorKind.Text == "System   /   #0", "Viewport selection updates the property inspector");
            window.DisplayNameBox.Text = "Sol — First Light"; window.ObjectNotesBox.Text = "Player arrival and officer introduction."; window.BookmarkBox.IsChecked = true;
            window.ApplyButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(window.Project.Annotations[0].DisplayName == "Sol — First Light", "Apply button persists an authored name");
            Check(window.Project.Annotations[0].Bookmarked && window.Project.Annotations[0].Notes.Contains("officer"), "Notes and bookmark are authored together");
            Check(window.Project.Catalog!["systems"]![0]!["name"]!.GetValue<string>() != "Sol — First Light", "Annotations preserve authoritative generated data");
            window.Undo(); Check(!window.Project.Annotations.ContainsKey(0), "Undo restores previous project state");
            Check(window.Viewport.ArtworkBuildCount == artworkBuilds, "Annotation edits and undo reuse cached galaxy artwork");
            window.DisplayNameBox.Text = "Sol — First Light"; window.ObjectNotesBox.Text = "Player arrival and officer introduction."; window.BookmarkBox.IsChecked = true; window.ApplyAnnotation();
            window.CategoryBox.SelectedIndex = 4; Check(window.ObjectList.Items.Count == 1, "Bookmark filter lists only bookmarked systems");
            window.CategoryBox.SelectedIndex = 0; window.SearchBox.Text = "First Light"; Check(window.ObjectList.Items.Count == 1, "Search includes authored display names");
            window.SearchBox.Text = ""; window.SelectSystem(0);
            var assetFile = Path.Combine(output, "test-tone.wav"); WriteWave(assetFile);
            var asset = window.Library.Import(assetFile, "Verification tone");
            window.Library.Import(assetFile); Check(window.Library.Assets.Count == 1, "Repeated import deduplicates identical files");
            window.AddAsset(asset); Check(window.Project.Assets.Count == 1, "Library asset can be added to a project");
            window.AddAsset(asset); Check(window.Project.Assets.Count == 1, "Repeated project asset add is idempotent");
            File.Delete(assetFile); Check(File.Exists(window.Library.Resolve(asset)), "Imported library asset survives source removal");
            await VerifyAudio(window.Library.Resolve(asset)); checks.Add("Windows audio backend opens a WAV asset for preview");
            var reloadedLibrary = new AssetLibrary(window.Library.Root); Check(reloadedLibrary.Assets.Count == 1, "Asset library survives reopening");
            Reject(() => reloadedLibrary.Resolve(new() { RelativePath = "../escape.txt" }), "Asset library rejects escaping paths");
            var path = Path.Combine(output, "First Light ü.stellar-project"); window.SaveTo(path);
            var first = WorldProject.Load(path); Check(first.Annotations[0].Bookmarked && first.Assets[0].Bytes.SequenceEqual(window.Project.Assets[0].Bytes), "Project round-trip preserves annotations and embedded assets");
            File.Delete(window.Library.Resolve(asset)); window.LoadProject(path);
            Check(File.Exists(window.Library.Resolve(asset)), "Opening a portable project restores missing library assets");
            window.AssetCategoryBox.SelectedIndex = 5; Check(window.AssetList.Items.Count == 1, "Project asset filter shows included library assets");
            window.AssetSearchBox.Text = "not-an-asset"; Check(window.AssetList.Items.Count == 0, "Asset search filters the library");
            window.AssetSearchBox.Text = ""; window.AssetCategoryBox.SelectedIndex = 0;
            window.ProjectNameBox.Text = "Stellar Continuum — Editor verification"; window.SaveTo(path);
            Check(WorldProject.Load(path + ".bak").Name == first.Name, "Atomic save preserves a readable previous project");
            window.LoadProject(path); Check(!window.IsDirty && window.Project.Name.Contains("verification"), "Open restores project and clears the dirty state");
            var corrupt = window.Project.Copy(); corrupt.SchemaVersion = 99; Reject(corrupt.Validate, "Unsupported project versions are rejected");
            corrupt = window.Project.Copy(); corrupt.Seed++; Reject(corrupt.Validate, "Mismatched generation settings are rejected");
            corrupt = window.Project.Copy(); corrupt.Catalog!["systems"]![1]!["id"] = 0; Reject(corrupt.Validate, "Duplicate world object IDs are rejected");
            corrupt = window.Project.Copy(); corrupt.Catalog!["systems"]![1]!["depthLightYears"] = double.PositiveInfinity; Reject(corrupt.Validate, "Nonfinite system depth is rejected before entering the viewport");
            corrupt = window.Project.Copy(); corrupt.Catalog!["core"]!["x"] = "invalid"; Reject(corrupt.Validate, "Invalid galaxy center is rejected before entering the viewport");
            corrupt = window.Project.Copy(); corrupt.Catalog!["radiusLightYears"] = 0.0; Reject(corrupt.Validate, "Nonpositive galaxy radius is rejected before entering the viewport");
            var before = window.Project.Catalog!.ToJsonString(); window.SeedBox.Text = "not-a-seed";
            bool failed = false; try { await window.GenerateAsync(false); } catch (InvalidDataException) { failed = true; }
            Check(failed && window.Project.Catalog.ToJsonString() == before, "Invalid generation request preserves the current world");
            window.Refresh(true);
            var export = Path.Combine(output, "world-snapshot.json"); window.ExportSnapshot(export);
            var exported = JsonNode.Parse(File.ReadAllText(export))!;
            Check(exported["editor"]!["gameplayParity"]!.GetValue<bool>() == false && exported["systems"]!.AsArray().Count == 500, "Snapshot export carries real native data and explicit preview status");
            window.ProjectNameBox.Text = "Stellar Continuum — First Light"; window.SaveTo(path);
            var authoritative = window.Project.Catalog!.ToJsonString();
            window.Viewport.SetOrientation(37, 53); window.Viewport.Fit();
            var anchor = window.Viewport.ScreenFor(0); window.Viewport.ZoomAt(2, anchor);
            Check((window.Viewport.ScreenFor(0) - anchor).Length < .0001, "Zoom keeps the catalog system under the cursor at an oblique angle");
            window.Viewport.PanBy(new Vector(27, -19));
            Check((window.Viewport.ScreenFor(0) - anchor - new Vector(27, -19)).Length < .0001, "Pan moves projected systems by the requested screen distance");
            Check(window.Viewport.SelectAt(window.Viewport.ScreenFor(0)) && window.Viewport.SelectedSystem == 0, "Tilted and rotated galaxy retains accurate system hit testing");
            window.Viewport.FocusSystem(0);
            Check((window.Viewport.ScreenFor(0) - new Point(window.Viewport.ActualWidth / 2, window.Viewport.ActualHeight / 2)).Length < .0001, "Focus centers a system including its native depth");
            window.ViewModeBox.SelectedIndex = 1;
            Check(!window.Viewport.Detailed && !window.TiltSlider.IsEnabled && window.Viewport.SelectAt(window.Viewport.ScreenFor(0)), "Map mode offers selectable systems and disables galaxy tilt");
            Capture(window, Path.Combine(output, "editor-map.png"));
            window.ViewModeBox.SelectedIndex = 0; window.TiltSlider.Value = 24; window.Viewport.SetOrientation(-15, 24);
            Check(window.Viewport.Detailed && window.Viewport.ArtworkBuildCount == artworkBuilds, "Switching views and moving the camera reuse existing artwork");
            Check(window.Project.Catalog.ToJsonString() == authoritative && !window.IsDirty, "View controls do not change world data or dirty the saved project");
            window.Width = 1480; window.Height = 940; await Layout(window); Capture(window, Path.Combine(output, "editor-1480.png"));
            Check(window.Viewport.ActualWidth > 500 && window.ObjectList.ActualHeight > 250, "Large layout keeps viewport and object list usable");
            window.Width = 1180; window.Height = 800; await Layout(window); Capture(window, Path.Combine(output, "editor-1180.png"));
            Check(window.Viewport.ActualWidth >= 350 && window.GenerateButton.ActualWidth > 150, "Compact layout retains usable viewport and generation controls");
            window.ExpandButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(window.IsViewportExpanded && window.Viewport.ActualWidth > 1100 && window.ObjectList.IsVisible == false, "Expand gives the galaxy the full workspace");
            window.ExpandButton.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(!window.IsViewportExpanded && window.ObjectList.IsVisible, "Restore returns the editing panels");
            window.Width = 1480; window.Height = 940; window.SetExpanded(true); await Layout(window);
            Capture(window, Path.Combine(output, "editor-galaxy-expanded.png"));
            CaptureElement(window.Viewport, Path.Combine(output, "galaxy-view.png"));
            window.CountBox.SelectedIndex = 3; window.SeedBox.Text = "932017";
            await window.GenerateAsync(false); await window.Viewport.ArtworkReady; await Layout(window);
            Check(window.Project.Catalog!["systems"]!.AsArray().Count == 2500 && window.Viewport.ArtworkResolution == 2048, "Largest supported 2500-system world builds detailed galaxy artwork");
            Check(window.Viewport.SelectAt(window.Viewport.ScreenFor(2499)) && window.Viewport.SelectedSystem == 2499, "Largest supported world keeps its last catalog system selectable");
            Capture(window, Path.Combine(output, "editor-galaxy-2500.png"));
            await File.WriteAllTextAsync(Path.Combine(output, "verification.json"), JsonSerializer.Serialize(new { passed = checks.Count, checks, engineVersion = window.Project.EngineVersion, engineCommit = window.Project.EngineCommit, standaloneCleanMachine = false }, WorldProject.JsonOptions));
            window.CloseVerification(); Application.Current.Shutdown(0);
        }
        catch (Exception error)
        {
            await File.WriteAllTextAsync(Path.Combine(output, "verification-failure.txt"), error.ToString());
            window.CloseVerification(); Application.Current.Shutdown(1);
        }
    }
    private static async Task Layout(MainWindow window) { window.UpdateLayout(); await window.Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle); window.Viewport.Fit(); window.UpdateLayout(); }
    private static async Task VerifyAudio(string path)
    {
        var completion = new TaskCompletionSource<bool>(); var player = new MediaPlayer { Volume = 0 };
        player.MediaOpened += (_, _) => completion.TrySetResult(player.NaturalDuration.HasTimeSpan && player.NaturalDuration.TimeSpan.TotalSeconds > 0);
        player.MediaFailed += (_, e) => completion.TrySetException(e.ErrorException);
        try { player.Open(new Uri(path)); if (!await completion.Task.WaitAsync(TimeSpan.FromSeconds(15))) throw new IOException("WAV preview did not expose an audio duration."); }
        finally { player.Close(); }
    }
    public static void Capture(MainWindow window, string path)
    {
        CaptureElement(window.RootGrid, path);
    }
    private static void CaptureElement(FrameworkElement target, string path)
    {
        var bitmap = new RenderTargetBitmap((int)target.ActualWidth, (int)target.ActualHeight, 96, 96, PixelFormats.Pbgra32);
        var drawing = new DrawingVisual();
        using (var context = drawing.RenderOpen()) context.DrawRectangle(new VisualBrush(target) { Stretch = Stretch.Fill }, null, new Rect(0, 0, target.ActualWidth, target.ActualHeight));
        bitmap.Render(drawing); var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap)); using var stream = File.Create(path); encoder.Save(stream);
    }
    private static void WriteWave(string path)
    {
        using var stream = File.Create(path); using var writer = new BinaryWriter(stream);
        writer.Write(Encoding.ASCII.GetBytes("RIFF")); writer.Write(36 + 4800); writer.Write(Encoding.ASCII.GetBytes("WAVEfmt "));
        writer.Write(16); writer.Write((short)1); writer.Write((short)1); writer.Write(24000); writer.Write(48000); writer.Write((short)2); writer.Write((short)16); writer.Write(Encoding.ASCII.GetBytes("data")); writer.Write(4800);
        for (int i = 0; i < 2400; i++) writer.Write((short)(Math.Sin(i * Math.Tau * 440 / 24000) * 1000));
    }
}
