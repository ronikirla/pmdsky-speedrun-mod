using Avalonia;
using UplinkReader.ConsoleMode;
using UplinkReader.SelfTest;
using UplinkReader.Ui;

namespace UplinkReader;

public static class Program
{
    // Modes:
    //   (default)   GUI (Avalonia): port detection, connect control, status
    //   --console   headless reader with a per-frame console log (debugging)
    //   --selftest  offline protocol self-test (no device needed)
    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Contains("--selftest"))
            return SelfTestRunner.Run();

        if (args.Contains("--console"))
            return ConsoleRunner.Run(args);

        return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>()
        .UsePlatformDetect()
        .LogToTrace();
}
