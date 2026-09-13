using System.IO;
using System.Windows;

namespace Stellar.Editor;
public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        DispatcherUnhandledException += (_, error) =>
        {
            error.Handled = true;
            var folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "StellarEngineEditor", "Logs");
            try { Directory.CreateDirectory(folder); File.AppendAllText(Path.Combine(folder, "editor.log"), $"{DateTimeOffset.Now:O}\n{error.Exception}\n"); } catch { }
            MessageBox.Show(error.Exception.Message, "Stellar Engine Editor", MessageBoxButton.OK, MessageBoxImage.Error);
        };
        var window = new MainWindow();
        MainWindow = window;
        if (e.Args.Length == 2 && e.Args[0] == "--verify")
        {
            window.Left = -20000; window.Top = -20000; window.ShowInTaskbar = false;
            window.Loaded += async (_, _) => await EditorVerification.Run(window, e.Args[1]);
        }
        else if (e.Args.Length == 2 && e.Args[0] == "--open-and-capture")
        {
            window.Loaded += async (_, _) =>
            {
                await window.StartAsync(null);
                await window.Viewport.ArtworkReady;
                await window.Dispatcher.InvokeAsync(() => { }, System.Windows.Threading.DispatcherPriority.ApplicationIdle);
                window.Viewport.Fit(); window.UpdateLayout();
                EditorVerification.Capture(window, e.Args[1]);
            };
        }
        else window.Loaded += async (_, _) => await window.StartAsync(e.Args.FirstOrDefault());
        window.Show();
    }
}
