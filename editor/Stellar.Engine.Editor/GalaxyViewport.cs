using System.Globalization;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Stellar.Editor;
public sealed class GalaxyViewport : FrameworkElement
{
    private List<WorldItem> stars = [];
    private Dictionary<int, Annotation> annotations = [];
    private double scale = 1, fitScale = 1;
    private Point center, core, down, last;
    private double radius = 128, yaw = -.26, tilt = 24 * Math.PI / 180;
    private bool dragging, moved, orbiting;
    private BitmapSource? artwork;
    private (long, double, double, double, int)? artworkKey;
    private CancellationTokenSource? artworkCancellation;
    private int artworkGeneration;
    private string? artworkError;
    public int SelectedSystem { get; set; } = -1;
    public bool ShowNames { get; set; }
    public bool Detailed { get; private set; } = true;
    public double TiltDegrees => tilt * 180 / Math.PI;
    public double RotationDegrees => yaw * 180 / Math.PI;
    public int ArtworkBuildCount { get; private set; }
    public int ArtworkResolution => artwork?.PixelWidth ?? 0;
    public Task ArtworkReady { get; private set; } = Task.CompletedTask;
    public event Action<int>? SystemSelected;
    public event Action? CameraChanged;
    public static readonly string[] Classes = ["Red dwarf", "Orange dwarf", "Yellow dwarf", "Yellow-white dwarf", "White star", "Hot blue star", "Giant", "White dwarf", "Neutron star", "Black hole", "Protostar", "Pulsar"];
    private static readonly string[] Colors = ["#DA7567", "#EFB078", "#F5DA9D", "#EAE5C7", "#EBF1FF", "#83B9FF", "#F09965", "#C5D6F3", "#77DAE6", "#A689D4", "#DAA17A", "#AA98EF"];
    private static readonly SolidColorBrush[] StarBrushes = Colors.Select(Brush).ToArray();
    private static readonly SolidColorBrush Background = Brush("#03060C");
    private static readonly SolidColorBrush FieldBrush = Brush("#5E718C");
    private static readonly Pen GridPen = new(Brush("#142131"), 1);
    private static readonly Pen SelectionPen = new(Brush("#88EDDE"), 1.4);
    private static readonly Pen BookmarkPen = new(Brush("#CFB876"), 1);
    public GalaxyViewport()
    {
        Focusable = true; ClipToBounds = true; Cursor = Cursors.Cross;
        RenderOptions.SetBitmapScalingMode(this, BitmapScalingMode.HighQuality);
        SizeChanged += (_, _) => InvalidateVisual();
    }
    public void SetWorld(List<WorldItem> items, Dictionary<int, Annotation> notes, JsonObject? catalog, long seed)
    {
        stars = items.Where(i => i.Kind == "System").ToList(); annotations = notes;
        core = new(catalog?["core"]?["x"]?.GetValue<double>() ?? 0, catalog?["core"]?["y"]?.GetValue<double>() ?? 0);
        radius = Math.Max(1, catalog?["radiusLightYears"]?.GetValue<double>() ?? 128);
        var key = (seed, radius, core.X, core.Y, stars.Count);
        if (artworkKey != key)
        {
            artworkKey = key; artwork = null; artworkError = null;
            artworkCancellation?.Cancel(); artworkCancellation?.Dispose();
            artworkCancellation = new(); var generation = ++artworkGeneration;
            ArtworkReady = stars.Count == 0 ? Task.CompletedTask : BuildArtwork(seed, generation, artworkCancellation.Token);
        }
        InvalidateVisual();
    }
    private async Task BuildArtwork(long seed, int generation, CancellationToken token)
    {
        try
        {
            ArtworkBuildCount++;
            var result = await Task.Run(() => GalaxyArtwork.Build(seed, token), token);
            if (generation == artworkGeneration && !token.IsCancellationRequested) { artwork = result; InvalidateVisual(); }
        }
        catch (OperationCanceledException) { }
        catch (Exception error) { if (generation == artworkGeneration) { artworkError = error.Message; InvalidateVisual(); } }
    }
    private static (double X, double Y, double Z) Position(WorldItem star) =>
        (star.Data["xLightYears"]!.GetValue<double>(), star.Data["yLightYears"]!.GetValue<double>(), star.Data["depthLightYears"]?.GetValue<double>() ?? 0);
    private Point Project(double x, double y, double z = 0)
    {
        var dx = x - core.X; var dy = y - core.Y;
        if (!Detailed) return new(dx, dy);
        return new(dx * Math.Cos(yaw) - dy * Math.Sin(yaw), (dx * Math.Sin(yaw) + dy * Math.Cos(yaw)) * Math.Cos(tilt) + z * Math.Sin(tilt));
    }
    private Point Project(WorldItem star) { var p = Position(star); return Project(p.X, p.Y, p.Z); }
    private Point ToScreen(Point projected) => new(ActualWidth / 2 + (projected.X - center.X) * scale, ActualHeight / 2 - (projected.Y - center.Y) * scale);
    public Point ScreenFor(int id) => ToScreen(Project(stars.First(s => s.Id == id)));
    public void Fit()
    {
        if (stars.Count == 0 || ActualWidth < 1 || ActualHeight < 1) return;
        var points = stars.Select(Project).ToList();
        if (Detailed)
            for (var n = 0; n < 128; n++)
            {
                var a = n * Math.Tau / 128;
                points.Add(Project(core.X + Math.Cos(a) * radius * 1.08, core.Y + Math.Sin(a) * radius * .72 * 1.08));
            }
        var minX = points.Min(p => p.X); var maxX = points.Max(p => p.X); var minY = points.Min(p => p.Y); var maxY = points.Max(p => p.Y);
        center = new((minX + maxX) / 2, (minY + maxY) / 2);
        scale = Math.Max(.0001, Math.Min(Math.Max(10, ActualWidth - 64) / Math.Max(1, maxX - minX), Math.Max(10, ActualHeight - 58) / Math.Max(1, maxY - minY)));
        fitScale = scale; InvalidateVisual();
    }
    public void SetDetailed(bool detailed) { Detailed = detailed; Fit(); CameraChanged?.Invoke(); }
    public void SetOrientation(double rotation, double inclination)
    {
        if (!Detailed || !double.IsFinite(rotation) || !double.IsFinite(inclination)) return;
        var y = center.Y / Math.Cos(tilt);
        var xWorld = center.X * Math.Cos(yaw) + y * Math.Sin(yaw);
        var yWorld = -center.X * Math.Sin(yaw) + y * Math.Cos(yaw);
        yaw = Math.IEEERemainder(rotation, 360) * Math.PI / 180;
        tilt = Math.Clamp(inclination, 0, 65) * Math.PI / 180;
        center = Project(core.X + xWorld, core.Y + yWorld);
        InvalidateVisual(); CameraChanged?.Invoke();
    }
    public void FocusSystem(int id)
    {
        var star = stars.FirstOrDefault(s => s.Id == id); if (star is null) return;
        center = Project(star); SelectedSystem = id; InvalidateVisual();
    }
    public void ZoomAt(double multiplier, Point p)
    {
        if (!double.IsFinite(multiplier) || multiplier <= 0) return;
        var old = scale; scale = Math.Clamp(scale * multiplier, .0001, 200);
        center.X += (p.X - ActualWidth / 2) * (1 / old - 1 / scale);
        center.Y -= (p.Y - ActualHeight / 2) * (1 / old - 1 / scale);
        InvalidateVisual();
    }
    public void PanBy(Vector delta)
    {
        center.X -= delta.X / scale; center.Y += delta.Y / scale; InvalidateVisual();
    }
    protected override void OnRender(DrawingContext dc)
    {
        base.OnRender(dc);
        dc.DrawRectangle(Background, null, new Rect(RenderSize));
        if (Detailed)
        {
            var field = new Random(831147);
            for (var i = 0; i < 190; i++)
            {
                var p = new Point(field.NextDouble() * ActualWidth, field.NextDouble() * ActualHeight);
                var r = i % 31 == 0 ? .85 : .38; dc.DrawEllipse(FieldBrush, null, p, r, r);
            }
            if (artwork is not null)
            {
                var cosine = Math.Cos(yaw); var sine = Math.Sin(yaw); var inclination = Math.Cos(tilt);
                var matrix = new Matrix(cosine * scale, -sine * inclination * scale, -sine * scale, -cosine * inclination * scale,
                    ActualWidth / 2 - center.X * scale, ActualHeight / 2 + center.Y * scale);
                dc.PushTransform(new MatrixTransform(matrix));
                dc.DrawImage(artwork, new Rect(-radius * 1.12, -radius * .72 * 1.12, radius * 2.24, radius * .72 * 2.24));
                dc.Pop();
            }
        }
        else
        {
            for (double x = 0; x < ActualWidth; x += 64) dc.DrawLine(GridPen, new Point(x, 0), new Point(x, ActualHeight));
            for (double y = 0; y < ActualHeight; y += 64) dc.DrawLine(GridPen, new Point(0, y), new Point(ActualWidth, y));
        }
        if (stars.Count == 0) return;
        var labels = new List<Rect>();
        foreach (var star in stars.OrderBy(s => s.Id == SelectedSystem ? 1 : 0))
        {
            var p = ToScreen(Project(star));
            if (p.X < -20 || p.Y < -20 || p.X > ActualWidth + 20 || p.Y > ActualHeight + 20) continue;
            var type = Math.Clamp(star.Data["primaryClass"]?.GetValue<int>() ?? 2, 0, 11);
            var selected = star.Id == SelectedSystem;
            var bookmarked = annotations.TryGetValue(star.Id, out var note) && note.Bookmarked;
            var r = Detailed ? (type == 6 ? 1.35 : .90) : type == 6 ? 3.4 : type == 5 ? 2.9 : 2.1;
            dc.PushOpacity(selected || !Detailed ? 1 : Math.Clamp(.47 + (scale / fitScale - 1) * .06, .47, 1));
            dc.DrawEllipse(StarBrushes[type], null, p, r, r); dc.Pop();
            if (bookmarked) dc.DrawEllipse(null, BookmarkPen, p, r + 4, r + 4);
            if (selected) dc.DrawEllipse(null, SelectionPen, p, 10, 10);
            if (selected || ShowNames && (bookmarked || star.Id < 18 || scale / fitScale > 4))
            {
                var text = Text(star.Name, selected ? "#C3FFF4" : "#A8BDD5", selected ? 12 : 10);
                var box = new Rect(p.X + 13, p.Y - 15, text.Width + 8, text.Height + 4);
                if (box.Right > ActualWidth - 8) box.X = p.X - text.Width - 18;
                if (selected || !labels.Any(rect => rect.IntersectsWith(box)))
                {
                    dc.DrawRoundedRectangle(Brush("#B30A1520"), null, new Rect(box.X - 3, box.Y - 2, box.Width + 4, box.Height + 2), 3, 3);
                    dc.DrawText(text, box.TopLeft); labels.Add(box);
                }
            }
        }
        var heading = Detailed ? "GALAXY" : "MAP";
        dc.DrawText(Text($"{heading}   /   {stars.Count:N0} STAR SYSTEMS", "#94ABC2", 10), new Point(14, 12));
        if (Detailed && artwork is null)
            dc.DrawText(Text(artworkError is null ? "Preparing galaxy detail…" : "Galaxy detail unavailable; systems remain selectable.", "#B3C9DD", 11), new Point(14, 30));
        dc.DrawText(Text(Detailed ? "Illustrative stellar light and dust · Selectable systems" : "Markers show star types, not physical sizes", "#91A5BE", 10),
            new Point(14, Math.Max(32, ActualHeight - 24)));
    }
    private FormattedText Text(string value, string color, double size) => new(value, CultureInfo.InvariantCulture, FlowDirection.LeftToRight, new Typeface("Segoe UI"), size, Brush(color), VisualTreeHelper.GetDpi(this).PixelsPerDip);
    private static SolidColorBrush Brush(string hex) { var brush = new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex)); brush.Freeze(); return brush; }
    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e) { Focus(); down = last = e.GetPosition(this); dragging = true; moved = false; CaptureMouse(); e.Handled = true; }
    protected override void OnMouseRightButtonDown(MouseButtonEventArgs e) { if (!Detailed) return; Focus(); last = e.GetPosition(this); orbiting = true; CaptureMouse(); e.Handled = true; }
    protected override void OnMouseMove(MouseEventArgs e)
    {
        var p = e.GetPosition(this);
        if (orbiting) { var delta = p - last; SetOrientation(RotationDegrees + delta.X * .3, TiltDegrees + delta.Y * .2); last = p; e.Handled = true; return; }
        if (!dragging) return;
        if ((p - down).Length > 4) moved = true;
        if (moved) PanBy(p - last);
        last = p;
    }
    protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
    {
        if (!dragging) return; dragging = false; ReleaseMouseCapture();
        if (!moved) SelectAt(e.GetPosition(this)); e.Handled = true;
    }
    protected override void OnMouseRightButtonUp(MouseButtonEventArgs e) { if (!orbiting) return; orbiting = false; ReleaseMouseCapture(); e.Handled = true; }
    protected override void OnLostMouseCapture(MouseEventArgs e) { dragging = false; orbiting = false; base.OnLostMouseCapture(e); }
    public bool SelectAt(Point p)
    {
        var star = stars.OrderBy(s => (ToScreen(Project(s)) - p).LengthSquared).FirstOrDefault();
        if (star is null || (ToScreen(Project(star)) - p).Length > 13) return false;
        SelectedSystem = star.Id; SystemSelected?.Invoke(star.Id); InvalidateVisual(); return true;
    }
    protected override void OnMouseWheel(MouseWheelEventArgs e) { ZoomAt(e.Delta > 0 ? 1.2 : 1 / 1.2, e.GetPosition(this)); e.Handled = true; }
    protected override void OnKeyDown(KeyEventArgs e) { if (e.Key == Key.F) { FocusSystem(SelectedSystem); e.Handled = true; } base.OnKeyDown(e); }
}
