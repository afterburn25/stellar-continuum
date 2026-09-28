using Godot;

namespace Game.Presentation;

/// <summary>Reflows cards at the compact breakpoint; text and hit targets keep their size.</summary>
public partial class ResponsiveGrid : GridContainer
{
    public int ReferenceColumns { get; set; } = 3;
    public int CompactColumns { get; set; } = 2;
    public override void _Ready()
    {
        GetViewport().SizeChanged += Reflow;
        Reflow();
    }
    public override void _ExitTree() => GetViewport().SizeChanged -= Reflow;
    private void Reflow() => Columns = GetViewportRect().Size.X < 1440 ? CompactColumns : ReferenceColumns;
}
