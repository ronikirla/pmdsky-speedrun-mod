using System.Diagnostics;
using System.Runtime.InteropServices;
using UplinkReader.Link;

namespace UplinkReader.ConsoleMode;

/// <summary>
/// Headless reader: the same UplinkClient as the GUI, with a per-frame
/// console log (one line at ~60 Hz) for debugging. Ctrl+C stops.
///
///   uplink-reader --console                 auto-detect, per-frame log
///   uplink-reader --console --port COM5     explicit port
///   uplink-reader --console --duration 10   stop after 10 s
///   uplink-reader --console --list          list ports (with VID:PID) and exit
/// </summary>
public static class ConsoleRunner
{
    public static int Run(string[] args)
    {
        EnsureConsole();

        string? port = null;
        double? duration = null;
        bool list = false;

        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--console":
                    break;
                case "--port":
                    if (i + 1 >= args.Length)
                        return Fail("--port needs a value (e.g. COM5 or /dev/ttyACM0)");
                    port = args[++i];
                    break;
                case "--duration":
                    if (i + 1 >= args.Length || !double.TryParse(args[i + 1], out double d))
                        return Fail("--duration needs a number of seconds");
                    duration = d;
                    i++;
                    break;
                case "--list":
                    list = true;
                    break;
                default:
                    return Fail($"unknown option: {args[i]}");
            }
        }

        if (list)
        {
            Console.WriteLine("port        vid:pid   note");
            foreach (var p in PortDiscovery.ListPorts())
            {
                string vid = p.Vid is { } v ? v.ToString("X4") : "????";
                string pid = p.Pid is { } q ? q.ToString("X4") : "????";
                Console.WriteLine($"{p.Name,-10} {vid}:{pid}  {(p.IsUplink ? "<-- uplink" : "")}");
            }
            return 0;
        }

        var sw = Stopwatch.StartNew();
        var client = new UplinkClient();
        if (port is not null)
            client.PreferredPort = port;
        client.WantsConnection = true;

        client.Log += (_, m) => Console.WriteLine($"[{DateTime.Now:HH:mm:ss.fff}] {m}");
        client.DeviceLine += (_, l) => Console.WriteLine($"host> {l}");
        client.FrameReceived += (_, s) => Console.WriteLine(FrameLine(s));
        client.Start();

        Console.WriteLine("uplink reader (console mode); Ctrl+C to stop");
        Console.WriteLine("slots: " + string.Join(", ", UplinkSample.SlotNames));

        var done = new ManualResetEventSlim(false);
        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            done.Set();
        };
        Timer? durationTimer = duration is { } secs
            ? new Timer(_ => done.Set(), null, (int)(secs * 1000), Timeout.Infinite)
            : null;

        try
        {
            done.Wait();
        }
        finally
        {
            durationTimer?.Dispose();
            client.Stop();
            client.Dispose();
            PrintSummary(client, sw.Elapsed.TotalSeconds);
        }
        return 0;
    }

    private static int Fail(string message)
    {
        Console.Error.WriteLine(message);
        return 2;
    }

    // One line per frame: seq, game time, then every named slot value.
    private static string FrameLine(UplinkSample s)
    {
        var parts = new string[UplinkSample.SlotNames.Length];
        for (int i = 0; i < parts.Length; i++)
            parts[i] = $"{UplinkSample.SlotNames[i]}={s[i]}";
        return $"seq={s.Seq,-9} {s.FormatGameTime()}  {string.Join("  ", parts)}";
    }

    private static void PrintSummary(UplinkClient c, double seconds)
    {
        double dt = Math.Max(seconds, 1e-9);
        Console.WriteLine();
        Console.WriteLine("--- summary ---");
        Console.WriteLine($"frames received : {c.FramesReceived}");
        Console.WriteLine($"bytes received  : {c.BytesReceived} ({c.BytesReceived / 1024 / dt:F1} KB/s)");
        Console.WriteLine($"checksum errors : {c.ChecksumErrors}");
        Console.WriteLine($"seq gaps        : {c.SeqGaps} ({c.GapFramesLost} frames lost)");
        Console.WriteLine($"device lines    : {c.DeviceLines}");
        Console.WriteLine($"reconnects      : {c.Reconnects}");
    }

    // The app is a GUI-subsystem binary; when run without a console
    // (double-click on Windows), allocate one so the log has somewhere to go.
    private static void EnsureConsole()
    {
        if (OperatingSystem.IsWindows() && GetConsoleWindow() == IntPtr.Zero)
            AllocConsole();
    }

    [DllImport("kernel32.dll")]
    private static extern IntPtr GetConsoleWindow();

    [DllImport("kernel32.dll")]
    private static extern bool AllocConsole();
}
