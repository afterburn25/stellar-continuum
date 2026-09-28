using Microsoft.Win32;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Stellar.Editor;
public partial class MainWindow : Window
{
    public WorldProject Project { get; private set; } = new();
    public AssetLibrary Library { get; private set; } = null!;
    public string? ProjectPath { get; private set; }
    public bool IsDirty { get; private set; }
    public bool IsBusy { get; private set; }
    private List<WorldItem> objects = [];
    private WorldItem? selected;
    private bool ready, refreshing, verifiedClose;
    private bool updatingCamera;
    private readonly Dictionary<UIElement, Visibility> panelVisibility = [];
    public bool IsViewportExpanded { get; private set; }
    private CancellationTokenSource? generation;
    private readonly Stack<WorldProject> undo = new(), redo = new();
    private readonly MediaPlayer audio = new();
    public MainWindow()
    {
        InitializeComponent();
        Viewport.SystemSelected += id => SelectSystem(id);
        Viewport.CameraChanged += UpdateCameraControls;
        PreviewKeyDown += Shortcuts;
        audio.MediaFailed += (_, e) => Report("This audio could not be played: " + e.ErrorException.Message);
    }
    public async Task StartAsync(string? path)
    {
        Library = new AssetLibrary(); ready = true;
        var starter = Path.Combine(AppContext.BaseDirectory, "StarterAssets");
        if (Directory.Exists(starter))
            foreach (var file in Directory.EnumerateFiles(starter, "*", SearchOption.AllDirectories))
                Library.Import(file, "Stellar Continuum / " + Path.GetRelativePath(starter, file));
        RefreshAssets();
        if (!string.IsNullOrEmpty(path)) { Try(() => LoadProject(path)); return; }
        var sample = Path.Combine(AppContext.BaseDirectory, "Samples", "First Light.stellar-project");
        if (File.Exists(sample)) { Project = WorldProject.Load(sample); ProjectPath = null; Refresh(true); Log("Opened the First Light sample. Save a copy to begin your project."); }
        else { Refresh(true); await GenerateAsync(false); IsDirty = false; UpdateTitle(); }
    }
    public void InitializeVerification(string libraryPath) { Library = new AssetLibrary(libraryPath); ready = true; Refresh(true); RefreshAssets(); }
    private void Checkpoint()
    {
        if (undo.Count >= 12) { var recent = undo.Take(11).Reverse().ToList(); undo.Clear(); foreach (var entry in recent) undo.Push(entry); }
        undo.Push(Project.Copy()); redo.Clear(); IsDirty = true;
    }
    private void UpdateTitle() { ProjectTitle.Text = Project.Name + (IsDirty ? "  •  Unsaved" : ""); Title = Project.Name + (IsDirty ? " *" : "") + " — Stellar Engine Editor"; }
    public void Refresh(bool fit = false)
    {
        refreshing = true;
        ProjectNameBox.Text = Project.Name; SeedBox.Text = Project.Seed.ToString(CultureInfo.InvariantCulture);
        CountBox.SelectedIndex = Array.IndexOf(new[] { 250, 500, 1000, 2500 }, Project.SystemCount);
        objects = WorldData.Items(Project); EmptyHint.Visibility = objects.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        Viewport.SetWorld(objects, Project.Annotations, Project.Catalog, Project.Seed); refreshing = false; RefreshFilter();
        if (fit) { Viewport.Fit(); if (objects.Count > 0) SelectSystem(0); }
        UpdateTitle(); RefreshAssets();
    }
    private void RefreshFilter()
    {
        if (!ready || refreshing) return;
        var kind = CategoryBox.SelectedIndex switch { 1 => "Body", 2 => "Civilization", 3 => "Colony", _ => "System" };
        var query = objects.Where(o => o.Kind == kind);
        if (CategoryBox.SelectedIndex == 4) query = query.Where(o => Project.Annotations.TryGetValue(o.Id, out var a) && a.Bookmarked);
        var search = SearchBox.Text.Trim();
        if (search.Length > 0) query = query.Where(o => o.Name.Contains(search, StringComparison.OrdinalIgnoreCase));
        var rows = query.ToList(); ObjectList.ItemsSource = rows; ObjectCount.Text = $"{rows.Count:N0} {kind.ToLowerInvariant()}{(rows.Count == 1 ? "" : "s")}";
    }
    private void FilterChanged(object sender, SelectionChangedEventArgs e) => RefreshFilter();
    private void SearchChanged(object sender, TextChangedEventArgs e) => RefreshFilter();
    private void ObjectSelected(object sender, SelectionChangedEventArgs e) { if (ObjectList.SelectedItem is WorldItem item) Inspect(item); }
    public void SelectSystem(int id)
    {
        CategoryBox.SelectedIndex = 0; SearchBox.Text = ""; RefreshFilter();
        var item = objects.FirstOrDefault(o => o.Kind == "System" && o.Id == id);
        if (item is null) return;
        ObjectList.SelectedItem = item; ObjectList.ScrollIntoView(item); Inspect(item);
    }
    private static string Friendly(string name) { var text = Regex.Replace(name, "([a-z])([A-Z])", "$1 $2"); return char.ToUpperInvariant(text[0]) + text[1..]; }
    private static string Value(JsonNode? node)
    {
        if (node is JsonValue value)
        {
            if (value.TryGetValue<bool>(out var boolean)) return boolean ? "Yes" : "No";
            if (value.TryGetValue<double>(out var number)) return number.ToString("#,0.###", CultureInfo.InvariantCulture);
            return value.ToString();
        }
        return node is JsonArray a ? $"{a.Count} entries" : node is JsonObject ? "Details below" : "—";
    }
    private void Row(string label, string value)
    {
        var grid = new Grid { Margin = new Thickness(0, 0, 0, 8) };
        grid.ColumnDefinitions.Add(new() { Width = new GridLength(1, GridUnitType.Star) }); grid.ColumnDefinitions.Add(new() { Width = new GridLength(1, GridUnitType.Star) });
        grid.Children.Add(new TextBlock { Text = label, Foreground = (Brush)FindResource("Muted"), FontSize = 11, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 8, 0) });
        var text = new TextBlock { Text = value, FontSize = 11, TextWrapping = TextWrapping.Wrap }; Grid.SetColumn(text, 1); grid.Children.Add(text); PropertyPanel.Children.Add(grid);
    }
    private void Inspect(WorldItem item)
    {
        selected = item; InspectorTitle.Text = item.Name; InspectorKind.Text = item.Kind + "   /   #" + item.Id;
        Viewport.SelectedSystem = item.SystemId; Viewport.InvalidateVisual(); PropertyPanel.Children.Clear();
        if (item.Kind == "System")
        {
            var type = Math.Clamp(item.Data["primaryClass"]?.GetValue<int>() ?? 2, 0, 11);
            Row("Star type", GalaxyViewport.Classes[type]);
            Row("Planets and moons", objects.Count(o => o.Kind == "Body" && o.SystemId == item.Id).ToString());
            Row("Habitable world", Value(item.Data["hasHabitableWorld"]));
            Row("Anomaly", Value(item.Data["hasAnomaly"])); Row("Rare resources", Value(item.Data["hasRareResource"]));
            Row("Position X (ly)", Value(item.Data["xLightYears"])); Row("Position Y (ly)", Value(item.Data["yLightYears"])); Row("Depth (ly)", Value(item.Data["depthLightYears"]));
            foreach (var key in new[] { "secondaryClass", "tertiaryClass" }) if (item.Data[key] is JsonNode companion) Row("Companion star", GalaxyViewport.Classes[Math.Clamp(companion.GetValue<int>(), 0, 11)]);
        }
        else foreach (var pair in item.Data)
        {
            if (pair.Key == "name") continue;
            if (pair.Value is JsonObject nested) { foreach (var detail in nested) Row(Friendly(detail.Key), Value(detail.Value)); }
            else if (pair.Key.EndsWith("Class") && pair.Value is not null) Row(Friendly(pair.Key), GalaxyViewport.Classes[Math.Clamp(pair.Value.GetValue<int>(), 0, 11)]);
            else Row(Friendly(pair.Key), Value(pair.Value));
        }
        if (item.Kind == "System")
        {
            var note = Project.Annotations.GetValueOrDefault(item.Id) ?? new();
            DisplayNameBox.Text = note.DisplayName; ObjectNotesBox.Text = note.Notes; BookmarkBox.IsChecked = note.Bookmarked;
        }
        AnnotationPanel.Visibility = item.Kind == "System" ? Visibility.Visible : Visibility.Collapsed;
    }
    public void ApplyAnnotation()
    {
        if (IsBusy || selected?.Kind != "System") return;
        var id = selected.Id; Checkpoint();
        Project.Annotations[id] = new() { DisplayName = DisplayNameBox.Text.Trim(), Notes = ObjectNotesBox.Text, Bookmarked = BookmarkBox.IsChecked == true };
        Refresh(); SelectSystem(id); Log("Saved this system's name, notes and bookmark to the project. Ctrl+Z undoes the change.");
    }
    private void ApplyClick(object sender, RoutedEventArgs e) => Try(ApplyAnnotation);
    public async Task GenerateAsync(bool prompt = true)
    {
        if (IsBusy) return;
        if (!long.TryParse(SeedBox.Text, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var seed)) throw new InvalidDataException("Enter a whole-number seed between -9223372036854775808 and 9223372036854775807.");
        var count = int.Parse(((ComboBoxItem)CountBox.SelectedItem).Content.ToString()!, CultureInfo.InvariantCulture);
        if (prompt && Project.Catalog is not null && MessageBox.Show(this, "Generate a new world? The current world and its system annotations will be replaced. You can undo this afterward.", "Generate world", MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK) return;
        SetBusy(true); generation = new(); Log($"Generating {count:N0} systems with seed {seed}…");
        try
        {
            var result = await EngineRunner.Generate(seed, count, generation.Token);
            var candidate = Project.Copy(); candidate.Seed = seed; candidate.SystemCount = count; candidate.Catalog = result.Catalog; candidate.Annotations.Clear();
            candidate.EngineVersion = result.Receipt["engineVersion"]!.GetValue<string>(); candidate.EngineCommit = result.Receipt["sourceCommit"]!.GetValue<string>(); candidate.Validate();
            Checkpoint(); Project = candidate; Refresh(true);
            var bodies = result.Catalog["planetaryBodies"]!.AsArray().Count; var colonies = result.Catalog["colonies"]!.AsArray().Count;
            Log($"Generated {count:N0} systems, {bodies:N0} bodies and {colonies} colonies using Stellar Engine {Project.EngineVersion}.");
            StatusText.Text = $"World ready  ·  {count:N0} systems  ·  {bodies:N0} bodies  ·  {colonies} colonies";
        }
        finally { generation.Dispose(); generation = null; SetBusy(false); }
    }
    private async void GenerateClick(object sender, RoutedEventArgs e) { try { await GenerateAsync(); } catch (Exception error) { Report(error.Message); } }
    private void SetBusy(bool value)
    {
        IsBusy = value; GenerateButton.IsEnabled = !value; NewButton.IsEnabled = !value; OpenButton.IsEnabled = !value; SaveButton.IsEnabled = !value;
        ApplyButton.IsEnabled = !value; SeedBox.IsEnabled = !value; CountBox.IsEnabled = !value; ProjectNameBox.IsEnabled = !value;
        CancelButton.Visibility = value ? Visibility.Visible : Visibility.Collapsed;
        if (value) StatusText.Text = "The engine is generating your world…";
    }
    private void CancelClick(object sender, RoutedEventArgs e) => generation?.Cancel();
    public void Undo() { if (IsBusy || undo.Count == 0) return; redo.Push(Project.Copy()); Project = undo.Pop(); IsDirty = true; Refresh(true); Log("Undid the last project change."); }
    private void Redo() { if (IsBusy || redo.Count == 0) return; undo.Push(Project.Copy()); Project = redo.Pop(); IsDirty = true; Refresh(true); Log("Redid the project change."); }
    private void Shortcuts(object sender, KeyEventArgs e)
    {
        if ((Keyboard.Modifiers & ModifierKeys.Control) == 0) return;
        if (e.Key == Key.S) { Try(() => SaveProject()); e.Handled = true; }
        if (e.OriginalSource is TextBox) return;
        if (e.Key == Key.Z) { Undo(); e.Handled = true; }
        if (e.Key == Key.Y) { Redo(); e.Handled = true; }
        if (e.Key == Key.O) { OpenClick(this, new()); e.Handled = true; }
        if (e.Key == Key.N) { NewClick(this, new()); e.Handled = true; }
    }
    public void SaveTo(string path) { CommitName(); Project.Save(path); ProjectPath = path; IsDirty = false; UpdateTitle(); Log("Project saved. Imported project assets and the generated world are included."); }
    public bool SaveProject(bool saveAs = false)
    {
        if (IsBusy) return false;
        var path = saveAs ? null : ProjectPath;
        if (path is null)
        {
            var dialog = new SaveFileDialog { Title = "Save Stellar Engine project", Filter = "Stellar Engine project|*.stellar-project", DefaultExt = ".stellar-project", FileName = "First Light.stellar-project" };
            if (dialog.ShowDialog(this) != true) return false; path = dialog.FileName;
        }
        SaveTo(path); return true;
    }
    private void SaveClick(object sender, RoutedEventArgs e) => Try(() => SaveProject());
    private void SaveAsClick(object sender, RoutedEventArgs e) => Try(() => SaveProject(true));
    private bool CanReplace()
    {
        if (IsBusy) return false; CommitName(); if (!IsDirty) return true;
        var answer = MessageBox.Show(this, "Save your project changes before continuing?", "Stellar Engine Editor", MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
        return answer == MessageBoxResult.No || answer == MessageBoxResult.Yes && SaveProject();
    }
    public void LoadProject(string path)
    {
        var loaded = WorldProject.Load(path);
        foreach (var asset in loaded.Assets) Library.ImportBytes(asset.Name, asset.Bytes, "Restored from project: " + loaded.Name);
        Project = loaded; ProjectPath = path; IsDirty = false; undo.Clear(); redo.Clear(); selected = null; PropertyPanel.Children.Clear(); AnnotationPanel.Visibility = Visibility.Collapsed;
        InspectorTitle.Text = "Select a star system"; InspectorKind.Text = "Properties appear here"; Refresh(true); Log("Project opened.");
    }
    private void OpenClick(object sender, RoutedEventArgs e) => Try(() =>
    {
        if (!CanReplace()) return;
        var dialog = new OpenFileDialog { Filter = "Stellar Engine project|*.stellar-project", Title = "Open project" };
        if (dialog.ShowDialog(this) == true) LoadProject(dialog.FileName);
    });
    private void NewClick(object sender, RoutedEventArgs e) => Try(() =>
    {
        if (!CanReplace()) return;
        Project = new(); ProjectPath = null; IsDirty = false; undo.Clear(); redo.Clear(); selected = null;
        PropertyPanel.Children.Clear(); AnnotationPanel.Visibility = Visibility.Collapsed; InspectorTitle.Text = "Select a star system"; InspectorKind.Text = "Properties appear here"; Refresh(true); Log("New world project. Choose a seed and generate it.");
    });
    private void CommitName()
    {
        if (IsBusy) return; var name = ProjectNameBox.Text.Trim();
        if (name.Length == 0) { ProjectNameBox.Text = Project.Name; return; }
        if (Project.Name != name) { Checkpoint(); Project.Name = name; UpdateTitle(); }
    }
    private void ProjectNameChanged(object sender, KeyboardFocusChangedEventArgs e) { if (ready && !refreshing) CommitName(); }
    public void ExportSnapshot(string path)
    {
        if (Project.Catalog is null) throw new InvalidOperationException("Generate a world before exporting a snapshot.");
        var output = Project.Catalog.DeepClone().AsObject();
        output["editor"] = JsonSerializer.SerializeToNode(new { projectName = Project.Name, annotations = Project.Annotations, engineVersion = Project.EngineVersion, engineCommit = Project.EngineCommit, gameplayParity = false });
        WorldProject.AtomicWrite(path, output.ToJsonString(WorldProject.JsonOptions)); Log("Exported the native world snapshot with project annotations. This is preview data, not a playable game or campaign save.");
    }
    private void ExportClick(object sender, RoutedEventArgs e) => Try(() =>
    {
        if (IsBusy) return;
        var dialog = new SaveFileDialog { Title = "Export world snapshot (preview data)", Filter = "World snapshot JSON|*.json", FileName = "stellar-world.json" };
        if (dialog.ShowDialog(this) == true) ExportSnapshot(dialog.FileName);
    });
    public void RefreshAssets()
    {
        if (Library is null) return;
        var query = Library.Assets.AsEnumerable();
        var category = ((ComboBoxItem)AssetCategoryBox.SelectedItem).Content.ToString();
        if (AssetCategoryBox.SelectedIndex == 5) query = query.Where(a => Project.Assets.Any(p => p.Name == a.Name));
        else if (AssetCategoryBox.SelectedIndex > 0) query = query.Where(a => a.Category == category);
        var search = AssetSearchBox.Text.Trim();
        if (search.Length > 0) query = query.Where(a => a.Name.Contains(search, StringComparison.OrdinalIgnoreCase) || a.Description.Contains(search, StringComparison.OrdinalIgnoreCase));
        AssetList.ItemsSource = query.OrderBy(a => a.Category).ThenBy(a => a.Name).ToList();
        AssetSummary.Text = $"{Library.Assets.Count} library assets   ·   {Project.Assets.Count} included in this project";
    }
    private void AssetFilterChanged(object sender, SelectionChangedEventArgs e) { if (ready) RefreshAssets(); }
    private void AssetSearchChanged(object sender, TextChangedEventArgs e) { if (ready) RefreshAssets(); }
    private void ImportClick(object sender, RoutedEventArgs e) => Try(() =>
    {
        var dialog = new OpenFileDialog { Title = "Import into Engine Assets", Filter = "Engine assets|*.png;*.jpg;*.jpeg;*.svg;*.webp;*.wav;*.mp3;*.ogg;*.flac;*.glb;*.gltf;*.obj;*.json;*.csv;*.txt;*.md", Multiselect = true };
        if (dialog.ShowDialog(this) != true) return;
        foreach (var path in dialog.FileNames) Library.Import(path);
        RefreshAssets(); Log("Imported assets into the reusable library. Original files were kept.");
    });
    public void AddAsset(LibraryAsset asset)
    {
        if (IsBusy) return;
        var copy = Library.CopyToProject(asset); var existing = Project.Assets.FirstOrDefault(a => a.Name == copy.Name);
        if (existing is not null)
        {
            if (existing.Bytes.SequenceEqual(copy.Bytes)) { Log("This asset is already included in the project."); return; }
            throw new InvalidDataException("A different asset with this name is already in the project. Import the new asset with a distinct filename.");
        }
        var candidate = Project.Copy(); candidate.Assets.Add(copy); candidate.Validate(); Checkpoint(); Project = candidate; RefreshAssets(); UpdateTitle(); Log("Added " + asset.Name + " to the portable project.");
    }
    private void AddAssetClick(object sender, RoutedEventArgs e) => Try(() => { if (AssetList.SelectedItem is LibraryAsset a) AddAsset(a); else Log("Select an asset first."); });
    private void AssetSelected(object sender, SelectionChangedEventArgs e) { if (AssetList.SelectedItem is LibraryAsset a) AssetSummary.Text = $"{a.Category}   ·   {a.Size / 1024.0:N0} KB   ·   {a.Description}"; }
    private void AssetFolderClick(object sender, RoutedEventArgs e) => Try(() => Process.Start(new ProcessStartInfo("explorer.exe") { ArgumentList = { Library.Root }, UseShellExecute = false }));
    private void PreviewAssetClick(object sender, RoutedEventArgs e) => Try(() =>
    {
        if (AssetList.SelectedItem is not LibraryAsset asset) { Log("Select an asset to preview."); return; }
        var path = Library.Resolve(asset);
        if (asset.Category == "Audio") { audio.Stop(); audio.Open(new Uri(path)); audio.Volume = .7; audio.Play(); Log("Playing " + asset.Name); return; }
        if (new[] { ".png", ".jpg", ".jpeg" }.Contains(Path.GetExtension(path).ToLowerInvariant()))
        {
            var bitmap = new BitmapImage(); bitmap.BeginInit(); bitmap.CacheOption = BitmapCacheOption.OnLoad; bitmap.UriSource = new Uri(path); bitmap.EndInit(); bitmap.Freeze();
            new Window { Owner = this, Title = asset.Name, Width = 860, Height = 650, Background = Brushes.Black, Content = new Image { Source = bitmap, Stretch = Stretch.Uniform, Margin = new Thickness(15) } }.Show(); return;
        }
        MessageBox.Show(this, $"{asset.Name}\n{asset.Category} · {asset.Size:N0} bytes\n\nStored and reusable. A visual preview for this format is not available in the first editor build.", "Asset details");
    });
    private void StopAudioClick(object sender, RoutedEventArgs e) { audio.Stop(); Log("Audio stopped."); }
    private void FitClick(object sender, RoutedEventArgs e) => Viewport.Fit();
    private void ViewModeChanged(object sender, SelectionChangedEventArgs e)
    {
        if (Viewport is not null && !updatingCamera) Viewport.SetDetailed(ViewModeBox.SelectedIndex == 0);
    }
    private void TiltChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
    {
        if (Viewport is not null && !updatingCamera) Viewport.SetOrientation(Viewport.RotationDegrees, e.NewValue);
    }
    private void UpdateCameraControls()
    {
        updatingCamera = true;
        ViewModeBox.SelectedIndex = Viewport.Detailed ? 0 : 1;
        TiltSlider.IsEnabled = Viewport.Detailed; TiltSlider.Value = Viewport.TiltDegrees;
        TiltLabel.Text = $"{Viewport.TiltDegrees:0}°";
        updatingCamera = false;
    }
    public void SetExpanded(bool expanded)
    {
        if (IsViewportExpanded == expanded) return;
        IsViewportExpanded = expanded;
        if (expanded)
        {
            foreach (UIElement panel in WorkspaceGrid.Children)
                if (panel != GalaxyPanel) { panelVisibility[panel] = panel.Visibility; panel.Visibility = Visibility.Collapsed; }
        }
        else { foreach (var pair in panelVisibility) pair.Key.Visibility = pair.Value; panelVisibility.Clear(); }
        Grid.SetColumn(GalaxyPanel, expanded ? 0 : 2); Grid.SetColumnSpan(GalaxyPanel, expanded ? 5 : 1); Grid.SetRowSpan(GalaxyPanel, expanded ? 3 : 1);
        ExpandButton.Content = expanded ? "Restore" : "Expand";
        UpdateLayout(); Viewport.Fit();
    }
    private void ExpandClick(object sender, RoutedEventArgs e) => SetExpanded(!IsViewportExpanded);
    private void FocusClick(object sender, RoutedEventArgs e) { if (selected is not null) Viewport.FocusSystem(selected.SystemId); }
    private void LabelsChanged(object sender, RoutedEventArgs e) { if (Viewport is not null) { Viewport.ShowNames = LabelsBox.IsChecked == true; Viewport.InvalidateVisual(); } }
    private void HelpClick(object sender, RoutedEventArgs e) => MessageBox.Show(this,
        "STELLAR ENGINE EDITOR 0.1.1\n\n1. Generate a world with the converted native engine.\n2. Explore the detailed Galaxy view: scroll to zoom, drag to pan, right-drag to orbit, or adjust Viewing angle. Expand fills the workspace. Map shows clear editing markers.\n3. Click a system to inspect it, add names, notes and bookmarks, then click Apply.\n4. Import art, voices, models and data into Engine Assets. Add selected assets to your project to include a portable copy.\n5. Save a .stellar-project or export a world snapshot.\n\nCtrl+S saves. Ctrl+Z / Ctrl+Y undo and redo project changes outside text fields. F focuses the selected system.\n\nGalaxy light and dust are illustrative; selectable systems use the generated world data. Full campaign play, a native game renderer, a scene/model editor and playable game export are still being developed.\n\nEngine Assets: " + Library.Root, "Using Stellar Engine Editor");
    public void Log(string text) { LogBox.AppendText(text + "\n\n"); LogBox.ScrollToEnd(); }
    private void Report(string text) { Log(text); StatusText.Text = text; MessageBox.Show(this, text, "Stellar Engine Editor", MessageBoxButton.OK, MessageBoxImage.Information); }
    private void Try(Action work) { try { work(); } catch (Exception error) { Report(error.Message); } }
    private void OnClosing(object? sender, CancelEventArgs e)
    {
        if (verifiedClose) { audio.Close(); return; }
        try { if (IsBusy) { generation?.Cancel(); e.Cancel = true; Log("Cancelling generation. Close the editor once it finishes."); return; } e.Cancel = !CanReplace(); if (!e.Cancel) audio.Close(); }
        catch (Exception error) { e.Cancel = true; Report(error.Message); }
    }
    public void CloseVerification() { verifiedClose = true; Close(); }
}
